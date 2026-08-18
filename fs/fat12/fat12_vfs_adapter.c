/**
 * @file fs/fat12/fat12_vfs_adapter.c
 * @brief VFS-Adapter für resilient FAT12.
 *
 * Layer: Ring-0 virtual filesystem and on-disk format.
 * Contract: On-Disk-Werte, Pfade und Ressourcenbereiche werden vor I/O validiert.
 * Safety: VFS-Mutationen durchlaufen Journal-, Remap- und Critical-Metadata-Verträge.
 */
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

static char fat12_display_character(char value, bool lowercase) {
    return lowercase && value >= 'A' && value <= 'Z'
        ? (char)(value + ('a' - 'A')) : value;
}

static void fat12_entry_name(const directory_entry* entry, char name[13]) {
    uint32_t length = 0;
    for (uint32_t i = 0; i < 8 && entry->filename[i] != ' '; i++)
        name[length++] = fat12_display_character(
            (char)entry->filename[i], (entry->reserved & 0x08U) != 0U);
    uint32_t extension_length = 0;
    while (extension_length < 3 && entry->extension[extension_length] != ' ')
        extension_length++;
    if (extension_length != 0) {
        name[length++] = '.';
        for (uint32_t i = 0; i < extension_length; i++)
            name[length++] = fat12_display_character(
                entry->extension[i], (entry->reserved & 0x10U) != 0U);
    }
    name[length] = '\0';
}

static bool fat12_names_equal(const char* first, const char* second) {
    size_t first_length = strlen(first);
    size_t second_length = strlen(second);
    return first_length == second_length &&
           strncasecmp(first, second, first_length) == 0;
}

typedef struct {
    directory_entry entry;
    uint32_t sector;
    uint16_t slot;
} fat12_entry_location_t;

static uint32_t fat12_cluster_sector(uint16_t cluster) {
    return (uint32_t)fat12->data_start +
        ((uint32_t)cluster - 2U) * fat12->boot_sector.sectors_per_cluster;
}

static bool fat12_short_name(const char* name, uint8_t output[11]) {
    if (!name || !*name || strcmp(name, ".") == 0 ||
        strcmp(name, "..") == 0) return false;
    memset(output, ' ', 11);
    uint32_t base = 0;
    uint32_t extension = 0;
    bool dot = false;
    for (const char* cursor = name; *cursor; ++cursor) {
        unsigned char value = (unsigned char)*cursor;
        if (value == '.') {
            if (dot || base == 0) return false;
            dot = true;
            continue;
        }
        if (value >= 'a' && value <= 'z') value -= 'a' - 'A';
        bool allowed = (value >= 'A' && value <= 'Z') ||
                       (value >= '0' && value <= '9') ||
                       strchr("!#$%&'()-@^_`{}~", value) != NULL;
        if (!allowed) return false;
        if (!dot) {
            if (base == 8) return false;
            output[base++] = value;
        } else {
            if (extension == 3) return false;
            output[8U + extension++] = value;
        }
    }
    return base != 0 && (!dot || extension != 0);
}

static uint16_t fat12_allocate_cluster(void) {
    uint32_t count = fat12_cluster_count();
    uint8_t zero[FAT12_SECTOR_SIZE];
    memset(zero, 0, sizeof(zero));
    for (uint32_t candidate = 2; candidate < count + 2U; ++candidate) {
        if (fat12_get_fat_entry((uint16_t)candidate) != FAT12_FREE_CLUSTER)
            continue;
        if (!fat12_set_fat_entry((uint16_t)candidate, FAT12_EOC_MAX))
            return 0;
        uint32_t first = fat12_cluster_sector((uint16_t)candidate);
        for (uint32_t sector = 0;
             sector < fat12->boot_sector.sectors_per_cluster; ++sector) {
            if (!fat12_write_logical_sectors(first + sector, 1, zero)) {
                (void)fat12_set_fat_entry((uint16_t)candidate,
                                          FAT12_FREE_CLUSTER);
                return 0;
            }
        }
        return (uint16_t)candidate;
    }
    return 0;
}

