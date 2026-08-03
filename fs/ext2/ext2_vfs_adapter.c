#include "ext2.h"
#include "fs/vfs/vfs.h"
#include "lib/libc/stdio.h"
#include "lib/libc/string.h"
#include "lib/libc/stdlib.h"
#include "drivers/block/ata.h"

#define EXT2_VFS_PATH_MAX 256

static bool ext2_volume_at(drive_t* drive, uint32_t lba) {
    uint8_t sector[512];
    if (!drive || lba > UINT32_MAX - 2 ||
        !ata_read_sector(drive->base, lba + 2, sector, drive->is_master)) {
        return false;
    }
    return sector[56] == 0x53 && sector[57] == 0xEF;
}

static bool ext2_find_volume(drive_t* drive, uint32_t* partition_lba,
                             uint32_t* volume_sectors) {
    if (!drive || !partition_lba || !volume_sectors) return false;
    if (ext2_volume_at(drive, 0)) {
        *partition_lba = 0;
        *volume_sectors = drive->sectors;
        return true;
    }

    uint8_t mbr[512];
    if (!ata_read_sector(drive->base, 0, mbr, drive->is_master) ||
        mbr[510] != 0x55 || mbr[511] != 0xAA) {
        return false;
    }
    for (uint32_t i = 0; i < 4; i++) {
        const uint8_t* partition = mbr + 446 + i * 16;
        if (partition[4] != 0x83) continue;
        uint32_t lba = (uint32_t)partition[8] |
                       ((uint32_t)partition[9] << 8) |
                       ((uint32_t)partition[10] << 16) |
                       ((uint32_t)partition[11] << 24);
        uint32_t sectors = (uint32_t)partition[12] |
                           ((uint32_t)partition[13] << 8) |
                           ((uint32_t)partition[14] << 16) |
                           ((uint32_t)partition[15] << 24);
        if (lba != 0 && sectors != 0 &&
            (uint64_t)lba + sectors <= (uint64_t)UINT32_MAX + 1u &&
            (drive->sectors == 0 || (uint64_t)lba + sectors <= drive->sectors) &&
            ext2_volume_at(drive, lba)) {
            *partition_lba = lba;
            *volume_sectors = sectors;
            return true;
        }
    }
    return false;
}

static vfs_node_type_t ext2_mode_to_vfs(uint16_t mode) {
    switch (mode & EXT2_S_IFMT) {
        case EXT2_S_IFDIR: return VFS_DIRECTORY;
        case EXT2_S_IFLNK: return VFS_SYMLINK;
        case EXT2_S_IFCHR: return VFS_CHARDEVICE;
        case EXT2_S_IFBLK: return VFS_BLOCKDEVICE;
        case EXT2_S_IFIFO: return VFS_PIPE;
        default: return VFS_FILE;
    }
}

static vfs_node_t* ext2_make_node(vfs_filesystem_t* fs, const char* name,
                                  uint32_t name_len, uint32_t inode_num,
                                  const ext2_inode_t* inode) {
    vfs_node_t* node = (vfs_node_t*)malloc(sizeof(vfs_node_t));
    if (!node) return NULL;
    memset(node, 0, sizeof(*node));
    if (name_len > 255) name_len = 255;
    memcpy(node->name, name, name_len);
    node->name[name_len] = '\0';
    node->inode = inode_num;
    node->size = inode->i_size;
    node->type = ext2_mode_to_vfs(inode->i_mode);
    node->fs = fs;
    return node;
}

