#include "fs/vfs/vfs.h"
#include "fat12.h"
#include "lib/libc/string.h"
#include "lib/libc/stdlib.h"
#include "drivers/bus/drives.h"

#define FAT12_VFS_PATH_MAX 256

extern fat12_t* fat12;
extern directory_entry* entries;

/* The legacy core owns one global FAT and directory buffer.  A second live
   mount would redirect operations of the first mount to another floppy. */
static vfs_filesystem_t* mounted_fat12_fs = NULL;

static void fat12_entry_name(const directory_entry* entry, char name[13]) {
    uint32_t length = 0;
    for (uint32_t i = 0; i < 8 && entry->filename[i] != ' '; i++)
        name[length++] = (char)entry->filename[i];
    uint32_t extension_length = 0;
    while (extension_length < 3 && entry->extension[extension_length] != ' ')
        extension_length++;
    if (extension_length != 0) {
        name[length++] = '.';
        for (uint32_t i = 0; i < extension_length; i++)
            name[length++] = entry->extension[i];
    }
    name[length] = '\0';
}

static bool fat12_names_equal(const char* first, const char* second) {
    size_t first_length = strlen(first);
    size_t second_length = strlen(second);
    return first_length == second_length &&
           strncasecmp(first, second, first_length) == 0;
}

static int fat12_find_in_directory(uint32_t directory_cluster,
                                   const char* name,
                                   directory_entry* result) {
    if (!name || !result) return VFS_ERR_INVALID;
    directory_entry directory;
    directory_entry* directory_ptr = NULL;
    if (directory_cluster != 0) {
        memset(&directory, 0, sizeof(directory));
        directory.first_cluster_low = (uint16_t)directory_cluster;
        directory.attributes = FILE_ATTR_DIRECTORY;
        directory_ptr = &directory;
    }
    int count = fat12_read_dir_entries(directory_ptr);
    if (count < 0) return VFS_ERR_IO;
    for (int i = 0; i < count; i++) {
        char formatted[13];
        fat12_entry_name(&entries[i], formatted);
        if (fat12_names_equal(formatted, name)) {
            *result = entries[i];
            return VFS_OK;
        }
    }
    return VFS_ERR_NOT_FOUND;
}

static int fat12_resolve(const char* path, directory_entry* result) {
    if (!path || !result || strlen(path) >= FAT12_VFS_PATH_MAX)
        return VFS_ERR_INVALID;
    char copy[FAT12_VFS_PATH_MAX];
    strcpy(copy, path);
    char* cursor = copy;
    while (*cursor == '/') cursor++;
    if (*cursor == '\0') return VFS_ERR_IS_DIR;

    uint32_t directory = 0;
    char* save = NULL;
    char* token = strtok_r(cursor, "/", &save);
    while (token) {
        char* next = strtok_r(NULL, "/", &save);
        directory_entry found;
        int status = fat12_find_in_directory(directory, token, &found);
        if (status != VFS_OK) return status;
        if (!next) {
            *result = found;
            return VFS_OK;
        }
        if (!(found.attributes & FILE_ATTR_DIRECTORY))
            return VFS_ERR_NOT_DIR;
        directory = found.first_cluster_low;
        token = next;
    }
    return VFS_ERR_NOT_FOUND;
}

static vfs_node_t* fat12_make_node(vfs_filesystem_t* fs,
                                   const directory_entry* entry) {
    vfs_node_t* node = (vfs_node_t*)malloc(sizeof(vfs_node_t));
    if (!node) return NULL;
    memset(node, 0, sizeof(*node));
    char name[13];
    fat12_entry_name(entry, name);
    strcpy(node->name, name);
    node->type = (entry->attributes & FILE_ATTR_DIRECTORY) ?
                 VFS_DIRECTORY : VFS_FILE;
    node->inode = entry->first_cluster_low;
    node->size = entry->file_size;
    node->fs = fs;

    if (node->type == VFS_FILE) {
        fat12_file* file = (fat12_file*)malloc(sizeof(fat12_file));
        if (!file) {
            free(node);
            return NULL;
        }
        memset(file, 0, sizeof(*file));
        file->start_cluster = entry->first_cluster_low;
        file->size = entry->file_size;
        strcpy(file->mode, "r");
        file->fat12_instance = fat12;
        strcpy((char*)file->name, name);
        node->fs_specific = file;
    }
    return node;
}