static int fat12_scan_directory(uint16_t directory_cluster,
                                const char* name,
                                fat12_entry_location_t* result) {
    uint8_t sector_buffer[FAT12_SECTOR_SIZE];
    uint32_t root_sectors =
        ((uint32_t)fat12->boot_sector.root_entry_count * 32U +
         FAT12_SECTOR_SIZE - 1U) / FAT12_SECTOR_SIZE;
    uint16_t cluster = directory_cluster;
    uint32_t clusters_left = fat12_cluster_count();
    for (;;) {
        uint32_t first = directory_cluster == 0 ?
            (uint32_t)fat12->root_dir_start : fat12_cluster_sector(cluster);
        uint32_t sectors = directory_cluster == 0 ? root_sectors :
            fat12->boot_sector.sectors_per_cluster;
        for (uint32_t sector_index = 0; sector_index < sectors;
             ++sector_index) {
            uint32_t logical = first + sector_index;
            if (!fat12_read_logical_sectors(logical, 1, sector_buffer))
                return VFS_ERR_IO;
            directory_entry* sector_entries =
                (directory_entry*)sector_buffer;
            for (uint16_t slot = 0; slot < FAT12_SECTOR_SIZE / 32U; ++slot) {
                directory_entry* entry = &sector_entries[slot];
                if (entry->filename[0] == 0x00) return VFS_ERR_NOT_FOUND;
                if (entry->filename[0] == 0xE5 ||
                    entry->attributes == FILE_ATTR_LONG_NAME ||
                    (entry->attributes & FILE_ATTR_VOLUME_LABEL)) continue;
                char formatted[13];
                fat12_entry_name(entry, formatted);
                if (fat12_names_equal(formatted, name)) {
                    if (result) {
                        result->entry = *entry;
                        result->sector = logical;
                        result->slot = slot;
                    }
                    return VFS_OK;
                }
            }
        }
        if (directory_cluster == 0) return VFS_ERR_NOT_FOUND;
        uint16_t next = fat12_get_fat_entry(cluster);
        if (next >= FAT12_EOC_MIN) return VFS_ERR_NOT_FOUND;
        if (!is_valid_cluster_fat12(next) || clusters_left-- == 0)
            return VFS_ERR_IO;
        cluster = next;
    }
}

static int fat12_resolve_location(const char* path,
                                  fat12_entry_location_t* result) {
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
        fat12_entry_location_t found;
        int status = fat12_scan_directory((uint16_t)directory, token, &found);
        if (status != VFS_OK) return status;
        if (!next) {
            *result = found;
            return VFS_OK;
        }
        if (!(found.entry.attributes & FILE_ATTR_DIRECTORY))
            return VFS_ERR_NOT_DIR;
        directory = found.entry.first_cluster_low;
        token = next;
    }
    return VFS_ERR_NOT_FOUND;
}

static int fat12_resolve(const char* path, directory_entry* result) {
    fat12_entry_location_t location;
    int status = fat12_resolve_location(path, &location);
    if (status == VFS_OK) *result = location.entry;
    return status;
}