static int ext2_resolve(vfs_filesystem_t* fs, const char* path,
                        ext2_dir_entry_t* result_entry,
                        ext2_inode_t* result_inode) {
    if (!fs || !fs->fs_data || !path || !result_inode ||
        strlen(path) >= EXT2_VFS_PATH_MAX) {
        return VFS_ERR_INVALID;
    }
    ext2_fs_t* ext2_fs = (ext2_fs_t*)fs->fs_data;
    char copy[EXT2_VFS_PATH_MAX];
    strcpy(copy, path);
    char* cursor = copy;
    while (*cursor == '/') cursor++;
    if (*cursor == '\0') {
        if (!ext2_read_inode(ext2_fs, EXT2_ROOT_INO, result_inode))
            return VFS_ERR_IO;
        if (result_entry) {
            memset(result_entry, 0, sizeof(*result_entry));
            result_entry->inode = EXT2_ROOT_INO;
            result_entry->name_len = 1;
            result_entry->name[0] = '/';
            result_entry->name[1] = '\0';
            result_entry->file_type = EXT2_FT_DIR;
        }
        return VFS_OK;
    }

    uint32_t directory = EXT2_ROOT_INO;
    char* save = NULL;
    char* token = strtok_r(cursor, "/", &save);
    while (token) {
        char* next = strtok_r(NULL, "/", &save);
        ext2_dir_entry_t entry;
        int find_result =
            ext2_find_entry_status(ext2_fs, directory, token, &entry);
        if (find_result < 0) return VFS_ERR_IO;
        if (find_result == 0) return VFS_ERR_NOT_FOUND;
        ext2_inode_t inode;
        if (!ext2_read_inode(ext2_fs, entry.inode, &inode))
            return VFS_ERR_IO;
        if (next && (inode.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR)
            return VFS_ERR_NOT_DIR;
        if (!next) {
            if (result_entry) *result_entry = entry;
            *result_inode = inode;
            return VFS_OK;
        }
        directory = entry.inode;
        token = next;
    }
    return VFS_ERR_NOT_FOUND;
}

static void ext2_fill_stat(const ext2_dir_entry_t* source,
                           const ext2_inode_t* inode,
                           vfs_dir_entry_t* target) {
    memset(target, 0, sizeof(*target));
    uint32_t length = source->name_len;
    if (length > 255) length = 255;
    memcpy(target->name, source->name, length);
    target->name[length] = '\0';
    target->inode = source->inode;
    target->size = inode->i_size;
    target->type = ext2_mode_to_vfs(inode->i_mode);
    target->access_time = inode->i_atime;
    target->create_time = inode->i_ctime;
    target->modify_time = inode->i_mtime;
}

static int ext2_vfs_mount(vfs_filesystem_t* fs, drive_t* drive) {
    if (!fs || !drive) return VFS_ERR_INVALID;
    uint32_t partition_lba;
    uint32_t volume_sectors;
    if (!ext2_find_volume(drive, &partition_lba, &volume_sectors))
        return VFS_ERR_INVALID;

    ext2_fs_t* ext2_fs = (ext2_fs_t*)malloc(sizeof(ext2_fs_t));
    if (!ext2_fs) return VFS_ERR_NO_MEMORY;
    if (!ext2_init_volume(ext2_fs, drive->base, drive->is_master,
                          partition_lba, volume_sectors)) {
        free(ext2_fs);
        return VFS_ERR_IO;
    }
    fs->fs_data = ext2_fs;

    ext2_inode_t root_inode;
    if (!ext2_read_inode(ext2_fs, EXT2_ROOT_INO, &root_inode) ||
        (root_inode.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR) {
        ext2_cleanup(ext2_fs);
        free(ext2_fs);
        fs->fs_data = NULL;
        return VFS_ERR_INVALID;
    }
    fs->root = ext2_make_node(fs, "/", 1, EXT2_ROOT_INO, &root_inode);
    if (!fs->root) {
        ext2_cleanup(ext2_fs);
        free(ext2_fs);
        fs->fs_data = NULL;
        return VFS_ERR_NO_MEMORY;
    }
    return VFS_OK;
}

static int ext2_vfs_unmount(vfs_filesystem_t* fs) {
    if (!fs || !fs->fs_data) return VFS_ERR_INVALID;
    if (fs->root) free(fs->root);
    fs->root = NULL;
    ext2_fs_t* ext2_fs = (ext2_fs_t*)fs->fs_data;
    ext2_cleanup(ext2_fs);
    free(ext2_fs);
    fs->fs_data = NULL;
    return VFS_OK;
}

static int ext2_vfs_open(vfs_filesystem_t* fs, const char* path,
                         vfs_node_t** node) {
    if (!fs || !path || !node) return VFS_ERR_INVALID;
    *node = NULL;
    if (strcmp(path, "/") == 0 || path[0] == '\0') {
        *node = fs->root;
        return fs->root ? VFS_OK : VFS_ERR_INVALID;
    }
    ext2_dir_entry_t entry;
    ext2_inode_t inode;
    int result = ext2_resolve(fs, path, &entry, &inode);
    if (result != VFS_OK) return result;
    *node = ext2_make_node(fs, entry.name, entry.name_len, entry.inode,
                           &inode);
    return *node ? VFS_OK : VFS_ERR_NO_MEMORY;
}

static int ext2_vfs_close(vfs_node_t* node) {
    if (!node || !node->fs) return VFS_ERR_INVALID;
    if (node != node->fs->root) free(node);
    return VFS_OK;
}

static int ext2_vfs_read(vfs_node_t* node, uint32_t offset, uint32_t size,
                         uint8_t* buffer) {
    if (!node || !node->fs || !node->fs->fs_data ||
        (!buffer && size != 0)) return VFS_ERR_INVALID;
    if (node->type == VFS_DIRECTORY) return VFS_ERR_IS_DIR;
    ext2_fs_t* ext2_fs = (ext2_fs_t*)node->fs->fs_data;
    ext2_inode_t inode;
    if (!ext2_read_inode(ext2_fs, node->inode, &inode)) return VFS_ERR_IO;
    int result = ext2_read_file(ext2_fs, &inode, offset, size, buffer);
    return result < 0 ? VFS_ERR_IO : result;
}

static int ext2_vfs_write(vfs_node_t* node, uint32_t offset, uint32_t size,
                          const uint8_t* buffer) {
    return VFS_ERR_READ_ONLY;
}

static int ext2_vfs_readdir(vfs_node_t* node, uint32_t index,
                            vfs_dir_entry_t* entry) {
    if (!node || !node->fs || !node->fs->fs_data || !entry)
        return VFS_ERR_INVALID;
    if (node->type != VFS_DIRECTORY) return VFS_ERR_NOT_DIR;
    ext2_fs_t* ext2_fs = (ext2_fs_t*)node->fs->fs_data;
    ext2_dir_entry_t ext2_entry;
    int result = ext2_get_dir_entry(ext2_fs, node->inode, index, &ext2_entry);
    if (result < 0) return VFS_ERR_IO;
    if (result == 0) return VFS_ERR_NOT_FOUND;
    ext2_inode_t inode;
    if (!ext2_read_inode(ext2_fs, ext2_entry.inode, &inode))
        return VFS_ERR_IO;
    ext2_fill_stat(&ext2_entry, &inode, entry);
    return VFS_OK;
}

static int ext2_vfs_finddir(vfs_node_t* node, const char* name,
                            vfs_node_t** child) {
    if (!node || !name || !child || !node->fs || !node->fs->fs_data)
        return VFS_ERR_INVALID;
    if (node->type != VFS_DIRECTORY) return VFS_ERR_NOT_DIR;
    *child = NULL;
    ext2_fs_t* ext2_fs = (ext2_fs_t*)node->fs->fs_data;
    ext2_dir_entry_t entry;
    int find_result = ext2_find_entry_status(ext2_fs, node->inode,
                                             name, &entry);
    if (find_result < 0) return VFS_ERR_IO;
    if (find_result == 0) return VFS_ERR_NOT_FOUND;
    ext2_inode_t inode;
    if (!ext2_read_inode(ext2_fs, entry.inode, &inode)) return VFS_ERR_IO;
    *child = ext2_make_node(node->fs, entry.name, entry.name_len,
                            entry.inode, &inode);
    return *child ? VFS_OK : VFS_ERR_NO_MEMORY;
}

static int ext2_vfs_mkdir(vfs_filesystem_t* fs, const char* path) {
    return VFS_ERR_READ_ONLY;
}

static int ext2_vfs_rmdir(vfs_filesystem_t* fs, const char* path) {
    return VFS_ERR_READ_ONLY;
}

static int ext2_vfs_create(vfs_filesystem_t* fs, const char* path) {
    return VFS_ERR_READ_ONLY;
}

static int ext2_vfs_delete(vfs_filesystem_t* fs, const char* path) {
    return VFS_ERR_READ_ONLY;
}

static int ext2_vfs_stat(vfs_filesystem_t* fs, const char* path,
                         vfs_dir_entry_t* stat) {
    if (!fs || !path || !stat) return VFS_ERR_INVALID;
    ext2_dir_entry_t entry;
    ext2_inode_t inode;
    int result = ext2_resolve(fs, path, &entry, &inode);
    if (result != VFS_OK) return result;
    ext2_fill_stat(&entry, &inode, stat);
    return VFS_OK;
}

static vfs_filesystem_ops_t ext2_vfs_ops = {
    .mount = ext2_vfs_mount,
    .unmount = ext2_vfs_unmount,
    .open = ext2_vfs_open,
    .close = ext2_vfs_close,
    .read = ext2_vfs_read,
    .write = ext2_vfs_write,
    .readdir = ext2_vfs_readdir,
    .finddir = ext2_vfs_finddir,
    .mkdir = ext2_vfs_mkdir,
    .rmdir = ext2_vfs_rmdir,
    .create = ext2_vfs_create,
    .delete = ext2_vfs_delete,
    .stat = ext2_vfs_stat
};

bool ext2_register_vfs(void) {
    return vfs_register_filesystem("ext2", &ext2_vfs_ops) == VFS_OK;
}