static void fat12_fill_stat(const directory_entry* source,
                            vfs_dir_entry_t* target) {
    memset(target, 0, sizeof(*target));
    fat12_entry_name(source, target->name);
    target->type = (source->attributes & FILE_ATTR_DIRECTORY) ?
                   VFS_DIRECTORY : VFS_FILE;
    target->size = source->file_size;
    target->inode = source->first_cluster_low;
    target->attributes = source->attributes;
}

static int fat12_vfs_mount(vfs_filesystem_t* fs, drive_t* drive) {
    if (!fs || !drive) return VFS_ERR_INVALID;
    if (mounted_fat12_fs) return VFS_ERR_BUSY;
    if (!fat12_init_fs(drive->fdd_drive_no)) return VFS_ERR_IO;
    fs->fs_data = fat12;

    vfs_node_t* root = (vfs_node_t*)malloc(sizeof(vfs_node_t));
    if (!root) {
        fat12_cleanup();
        fs->fs_data = NULL;
        return VFS_ERR_NO_MEMORY;
    }
    memset(root, 0, sizeof(*root));
    strcpy(root->name, "/");
    root->type = VFS_DIRECTORY;
    root->fs = fs;
    fs->root = root;
    mounted_fat12_fs = fs;
    return VFS_OK;
}

static int fat12_vfs_unmount(vfs_filesystem_t* fs) {
    if (!fs) return VFS_ERR_INVALID;
    if (fs->root) free(fs->root);
    fs->root = NULL;
    fat12_cleanup();
    fs->fs_data = NULL;
    if (mounted_fat12_fs == fs) mounted_fat12_fs = NULL;
    return VFS_OK;
}

static int fat12_vfs_open(vfs_filesystem_t* fs, const char* path,
                          vfs_node_t** node) {
    if (!fs || !path || !node) return VFS_ERR_INVALID;
    *node = NULL;
    if (strcmp(path, "/") == 0 || path[0] == '\0') {
        *node = fs->root;
        return fs->root ? VFS_OK : VFS_ERR_INVALID;
    }
    directory_entry entry;
    int status = fat12_resolve(path, &entry);
    if (status != VFS_OK) return status;
    *node = fat12_make_node(fs, &entry);
    return *node ? VFS_OK : VFS_ERR_NO_MEMORY;
}

static int fat12_vfs_close(vfs_node_t* node) {
    if (!node || !node->fs) return VFS_ERR_INVALID;
    if (node == node->fs->root) return VFS_OK;
    if (node->fs_specific) fat12_close_file((fat12_file*)node->fs_specific);
    free(node);
    return VFS_OK;
}

static int fat12_vfs_read(vfs_node_t* node, uint32_t offset, uint32_t size,
                          uint8_t* buffer) {
    if (!node || (!buffer && size != 0)) return VFS_ERR_INVALID;
    if (node->type != VFS_FILE) return VFS_ERR_IS_DIR;
    fat12_file* file = (fat12_file*)node->fs_specific;
    if (!file) return VFS_ERR_INVALID;
    if (offset >= node->size || size == 0) return 0;
    file->position = offset;
    uint32_t available = node->size - offset;
    uint32_t amount = size < available ? size : available;
    return fat12_read_file(file, buffer, size, amount);
}

static int fat12_vfs_write(vfs_node_t* node, uint32_t offset, uint32_t size,
                           const uint8_t* buffer) {
    return VFS_ERR_READ_ONLY;
}

static int fat12_vfs_readdir(vfs_node_t* node, uint32_t index,
                             vfs_dir_entry_t* entry) {
    if (!node || !entry) return VFS_ERR_INVALID;
    if (node->type != VFS_DIRECTORY) return VFS_ERR_NOT_DIR;
    directory_entry directory;
    directory_entry* directory_ptr = NULL;
    if (node->inode != 0) {
        memset(&directory, 0, sizeof(directory));
        directory.first_cluster_low = (uint16_t)node->inode;
        directory.attributes = FILE_ATTR_DIRECTORY;
        directory_ptr = &directory;
    }
    int count = fat12_read_dir_entries(directory_ptr);
    if (count < 0) return VFS_ERR_IO;
    if (!entries || index >= (uint32_t)count) return VFS_ERR_NOT_FOUND;
    fat12_fill_stat(&entries[index], entry);
    return VFS_OK;
}

static int fat12_vfs_finddir(vfs_node_t* node, const char* name,
                             vfs_node_t** child) {
    if (!node || !name || !child) return VFS_ERR_INVALID;
    if (node->type != VFS_DIRECTORY) return VFS_ERR_NOT_DIR;
    *child = NULL;
    directory_entry entry;
    int status = fat12_find_in_directory(node->inode, name, &entry);
    if (status != VFS_OK) return status;
    *child = fat12_make_node(node->fs, &entry);
    return *child ? VFS_OK : VFS_ERR_NO_MEMORY;
}