static vfs_node_t* fat12_make_node(vfs_filesystem_t* fs,
                                   const directory_entry* entry,
                                   const fat12_entry_location_t* location) {
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
        file->attributes = entry->attributes;
        if (location) {
            file->directory_sector = location->sector;
            file->directory_slot = location->slot;
        }
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

static int fat12_resolve_parent(const char* path, uint16_t* parent,
                                uint8_t short_name[11]) {
    if (!path || !parent || !short_name || strlen(path) >= FAT12_VFS_PATH_MAX)
        return VFS_ERR_INVALID;
    char copy[FAT12_VFS_PATH_MAX];
    strcpy(copy, path);
    char* cursor = copy;
    while (*cursor == '/') ++cursor;
    if (*cursor == '\0') return VFS_ERR_INVALID;
    uint16_t directory = 0;
    char* save = NULL;
    char* token = strtok_r(cursor, "/", &save);
    while (token) {
        char* next = strtok_r(NULL, "/", &save);
        if (!next) {
            if (!fat12_short_name(token, short_name)) return VFS_ERR_INVALID;
            *parent = directory;
            return VFS_OK;
        }
        fat12_entry_location_t location;
        int status = fat12_scan_directory(directory, token, &location);
        if (status != VFS_OK) return status;
        if (!(location.entry.attributes & FILE_ATTR_DIRECTORY))
            return VFS_ERR_NOT_DIR;
        directory = location.entry.first_cluster_low;
        token = next;
    }
    return VFS_ERR_INVALID;
}

static bool fat12_write_entry(const fat12_entry_location_t* location,
                              const directory_entry* entry) {
    uint8_t sector[FAT12_SECTOR_SIZE];
    if (!location || !entry || location->slot >= FAT12_SECTOR_SIZE / 32U ||
        !fat12_read_logical_sectors(location->sector, 1, sector)) return false;
    ((directory_entry*)sector)[location->slot] = *entry;
    return fat12_write_logical_sectors(location->sector, 1, sector);
}

static int fat12_find_free_slot(uint16_t directory_cluster,
                                fat12_entry_location_t* location,
                                bool* fat_changed) {
    if (!location || !fat_changed) return VFS_ERR_INVALID;
    uint8_t sector_buffer[FAT12_SECTOR_SIZE];
    bool have_deleted = false;
    fat12_entry_location_t deleted;
    uint32_t root_sectors =
        ((uint32_t)fat12->boot_sector.root_entry_count * 32U +
         FAT12_SECTOR_SIZE - 1U) / FAT12_SECTOR_SIZE;
    uint16_t cluster = directory_cluster;
    uint16_t last_cluster = directory_cluster;
    uint32_t clusters_left = fat12_cluster_count();
    for (;;) {
        uint32_t first = directory_cluster == 0 ?
            (uint32_t)fat12->root_dir_start : fat12_cluster_sector(cluster);
        uint32_t sectors = directory_cluster == 0 ? root_sectors :
            fat12->boot_sector.sectors_per_cluster;
        for (uint32_t sector_index = 0; sector_index < sectors;
             ++sector_index) {
            uint32_t logical = first + sector_index;
            if (!fat12_read_logical_sectors(logical, 1, sector_buffer))
                return VFS_ERR_IO;
            directory_entry* sector_entries =
                (directory_entry*)sector_buffer;
            for (uint16_t slot = 0; slot < FAT12_SECTOR_SIZE / 32U; ++slot) {
                uint8_t first_byte = sector_entries[slot].filename[0];
                if (first_byte == 0xE5 && !have_deleted) {
                    memset(&deleted, 0, sizeof(deleted));
                    deleted.sector = logical;
                    deleted.slot = slot;
                    have_deleted = true;
                }
                if (first_byte == 0x00) {
                    if (have_deleted) *location = deleted;
                    else {
                        memset(location, 0, sizeof(*location));
                        location->sector = logical;
                        location->slot = slot;
                    }
                    return VFS_OK;
                }
            }
        }
        if (directory_cluster == 0) {
            if (have_deleted) { *location = deleted; return VFS_OK; }
            return VFS_ERR_NO_SPACE;
        }
        last_cluster = cluster;
        uint16_t next = fat12_get_fat_entry(cluster);
        if (next >= FAT12_EOC_MIN) break;
        if (!is_valid_cluster_fat12(next) || clusters_left-- == 0)
            return VFS_ERR_IO;
        cluster = next;
    }
    if (have_deleted) { *location = deleted; return VFS_OK; }
    uint16_t added = fat12_allocate_cluster();
    if (added == 0 || !fat12_set_fat_entry(last_cluster, added))
        return VFS_ERR_NO_SPACE;
    *fat_changed = true;
    memset(location, 0, sizeof(*location));
    location->sector = fat12_cluster_sector(added);
    location->slot = 0;
    return VFS_OK;
}

static bool fat12_directory_is_empty(uint16_t directory_cluster) {
    uint8_t sector_buffer[FAT12_SECTOR_SIZE];
    uint16_t cluster = directory_cluster;
    uint32_t clusters_left = fat12_cluster_count();
    while (is_valid_cluster_fat12(cluster) && clusters_left-- != 0) {
        uint32_t first = fat12_cluster_sector(cluster);
        for (uint32_t sector_index = 0;
             sector_index < fat12->boot_sector.sectors_per_cluster;
             ++sector_index) {
            if (!fat12_read_logical_sectors(first + sector_index, 1,
                                            sector_buffer)) return false;
            directory_entry* directory = (directory_entry*)sector_buffer;
            for (uint32_t slot = 0; slot < FAT12_SECTOR_SIZE / 32U; ++slot) {
                directory_entry* entry = &directory[slot];
                if (entry->filename[0] == 0x00) return true;
                if (entry->filename[0] == 0xE5 ||
                    entry->attributes == FILE_ATTR_LONG_NAME ||
                    (entry->attributes & FILE_ATTR_VOLUME_LABEL)) continue;
                char name[13];
                fat12_entry_name(entry, name);
                if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0)
                    return false;
            }
        }
        uint16_t next = fat12_get_fat_entry(cluster);
        if (next >= FAT12_EOC_MIN) return true;
        if (!is_valid_cluster_fat12(next)) return false;
        cluster = next;
    }
    return false;
}

static bool fat12_free_chain(uint16_t start_cluster) {
    uint16_t cluster = start_cluster;
    uint32_t left = fat12_cluster_count();
    while (is_valid_cluster_fat12(cluster) && left-- != 0) {
        uint16_t next = fat12_get_fat_entry(cluster);
        if (!fat12_set_fat_entry(cluster, FAT12_FREE_CLUSTER)) return false;
        if (next >= FAT12_EOC_MIN) return true;
        if (!is_valid_cluster_fat12(next)) return false;
        cluster = next;
    }
    return start_cluster == 0;
}

static uint32_t fat12_chain_length(uint16_t start_cluster, bool* valid) {
    if (valid) *valid = false;
    if (start_cluster == 0) {
        if (valid) *valid = true;
        return 0;
    }
    uint16_t cluster = start_cluster;
    uint32_t length = 0;
    uint32_t left = fat12_cluster_count();
    while (is_valid_cluster_fat12(cluster) && left-- != 0) {
        ++length;
        uint16_t next = fat12_get_fat_entry(cluster);
        if (next >= FAT12_EOC_MIN) {
            if (valid) *valid = true;
            return length;
        }
        if (!is_valid_cluster_fat12(next)) return 0;
        cluster = next;
    }
    return 0;
}

static uint32_t fat12_free_clusters(void) {
    uint32_t free_clusters = 0;
    for (uint32_t cluster = 2; cluster < fat12_cluster_count() + 2U;
         ++cluster) {
        if (fat12_get_fat_entry((uint16_t)cluster) == FAT12_FREE_CLUSTER)
            ++free_clusters;
    }
    return free_clusters;
}

static bool fat12_ensure_chain(uint16_t* start_cluster,
                               uint32_t required_clusters) {
    if (!start_cluster) return false;
    if (required_clusters == 0) return true;
    uint16_t tail = *start_cluster;
    uint32_t existing = 0;
    uint32_t left = fat12_cluster_count();
    if (tail != 0) {
        while (is_valid_cluster_fat12(tail) && left-- != 0) {
            ++existing;
            uint16_t next = fat12_get_fat_entry(tail);
            if (next >= FAT12_EOC_MIN) break;
            if (!is_valid_cluster_fat12(next)) return false;
            tail = next;
        }
        if (existing == 0 || left == 0) return false;
    }
    while (existing < required_clusters) {
        uint16_t added = fat12_allocate_cluster();
        if (added == 0) return false;
        if (existing == 0) *start_cluster = added;
        else if (!fat12_set_fat_entry(tail, added)) return false;
        tail = added;
        ++existing;
    }
    return true;
}

static uint16_t fat12_cluster_at(uint16_t start, uint32_t index) {
    uint16_t cluster = start;
    while (index-- != 0 && is_valid_cluster_fat12(cluster)) {
        uint16_t next = fat12_get_fat_entry(cluster);
        if (!is_valid_cluster_fat12(next)) return 0;
        cluster = next;
    }
    return is_valid_cluster_fat12(cluster) ? cluster : 0;
}