static int fat12_vfs_mkdir(vfs_filesystem_t* fs, const char* path) {
    return VFS_ERR_READ_ONLY;
}

static int fat12_vfs_rmdir(vfs_filesystem_t* fs, const char* path) {
    return VFS_ERR_READ_ONLY;
}

static int fat12_vfs_create(vfs_filesystem_t* fs, const char* path) {
    return VFS_ERR_READ_ONLY;
}

static int fat12_vfs_delete(vfs_filesystem_t* fs, const char* path) {
    return VFS_ERR_READ_ONLY;
}

static int fat12_vfs_stat(vfs_filesystem_t* fs, const char* path,
                          vfs_dir_entry_t* stat) {
    if (!fs || !path || !stat) return VFS_ERR_INVALID;
    if (strcmp(path, "/") == 0 || path[0] == '\0') {
        memset(stat, 0, sizeof(*stat));
        strcpy(stat->name, "/");
        stat->type = VFS_DIRECTORY;
        return VFS_OK;
    }
    directory_entry entry;
    int status = fat12_resolve(path, &entry);
    if (status != VFS_OK) return status;
    fat12_fill_stat(&entry, stat);
    return VFS_OK;
}

static int fat12_vfs_space(vfs_filesystem_t* fs, vfs_space_info_t* info) {
    if (!fs || !fs->fs_data || !info) return VFS_ERR_INVALID;
    fat12_t* volume = (fat12_t*)fs->fs_data;
    const fat12_boot_sector* boot = &volume->boot_sector;
    uint32_t root_sectors = ((uint32_t)boot->root_entry_count * 32U +
                             boot->bytes_per_sector - 1U) /
                            boot->bytes_per_sector;
    uint32_t total_sectors = boot->total_sectors != 0
        ? boot->total_sectors : boot->total_sectors_large;
    uint32_t data_start = boot->reserved_sectors +
                          (uint32_t)boot->fat_count * boot->sectors_per_fat +
                          root_sectors;
    if (boot->bytes_per_sector == 0 || boot->sectors_per_cluster == 0 ||
        total_sectors <= data_start || volume->fat == NULL) return VFS_ERR_IO;
    uint32_t clusters = (total_sectors - data_start) /
                        boot->sectors_per_cluster;
    uint32_t free_clusters = 0;
    uint32_t fat_bytes = (uint32_t)boot->sectors_per_fat *
                         boot->bytes_per_sector;
    uint32_t fat_clusters = (fat_bytes * 2U) / 3U;
    fat_clusters = fat_clusters > 2U ? fat_clusters - 2U : 0U;
    if (clusters > fat_clusters) clusters = fat_clusters;
    for (uint32_t cluster = 2; cluster <= clusters + 1U; ++cluster) {
        uint32_t offset = cluster + cluster / 2U;
        if (offset + 1U >= fat_bytes) return VFS_ERR_IO;
        uint16_t value = (uint16_t)volume->fat[offset] |
                         ((uint16_t)volume->fat[offset + 1U] << 8);
        value = (cluster & 1U) != 0 ? value >> 4 : value & 0x0FFFU;
        if (value == FAT12_FREE_CLUSTER) ++free_clusters;
    }
    uint64_t cluster_bytes = (uint64_t)boot->bytes_per_sector *
                             boot->sectors_per_cluster;
    info->total_bytes = (uint64_t)clusters * cluster_bytes;
    info->free_bytes = (uint64_t)free_clusters * cluster_bytes;
    return VFS_OK;
}

vfs_filesystem_ops_t fat12_vfs_ops = {
    .mount = fat12_vfs_mount,
    .unmount = fat12_vfs_unmount,
    .open = fat12_vfs_open,
    .close = fat12_vfs_close,
    .read = fat12_vfs_read,
    .write = fat12_vfs_write,
    .readdir = fat12_vfs_readdir,
    .finddir = fat12_vfs_finddir,
    .mkdir = fat12_vfs_mkdir,
    .rmdir = fat12_vfs_rmdir,
    .create = fat12_vfs_create,
    .delete = fat12_vfs_delete,
    .stat = fat12_vfs_stat,
    .space = fat12_vfs_space
};

void fat12_register_vfs(void) {
    (void)vfs_register_filesystem("fat12", &fat12_vfs_ops);
}