static bool fat12_write_bytes(uint16_t start_cluster, uint32_t offset,
                              const uint8_t* input, uint32_t size) {
    uint32_t cluster_bytes = FAT12_SECTOR_SIZE *
        fat12->boot_sector.sectors_per_cluster;
    uint8_t sector_buffer[FAT12_SECTOR_SIZE];
    while (size != 0) {
        uint32_t cluster_index = offset / cluster_bytes;
        uint32_t within_cluster = offset % cluster_bytes;
        uint16_t cluster = fat12_cluster_at(start_cluster, cluster_index);
        if (cluster == 0) return false;
        uint32_t sector_index = within_cluster / FAT12_SECTOR_SIZE;
        uint32_t within_sector = within_cluster % FAT12_SECTOR_SIZE;
        uint32_t logical = fat12_cluster_sector(cluster) + sector_index;
        if (within_sector == 0 && size >= FAT12_SECTOR_SIZE) {
            uint32_t full_sectors = size / FAT12_SECTOR_SIZE;
            uint32_t run = fat12->boot_sector.sectors_per_cluster -
                           sector_index;
            if (run > full_sectors) run = full_sectors;
            uint16_t last_cluster = cluster;
            while (run < full_sectors) {
                uint16_t next = fat12_get_fat_entry(last_cluster);
                if (next != (uint16_t)(last_cluster + 1U)) break;
                uint32_t addition = fat12->boot_sector.sectors_per_cluster;
                if (addition > full_sectors - run)
                    addition = full_sectors - run;
                run += addition;
                last_cluster = next;
                if (addition < fat12->boot_sector.sectors_per_cluster) break;
            }
            if (!fat12_write_logical_sectors(logical, run, input))
                return false;
            uint32_t amount = run * FAT12_SECTOR_SIZE;
            input += amount;
            offset += amount;
            size -= amount;
            continue;
        }
        uint32_t amount = FAT12_SECTOR_SIZE - within_sector;
        if (amount > size) amount = size;
        if (within_sector != 0 || amount != FAT12_SECTOR_SIZE) {
            if (!fat12_read_logical_sectors(logical, 1, sector_buffer))
                return false;
        }
        if (within_sector == 0 && amount == FAT12_SECTOR_SIZE) {
            if (!fat12_write_logical_sectors(logical, 1, input)) return false;
        } else {
            memcpy(sector_buffer + within_sector, input, amount);
            if (!fat12_write_logical_sectors(logical, 1, sector_buffer))
                return false;
        }
        input += amount;
        offset += amount;
        size -= amount;
    }
    return true;
}

static bool fat12_zero_range(uint16_t start_cluster, uint32_t offset,
                             uint32_t size) {
    uint8_t zero[FAT12_SECTOR_SIZE];
    memset(zero, 0, sizeof(zero));
    while (size != 0) {
        uint32_t amount = size > sizeof(zero) ? sizeof(zero) : size;
        if (!fat12_write_bytes(start_cluster, offset, zero, amount))
            return false;
        offset += amount;
        size -= amount;
    }
    return true;
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
    fat12_entry_location_t location;
    int status = fat12_resolve_location(path, &location);
    if (status != VFS_OK) return status;
    *node = fat12_make_node(fs, &location.entry, &location);
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
    int result = fat12_read_file(file, buffer, size, amount);
    if (result == (int)amount || !fat12_is_critical_name((char*)file->name) ||
        node->size > FAT12_REPLICA_MAX_BYTES) return result;
    uint8_t* replica = (uint8_t*)malloc(node->size);
    size_t replica_length = 0U;
    if (!replica) return VFS_ERR_NO_MEMORY;
    bool recovered = fat12_read_critical_replica((char*)file->name, replica,
        node->size, &replica_length) && replica_length == node->size &&
        offset <= replica_length && amount <= replica_length - offset;
    if (recovered) memcpy(buffer, replica + offset, amount);
    free(replica);
    return recovered ? (int)amount : VFS_ERR_IO;
}

static int fat12_vfs_write(vfs_node_t* node, uint32_t offset, uint32_t size,
                           const uint8_t* buffer) {
    if (!node || !node->fs || (!buffer && size != 0) ||
        node->type != VFS_FILE || !node->fs_specific)
        return VFS_ERR_INVALID;
    if (size == 0) return 0;
    if (size > 0x7FFFFFFFU || offset > UINT32_MAX - size)
        return VFS_ERR_INVALID;
    fat12_file* handle = (fat12_file*)node->fs_specific;
    if (handle->attributes & FILE_ATTR_READONLY) return VFS_ERR_READ_ONLY;

    uint8_t entry_sector[FAT12_SECTOR_SIZE];
    if (handle->directory_slot >= FAT12_SECTOR_SIZE / 32U ||
        !fat12_read_logical_sectors(handle->directory_sector, 1,
                                    entry_sector)) return VFS_ERR_IO;
    directory_entry entry =
        ((directory_entry*)entry_sector)[handle->directory_slot];
    if (entry.filename[0] == 0x00 || entry.filename[0] == 0xE5 ||
        (entry.attributes & FILE_ATTR_DIRECTORY)) return VFS_ERR_NOT_FOUND;

    uint32_t fat_bytes = (uint32_t)fat12->boot_sector.sectors_per_fat *
                         FAT12_SECTOR_SIZE;
    uint8_t* original_fat = (uint8_t*)malloc(fat_bytes);
    if (!original_fat) return VFS_ERR_NO_MEMORY;
    memcpy(original_fat, fat12->fat, fat_bytes);

    uint32_t end = offset + size;
    uint32_t cluster_bytes = FAT12_SECTOR_SIZE *
        fat12->boot_sector.sectors_per_cluster;
    uint32_t required = (end + cluster_bytes - 1U) / cluster_bytes;
    uint16_t start = entry.first_cluster_low;
    bool valid_chain = false;
    uint32_t existing_clusters = fat12_chain_length(start, &valid_chain);
    if (!valid_chain) {
        free(original_fat);
        return VFS_ERR_IO;
    }
    if (required > existing_clusters &&
        required - existing_clusters > fat12_free_clusters()) {
        free(original_fat);
        return VFS_ERR_NO_SPACE;
    }
    bool ok = fat12_ensure_chain(&start, required);
    bool fat_changed = memcmp(original_fat, fat12->fat, fat_bytes) != 0;
    bool ordered_transaction = false;
    bool transaction_required = ok && fat12->journal_enabled;
    if (transaction_required) {
        uint32_t mutation_start = entry.file_size < offset
            ? entry.file_size : offset;
        uint32_t first_sector = mutation_start / FAT12_SECTOR_SIZE;
        uint32_t last_sector = (end - 1U) / FAT12_SECTOR_SIZE;
        uint32_t maximum_sectors = last_sector - first_sector + 1U + 1U;
        if (fat_changed)
            maximum_sectors += fat12->boot_sector.fat_count *
                               fat12->boot_sector.sectors_per_fat;
        if (fat12_is_critical_name((char*)handle->name))
            maximum_sectors += FAT12_REPLICA_SLOT_SECTORS;
        ordered_transaction = fat12_transaction_begin(maximum_sectors);
        ok = ordered_transaction;
    }
    if (ok && offset > entry.file_size) {
        ok = fat12_zero_range(start, entry.file_size,
                              offset - entry.file_size);
    }
    if (ok) ok = fat12_write_bytes(start, offset, buffer, size);
    if (ok && fat_changed) ok = fat12_sync_fat();
    if (ok) {
        entry.first_cluster_low = start;
        if (end > entry.file_size) entry.file_size = end;
        fat12_entry_location_t location;
        memset(&location, 0, sizeof(location));
        location.sector = handle->directory_sector;
        location.slot = handle->directory_slot;
        ok = fat12_write_entry(&location, &entry);
    }
    if (!ok) {
        if (ordered_transaction) fat12_transaction_fail();
        memcpy(fat12->fat, original_fat, fat_bytes);
        if (fat_changed && !transaction_required) (void)fat12_sync_fat();
        free(original_fat);
        return VFS_ERR_IO;
    }
    free(original_fat);
    node->inode = start;
    node->size = entry.file_size;
    handle->start_cluster = start;
    handle->size = entry.file_size;
    if (fat12_is_critical_name((char*)handle->name)) {
        if (handle->size == 0U || handle->size > FAT12_REPLICA_MAX_BYTES)
            return VFS_ERR_IO;
        uint8_t* replica = (uint8_t*)malloc(handle->size);
        if (!replica) return VFS_ERR_NO_MEMORY;
        uint32_t saved_position = handle->position;
        handle->position = 0U;
        int replicated = fat12_read_file(handle, replica, handle->size,
                                         handle->size);
        handle->position = saved_position;
        bool published = replicated == (int)handle->size &&
            fat12_publish_critical_replica((char*)handle->name, replica,
                                           handle->size);
        free(replica);
        if (!published) {
            if (ordered_transaction) fat12_transaction_fail();
            return VFS_ERR_IO;
        }
    }
    if (ordered_transaction && !fat12_transaction_commit()) return VFS_ERR_IO;
    return (int)size;
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
    fat12_entry_location_t location;
    int status = fat12_scan_directory((uint16_t)node->inode, name, &location);
    if (status != VFS_OK) return status;
    *child = fat12_make_node(node->fs, &location.entry, &location);
    return *child ? VFS_OK : VFS_ERR_NO_MEMORY;
}

static int fat12_vfs_mkdir(vfs_filesystem_t* fs, const char* path) {
    if (!fs || !path || fs != mounted_fat12_fs) return VFS_ERR_INVALID;
    uint16_t parent;
    uint8_t name[11];
    int status = fat12_resolve_parent(path, &parent, name);
    if (status != VFS_OK) return status;
    directory_entry probe;
    memset(&probe, 0, sizeof(probe));
    memcpy(probe.filename, name, 8);
    memcpy(probe.extension, name + 8, 3);
    char display[13];
    fat12_entry_name(&probe, display);
    fat12_entry_location_t existing;
    status = fat12_scan_directory(parent, display, &existing);
    if (status == VFS_OK) return VFS_ERR_EXISTS;
    if (status != VFS_ERR_NOT_FOUND) return status;

    uint32_t fat_bytes = (uint32_t)fat12->boot_sector.sectors_per_fat *
                         FAT12_SECTOR_SIZE;
    uint8_t* original_fat = (uint8_t*)malloc(fat_bytes);
    if (!original_fat) return VFS_ERR_NO_MEMORY;
    memcpy(original_fat, fat12->fat, fat_bytes);
    bool fat_changed = false;
    fat12_entry_location_t slot;
    status = fat12_find_free_slot(parent, &slot, &fat_changed);
    uint16_t cluster = status == VFS_OK ? fat12_allocate_cluster() : 0;
    if (cluster == 0) status = status == VFS_OK ? VFS_ERR_NO_SPACE : status;
    if (status == VFS_OK) {
        fat_changed = true;
        uint8_t sector[FAT12_SECTOR_SIZE];
        memset(sector, 0, sizeof(sector));
        directory_entry* dots = (directory_entry*)sector;
        memset(dots[0].filename, ' ', 8);
        memset(dots[0].extension, ' ', 3);
        dots[0].filename[0] = '.';
        dots[0].attributes = FILE_ATTR_DIRECTORY;
        dots[0].first_cluster_low = cluster;
        memset(dots[1].filename, ' ', 8);
        memset(dots[1].extension, ' ', 3);
        dots[1].filename[0] = '.';
        dots[1].filename[1] = '.';
        dots[1].attributes = FILE_ATTR_DIRECTORY;
        dots[1].first_cluster_low = parent;
        if (!fat12_write_logical_sectors(fat12_cluster_sector(cluster), 1,
                                         sector)) status = VFS_ERR_IO;
    }
    if (status == VFS_OK && !fat12_sync_fat()) status = VFS_ERR_IO;
    if (status == VFS_OK) {
        directory_entry entry;
        memset(&entry, 0, sizeof(entry));
        memcpy(entry.filename, name, 8);
        memcpy(entry.extension, name + 8, 3);
        entry.attributes = FILE_ATTR_DIRECTORY;
        entry.first_cluster_low = cluster;
        if (!fat12_write_entry(&slot, &entry)) status = VFS_ERR_IO;
    }
    if (status != VFS_OK) {
        memcpy(fat12->fat, original_fat, fat_bytes);
        if (fat_changed) (void)fat12_sync_fat();
    }
    free(original_fat);
    return status;
}

static int fat12_vfs_rmdir(vfs_filesystem_t* fs, const char* path) {
    if (!fs || !path || fs != mounted_fat12_fs) return VFS_ERR_INVALID;
    fat12_entry_location_t location;
    int status = fat12_resolve_location(path, &location);
    if (status != VFS_OK) return status;
    char target_name[13];
    fat12_entry_name(&location.entry, target_name);
    if (strcmp(target_name, ".") == 0 || strcmp(target_name, "..") == 0)
        return VFS_ERR_INVALID;
    if (!(location.entry.attributes & FILE_ATTR_DIRECTORY))
        return VFS_ERR_NOT_DIR;
    if (!fat12_directory_is_empty(location.entry.first_cluster_low))
        return VFS_ERR_BUSY;
    uint32_t fat_bytes = (uint32_t)fat12->boot_sector.sectors_per_fat *
                         FAT12_SECTOR_SIZE;
    uint8_t* original_fat = (uint8_t*)malloc(fat_bytes);
    if (!original_fat) return VFS_ERR_NO_MEMORY;
    memcpy(original_fat, fat12->fat, fat_bytes);
    if (!fat12_free_chain(location.entry.first_cluster_low) ||
        !fat12_sync_fat()) status = VFS_ERR_IO;
    if (status == VFS_OK) {
        location.entry.filename[0] = 0xE5;
        if (!fat12_write_entry(&location, &location.entry))
            status = VFS_ERR_IO;
    }
    if (status != VFS_OK) {
        memcpy(fat12->fat, original_fat, fat_bytes);
        (void)fat12_sync_fat();
    }
    free(original_fat);
    return status;
}

static int fat12_vfs_create(vfs_filesystem_t* fs, const char* path) {
    if (!fs || !path || fs != mounted_fat12_fs) return VFS_ERR_INVALID;
    uint16_t parent;
    uint8_t name[11];
    int status = fat12_resolve_parent(path, &parent, name);
    if (status != VFS_OK) return status;
    directory_entry entry;
    memset(&entry, 0, sizeof(entry));
    memcpy(entry.filename, name, 8);
    memcpy(entry.extension, name + 8, 3);
    char display[13];
    fat12_entry_name(&entry, display);
    fat12_entry_location_t existing;
    status = fat12_scan_directory(parent, display, &existing);
    if (status == VFS_OK) return VFS_ERR_EXISTS;
    if (status != VFS_ERR_NOT_FOUND) return status;

    uint32_t fat_bytes = (uint32_t)fat12->boot_sector.sectors_per_fat *
                         FAT12_SECTOR_SIZE;
    uint8_t* original_fat = (uint8_t*)malloc(fat_bytes);
    if (!original_fat) return VFS_ERR_NO_MEMORY;
    memcpy(original_fat, fat12->fat, fat_bytes);
    bool fat_changed = false;
    fat12_entry_location_t slot;
    status = fat12_find_free_slot(parent, &slot, &fat_changed);
    if (status == VFS_OK && fat_changed && !fat12_sync_fat())
        status = VFS_ERR_IO;
    if (status == VFS_OK) {
        entry.attributes = FILE_ATTR_ARCHIVE;
        if (!fat12_write_entry(&slot, &entry)) status = VFS_ERR_IO;
    }
    if (status != VFS_OK) {
        memcpy(fat12->fat, original_fat, fat_bytes);
        if (fat_changed) (void)fat12_sync_fat();
    }
    free(original_fat);
    return status;
}

static int fat12_vfs_delete(vfs_filesystem_t* fs, const char* path) {
    if (!fs || !path || fs != mounted_fat12_fs) return VFS_ERR_INVALID;
    fat12_entry_location_t location;
    int status = fat12_resolve_location(path, &location);
    if (status != VFS_OK) return status;
    if (location.entry.attributes & FILE_ATTR_DIRECTORY) return VFS_ERR_IS_DIR;
    if (location.entry.attributes & FILE_ATTR_READONLY)
        return VFS_ERR_READ_ONLY;
    uint32_t fat_bytes = (uint32_t)fat12->boot_sector.sectors_per_fat *
                         FAT12_SECTOR_SIZE;
    uint8_t* original_fat = (uint8_t*)malloc(fat_bytes);
    if (!original_fat) return VFS_ERR_NO_MEMORY;
    memcpy(original_fat, fat12->fat, fat_bytes);
    bool changed = location.entry.first_cluster_low != 0;
    if (changed && (!fat12_free_chain(location.entry.first_cluster_low) ||
                    !fat12_sync_fat())) status = VFS_ERR_IO;
    if (status == VFS_OK) {
        location.entry.filename[0] = 0xE5;
        if (!fat12_write_entry(&location, &location.entry))
            status = VFS_ERR_IO;
    }
    if (status != VFS_OK) {
        memcpy(fat12->fat, original_fat, fat_bytes);
        if (changed) (void)fat12_sync_fat();
    }
    free(original_fat);
    return status;
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
