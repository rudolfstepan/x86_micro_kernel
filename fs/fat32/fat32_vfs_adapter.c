/**
 * @file fs/fat32/fat32_vfs_adapter.c
 * @brief VFS-Adapter für FAT32.
 *
 * Layer: Ring-0 virtual filesystem and on-disk format.
 * Contract: On-Disk-Werte, Pfade und Ressourcenbereiche werden vor I/O validiert.
 * Safety: Übersetzt VFS-Operationen ohne FAT32-Grenzen oder Schreibfences zu umgehen.
 */
#include "fs/vfs/vfs.h"
#include "fat32.h"
#include "lib/libc/stdio.h"
#include "lib/libc/string.h"
#include "lib/libc/stdlib.h"
#include "drivers/bus/drives.h"
#include "drivers/block/ata.h"

// ===========================================================================
// FAT32 VFS Adapter
// Wraps existing FAT32 implementation to work with VFS layer
// ===========================================================================

// Forward declarations of existing FAT32 functions
extern int fat32_init_fs(unsigned short base, bool is_master);
extern struct fat32_boot_sector boot_sector;
extern unsigned int current_directory_cluster;
extern struct fat32_dir_entry* find_file_in_directory(const char* filename);
extern unsigned int read_file_data(unsigned int start_cluster, char* buffer, unsigned int buffer_size, unsigned int bytes_to_read);
extern bool fat32_read_dir(const char* path);
extern struct fat32_fsinfo fsinfo;
extern bool fsinfo_valid;
extern unsigned short ata_base_address;
extern bool ata_is_master;
extern unsigned int partition_lba_offset;
extern drive_t* current_drive;

typedef struct {
    struct fat32_boot_sector boot;
    struct fat32_fsinfo fsinfo;
    bool fsinfo_valid;
    uint32_t current_directory_cluster;
    uint32_t partition_lba;
    uint16_t ata_base;
    bool ata_master;
} fat32_vfs_context_t;

#define FAT32_CONTEXT_REGISTRY_SIZE 10
static fat32_vfs_context_t*
    fat32_context_registry[FAT32_CONTEXT_REGISTRY_SIZE];

static void fat32_sync_registered_contexts(void) {
    for (unsigned int i = 0; i < FAT32_CONTEXT_REGISTRY_SIZE; ++i) {
        fat32_vfs_context_t* context = fat32_context_registry[i];
        if (context && context->ata_base == ata_base_address &&
            context->ata_master == ata_is_master &&
            context->partition_lba == partition_lba_offset) {
            context->boot = boot_sector;
            context->fsinfo = fsinfo;
            context->fsinfo_valid = fsinfo_valid;
        }
    }
}

static bool fat32_register_context(fat32_vfs_context_t* context) {
    for (unsigned int i = 0; i < FAT32_CONTEXT_REGISTRY_SIZE; ++i) {
        if (!fat32_context_registry[i]) {
            fat32_context_registry[i] = context;
            fat32_context_sync_hook = fat32_sync_registered_contexts;
            return true;
        }
    }
    return false;
}

static void fat32_unregister_context(fat32_vfs_context_t* context) {
    bool any = false;
    for (unsigned int i = 0; i < FAT32_CONTEXT_REGISTRY_SIZE; ++i) {
        if (fat32_context_registry[i] == context) {
            fat32_context_registry[i] = NULL;
        }
        if (fat32_context_registry[i]) any = true;
    }
    if (!any && fat32_context_sync_hook == fat32_sync_registered_contexts) {
        fat32_context_sync_hook = NULL;
    }
}

typedef struct {
    struct fat32_dir_entry entry;
    uint32_t parent_cluster;
} fat32_vfs_handle_t;

static void fat32_activate(vfs_filesystem_t* fs) {
    fat32_vfs_context_t* context = (fat32_vfs_context_t*)fs->fs_data;
    boot_sector = context->boot;
    fsinfo = context->fsinfo;
    fsinfo_valid = context->fsinfo_valid;
    current_directory_cluster = context->current_directory_cluster;
    partition_lba_offset = context->partition_lba;
    ata_base_address = context->ata_base;
    ata_is_master = context->ata_master;
    current_drive = fs->drive;
}

static void fat32_sync(vfs_filesystem_t* fs) {
    fat32_vfs_context_t* context = (fat32_vfs_context_t*)fs->fs_data;
    context->boot = boot_sector;
    context->fsinfo = fsinfo;
    context->fsinfo_valid = fsinfo_valid;
    context->current_directory_cluster = current_directory_cluster;
    context->partition_lba = partition_lba_offset;
    context->ata_base = ata_base_address;
    context->ata_master = ata_is_master;
}

static bool fat32_bpb_candidate(const uint8_t sector[SECTOR_SIZE]) {
    const struct fat32_boot_sector* candidate =
        (const struct fat32_boot_sector*)sector;
    return candidate->boot_sector_signature == 0xAA55 &&
           candidate->bytes_per_sector == SECTOR_SIZE &&
           candidate->sectors_per_cluster != 0 &&
           candidate->reserved_sector_count != 0 &&
           candidate->fat_size_16 == 0 && candidate->fat_size_32 != 0 &&
           candidate->root_entry_count == 0 &&
           candidate->root_cluster >= 2;
}

static bool fat32_find_volume(drive_t* drive, uint32_t* partition_lba) {
    uint8_t sector[SECTOR_SIZE];
    if (!ata_read_sector(drive->base, 0, sector, drive->is_master)) {
        return false;
    }
    if (fat32_bpb_candidate(sector) &&
        (drive->sectors == 0 ||
         ((const struct fat32_boot_sector*)sector)->total_sectors_32 <=
            drive->sectors)) {
        *partition_lba = 0;
        return true;
    }
    if (sector[510] != 0x55 || sector[511] != 0xAA) {
        return false;
    }

    for (uint32_t i = 0; i < 4; i++) {
        const uint8_t* part = sector + 446 + i * 16;
        uint8_t type = part[4];
        if (type != 0x0B && type != 0x0C && type != 0x1B && type != 0x1C) {
            continue;
        }
        uint32_t lba = (uint32_t)part[8] |
                       ((uint32_t)part[9] << 8) |
                       ((uint32_t)part[10] << 16) |
                       ((uint32_t)part[11] << 24);
        uint32_t partition_sectors = (uint32_t)part[12] |
                       ((uint32_t)part[13] << 8) |
                       ((uint32_t)part[14] << 16) |
                       ((uint32_t)part[15] << 24);
        if (lba != 0 && partition_sectors != 0 &&
            (uint64_t)lba + partition_sectors <=
                (uint64_t)UINT32_MAX + 1u &&
            (drive->sectors == 0 ||
             (uint64_t)lba + partition_sectors <= drive->sectors) &&
            ata_read_sector(drive->base, lba, sector, drive->is_master) &&
            fat32_bpb_candidate(sector) &&
            ((const struct fat32_boot_sector*)sector)->total_sectors_32 <=
                partition_sectors) {
            *partition_lba = lba;
            return true;
        }
    }
    return false;
}

static int fat32_resolve_entry(vfs_filesystem_t* fs, const char* path,
                               struct fat32_dir_entry* result,
                               uint32_t* parent_cluster) {
    if (!fs || !path || !result || !parent_cluster || strlen(path) >= MAX_PATH_LENGTH) {
        return VFS_ERR_INVALID;
    }

    char copy[MAX_PATH_LENGTH];
    strcpy(copy, path);
    char* cursor = copy;
    while (*cursor == '/') cursor++;
    if (*cursor == '\0') return VFS_ERR_INVALID;

    uint32_t directory = ((fat32_vfs_context_t*)fs->fs_data)->boot.root_cluster;
    char* save = NULL;
    char* token = strtok_r(cursor, "/", &save);
    while (token) {
        char* next_token = strtok_r(NULL, "/", &save);
        struct fat32_dir_entry found;
        fat32_lookup_result_t lookup = fat32_lookup_entry_in_directory(
            directory, token, &found);
        if (lookup == FAT32_LOOKUP_NOT_FOUND) return VFS_ERR_NOT_FOUND;
        if (lookup != FAT32_LOOKUP_FOUND) return VFS_ERR_IO;
        if (!next_token) {
            *result = found;
            *parent_cluster = directory;
            return VFS_OK;
        }
        if (!(found.attr & ATTR_DIRECTORY)) return VFS_ERR_NOT_DIR;
        directory = read_start_cluster(&found);
        token = next_token;
    }
    return VFS_ERR_NOT_FOUND;
}

static bool fat32_resolve_parent(vfs_filesystem_t* fs, const char* path,
                                 uint32_t* parent_cluster, char leaf[13]) {
    if (!fs || !path || !parent_cluster || !leaf || strlen(path) >= MAX_PATH_LENGTH) {
        return false;
    }
    char copy[MAX_PATH_LENGTH];
    strcpy(copy, path);
    char* cursor = copy;
    while (*cursor == '/') cursor++;
    if (*cursor == '\0') return false;

    uint32_t directory = ((fat32_vfs_context_t*)fs->fs_data)->boot.root_cluster;
    char* save = NULL;
    char* token = strtok_r(cursor, "/", &save);
    while (token) {
        char* next_token = strtok_r(NULL, "/", &save);
        if (!next_token) {
            if (!fat32_is_valid_short_name(token) ||
                strcmp(token, ".") == 0 || strcmp(token, "..") == 0)
                return false;
            strcpy(leaf, token);
            *parent_cluster = directory;
            return true;
        }
        struct fat32_dir_entry* found =
            find_file_in_directory_cluster(directory, token);
        if (!found || !(found->attr & ATTR_DIRECTORY)) {
            if (found) free(found);
            return false;
        }
        directory = read_start_cluster(found);
        free(found);
        token = next_token;
    }
    return false;
}

// ===========================================================================
// Helper Functions
// ===========================================================================

static char fat32_display_character(char value, bool lowercase) {
    return lowercase && value >= 'A' && value <= 'Z'
        ? (char)(value + ('a' - 'A')) : value;
}

static void fat32_entry_to_vfs_entry(struct fat32_dir_entry* fat_entry, vfs_dir_entry_t* vfs_entry) {
    if (!fat_entry || !vfs_entry) return;
    
    // Copy name (convert from 8.3 format)
    char name[13];
    int name_idx = 0;
    
    // Copy filename (skip trailing spaces)
    int i;
    for (i = 0; i < 8 && fat_entry->name[i] != ' '; i++) {
        name[name_idx++] = fat32_display_character(
            fat_entry->name[i], (fat_entry->nt_res & 0x08U) != 0U);
    }
    
    // Add extension if present
    if (fat_entry->name[8] != ' ') {
        name[name_idx++] = '.';
        for (i = 8; i < 11 && fat_entry->name[i] != ' '; i++) {
            name[name_idx++] = fat32_display_character(
                fat_entry->name[i], (fat_entry->nt_res & 0x10U) != 0U);
        }
    }
    name[name_idx] = '\0';
    
    strcpy(vfs_entry->name, name);
    
    // Set type
    if (fat_entry->attr & 0x10) {
        vfs_entry->type = VFS_DIRECTORY;
    } else {
        vfs_entry->type = VFS_FILE;
    }
    
    // Copy other fields
    vfs_entry->size = fat_entry->file_size;
    vfs_entry->inode = ((uint32_t)fat_entry->first_cluster_high << 16) | fat_entry->first_cluster_low;
    vfs_entry->attributes = fat_entry->attr;
    
    // Convert FAT times to Unix-like timestamps (simplified)
    vfs_entry->create_time = 0;  // TODO: Convert FAT date/time
    vfs_entry->modify_time = 0;
    vfs_entry->access_time = 0;
}

static vfs_node_t* fat32_make_node(vfs_filesystem_t* fs,
                                   const struct fat32_dir_entry* entry,
                                   uint32_t parent_cluster) {
    vfs_node_t* node = (vfs_node_t*)malloc(sizeof(vfs_node_t));
    fat32_vfs_handle_t* handle =
        (fat32_vfs_handle_t*)malloc(sizeof(fat32_vfs_handle_t));
    if (!node || !handle) {
        if (node) free(node);
        if (handle) free(handle);
        return NULL;
    }

    memset(node, 0, sizeof(*node));
    handle->entry = *entry;
    handle->parent_cluster = parent_cluster;

    vfs_dir_entry_t converted;
    memset(&converted, 0, sizeof(converted));
    fat32_entry_to_vfs_entry(&handle->entry, &converted);
    strcpy(node->name, converted.name);
    node->type = converted.type;
    node->inode = converted.inode;
    node->size = converted.size;
    node->fs = fs;
    node->fs_specific = handle;
    return node;
}

static void fat32_flush_context(vfs_filesystem_t* fs) {
    if (fsinfo_valid) {
        (void)write_fsinfo();
    }
    fat32_sync(fs);
}

static bool fat32_commit_node_data(vfs_node_t* node, uint32_t start_cluster,
                                   uint32_t file_size,
                                   uint32_t original_tail,
                                   bool chain_reclaim_safe) {
    fat32_vfs_handle_t* handle = (fat32_vfs_handle_t*)node->fs_specific;
    const struct fat32_dir_entry original = handle->entry;
    struct fat32_dir_entry committed = original;
    committed.first_cluster_high = (uint16_t)(start_cluster >> 16);
    committed.first_cluster_low = (uint16_t)start_cluster;
    committed.file_size = file_size;
    set_fat32_time(&committed.write_time, &committed.write_date);
    if (!update_directory_entry(handle->parent_cluster, committed.name,
                                &committed)) {
        char filename[13];
        struct fat32_dir_entry observed;
        format_filename(filename, committed.name);
        if (fat32_lookup_entry_in_directory(handle->parent_cluster, filename,
                                            &observed) !=
                FAT32_LOOKUP_FOUND) {
            return false;
        }
        if (memcmp(&observed, &committed, sizeof(observed)) == 0) {
            committed = observed;
        } else if (memcmp(&observed, &original, sizeof(observed)) == 0) {
            uint32_t old_cluster = read_start_cluster(
                (struct fat32_dir_entry*)&original);
            if (chain_reclaim_safe) {
                if (is_valid_cluster(&boot_sector, old_cluster)) {
                    (void)fat32_reclaim_chain_suffix(&boot_sector,
                                                     original_tail);
                } else if (is_valid_cluster(&boot_sector, start_cluster)) {
                    (void)free_cluster_chain(&boot_sector, start_cluster);
                }
                fat32_flush_context(node->fs);
            }
            return false;
        } else {
            return false;
        }
    }
    handle->entry = committed;
    node->inode = start_cluster;
    node->size = file_size;
    fat32_flush_context(node->fs);
    return true;
}

static int fat32_refresh_file_node(vfs_node_t* node) {
    if (!node || !node->fs_specific) return VFS_ERR_INVALID;
    fat32_vfs_handle_t* handle = (fat32_vfs_handle_t*)node->fs_specific;
    char filename[13];
    format_filename(filename, handle->entry.name);
    struct fat32_dir_entry current;
    fat32_lookup_result_t result = fat32_lookup_entry_in_directory(
        handle->parent_cluster, filename, &current);
    if (result == FAT32_LOOKUP_NOT_FOUND) return VFS_ERR_NOT_FOUND;
    if (result != FAT32_LOOKUP_FOUND || (current.attr & ATTR_DIRECTORY)) {
        return VFS_ERR_IO;
    }
    handle->entry = current;
    node->inode = read_start_cluster(&current);
    node->size = current.file_size;
    return VFS_OK;
}

// ===========================================================================
// VFS Operations Implementation
// ===========================================================================

static int fat32_vfs_mount_unlocked(vfs_filesystem_t* fs, drive_t* drive) {
    if (!fs || !drive) {
        return VFS_ERR_INVALID;
    }
    
    uint32_t volume_lba;
    if (!fat32_find_volume(drive, &volume_lba)) {
        return VFS_ERR_INVALID;
    }

    current_drive = drive;
    int result = fat32_init_fs_at(drive->base, drive->is_master, volume_lba);
    if (result != SUCCESS) {
        return VFS_ERR_INVALID;
    }

    fat32_vfs_context_t* context =
        (fat32_vfs_context_t*)malloc(sizeof(fat32_vfs_context_t));
    if (!context) {
        return VFS_ERR_NO_MEMORY;
    }
    context->boot = boot_sector;
    context->fsinfo = fsinfo;
    context->fsinfo_valid = fsinfo_valid;
    context->current_directory_cluster = boot_sector.root_cluster;
    context->partition_lba = partition_lba_offset;
    context->ata_base = ata_base_address;
    context->ata_master = ata_is_master;
    fs->fs_data = context;
    
    // Create root node
    vfs_node_t* root = (vfs_node_t*)malloc(sizeof(vfs_node_t));
    if (!root) {
        free(context);
        fs->fs_data = NULL;
        return VFS_ERR_NO_MEMORY;
    }

    memset(root, 0, sizeof(*root));
    strcpy(root->name, "/");
    root->type = VFS_DIRECTORY;
    root->inode = boot_sector.root_cluster;
    root->size = 0;
    root->flags = 0;
    root->fs = fs;
    root->fs_specific = NULL;
    
    fs->root = root;
    if (!fat32_register_context(context)) {
        free(root);
        free(context);
        fs->root = NULL;
        fs->fs_data = NULL;
        return VFS_ERR_NO_MEMORY;
    }
    
    return VFS_OK;
}

static int fat32_vfs_unmount_unlocked(vfs_filesystem_t* fs) {
    if (!fs) {
        return VFS_ERR_INVALID;
    }
    
    if (fs->fs_data) {
        fat32_unregister_context((fat32_vfs_context_t*)fs->fs_data);
        free(fs->fs_data);
        fs->fs_data = NULL;
    }
    
    if (fs->root) {
        free(fs->root);
        fs->root = NULL;
    }
    
    return VFS_OK;
}

static int fat32_vfs_open_unlocked(vfs_filesystem_t* fs, const char* path,
                                   vfs_node_t** node) {
    if (!fs || !path || !node) {
        return VFS_ERR_INVALID;
    }
    
    *node = NULL;
    fat32_activate(fs);
    
    // Handle root directory
    if (strcmp(path, "/") == 0) {
        *node = fs->root;
        return VFS_OK;
    }
    
    struct fat32_dir_entry fat_entry;
    uint32_t parent_cluster;
    int resolve_result = fat32_resolve_entry(fs, path, &fat_entry,
                                             &parent_cluster);
    if (resolve_result != VFS_OK) return resolve_result;

    vfs_node_t* new_node = fat32_make_node(fs, &fat_entry, parent_cluster);
    if (!new_node) {
        return VFS_ERR_NO_MEMORY;
    }

    *node = new_node;
    return VFS_OK;
}

static int fat32_vfs_close_unlocked(vfs_node_t* node) {
    if (!node) {
        return VFS_ERR_INVALID;
    }
    
    // Don't free root node
    if (node == node->fs->root) {
        return VFS_OK;
    }
    
    if (node->fs_specific) {
        free(node->fs_specific);
    }
    free(node);
    
    return VFS_OK;
}

static int fat32_vfs_read_unlocked(vfs_node_t* node, uint32_t offset,
                                   uint32_t size, uint8_t* buffer) {
    if (!node || !buffer) {
        return VFS_ERR_INVALID;
    }
    
    if (node->type != VFS_FILE) {
        return VFS_ERR_IS_DIR;
    }
    
    fat32_activate(node->fs);
    int refresh = fat32_refresh_file_node(node);
    if (refresh != VFS_OK) return refresh;
    if (offset >= node->size || size == 0) return 0;
    uint32_t available = node->size - offset;
    uint32_t bytes_to_read = size < available ? size : available;
    if (bytes_to_read > INT_MAX) bytes_to_read = INT_MAX;
    return (int)read_file_data_at(node->inode, offset, (char*)buffer,
                                  size, bytes_to_read);
}

static int fat32_vfs_write_unlocked(vfs_node_t* node, uint32_t offset,
                                    uint32_t size,
                                    const uint8_t* buffer) {
    if (!node || (!buffer && size != 0) || !node->fs) return VFS_ERR_INVALID;
    if (node->type != VFS_FILE) return VFS_ERR_IS_DIR;
    if (size == 0) return 0;
    if (size > INT_MAX) return VFS_ERR_INVALID;
    if (offset > UINT32_MAX - size || !node->fs_specific) return VFS_ERR_INVALID;

    fat32_activate(node->fs);
    int refresh = fat32_refresh_file_node(node);
    if (refresh != VFS_OK) return refresh;
    fat32_vfs_handle_t* handle = (fat32_vfs_handle_t*)node->fs_specific;
    if (handle->entry.attr & ATTR_READ_ONLY) return VFS_ERR_READ_ONLY;
    uint32_t start_cluster = node->inode;
    uint32_t durable_size = node->size;
    uint32_t original_tail = INVALID_CLUSTER;
    if (is_valid_cluster(&boot_sector, node->inode) &&
        !fat32_get_chain_tail(&boot_sector, node->inode, &original_tail)) {
        return VFS_ERR_IO;
    }
    bool chain_reclaim_safe = true;

    if (offset > node->size) {
        uint8_t zeroes[SECTOR_SIZE];
        memset(zeroes, 0, sizeof(zeroes));
        uint32_t position = node->size;
        while (position < offset) {
            uint32_t amount = offset - position;
            if (amount > sizeof(zeroes)) amount = sizeof(zeroes);
            int written = write_file_data_at_checked(
                &start_cluster, position, zeroes, amount,
                &chain_reclaim_safe);
            if (written > 0) {
                position += (uint32_t)written;
                durable_size = position;
            }
            if (!chain_reclaim_safe) return VFS_ERR_IO;
            if (written != (int)amount) {
                if (written < 0 && durable_size == node->size) {
                    if (is_valid_cluster(&boot_sector, node->inode)) {
                        (void)fat32_reclaim_chain_suffix(&boot_sector,
                                                         original_tail);
                    } else if (is_valid_cluster(&boot_sector,
                                                start_cluster)) {
                        (void)free_cluster_chain(&boot_sector,
                                                 start_cluster);
                    }
                    fat32_flush_context(node->fs);
                    return VFS_ERR_IO;
                }
                if ((durable_size != node->size || start_cluster != node->inode) &&
                    !fat32_commit_node_data(node, start_cluster, durable_size,
                                            original_tail,
                                            chain_reclaim_safe)) {
                    return VFS_ERR_IO;
                }
                return VFS_ERR_IO;
            }
        }
        durable_size = offset;
    }

    int written = write_file_data_at_checked(&start_cluster, offset, buffer,
                                             size, &chain_reclaim_safe);
    if (!chain_reclaim_safe) return VFS_ERR_IO;
    if (written < 0) {
        if (durable_size == node->size) {
            if (is_valid_cluster(&boot_sector, node->inode)) {
                (void)fat32_reclaim_chain_suffix(&boot_sector,
                                                 original_tail);
            } else if (is_valid_cluster(&boot_sector, start_cluster)) {
                (void)free_cluster_chain(&boot_sector, start_cluster);
            }
            fat32_flush_context(node->fs);
            return VFS_ERR_IO;
        }
        if ((durable_size != node->size || start_cluster != node->inode) &&
            !fat32_commit_node_data(node, start_cluster, durable_size,
                                    original_tail, chain_reclaim_safe)) {
            return VFS_ERR_IO;
        }
        return VFS_ERR_IO;
    }

    uint32_t end = offset + (uint32_t)written;
    if (end < durable_size) end = durable_size;
    if (!fat32_commit_node_data(node, start_cluster, end, original_tail,
                                chain_reclaim_safe)) {
        return VFS_ERR_IO;
    }
    return written;
}

static int fat32_vfs_readdir_batch_unlocked(vfs_node_t* node, uint32_t index,
                                            vfs_dir_entry_t* entries_out,
                                            uint32_t capacity) {
    if (!node || !entries_out || capacity == 0) {
        return VFS_ERR_INVALID;
    }
    
    if (node->type != VFS_DIRECTORY) {
        return VFS_ERR_NOT_DIR;
    }
    
    fat32_activate(node->fs);
    uint32_t cluster = node->inode;
    uint32_t visible_index = 0;
    uint32_t count = 0;
    uint32_t clusters_left = get_total_clusters(&boot_sector);
    uint8_t sector_buffer[SECTOR_SIZE];

    while (is_valid_cluster(&boot_sector, cluster) && clusters_left-- != 0) {
        uint32_t sector = cluster_to_sector(&boot_sector, cluster);
        if (sector == INVALID_CLUSTER) return VFS_ERR_IO;
        for (uint32_t s = 0; s < boot_sector.sectors_per_cluster; s++) {
            if (!ata_read_sector(ata_base_address, sector + s, sector_buffer,
                                 ata_is_master)) {
                return VFS_ERR_IO;
            }
            struct fat32_dir_entry* entries =
                (struct fat32_dir_entry*)sector_buffer;
            for (uint32_t i = 0; i < SECTOR_SIZE / sizeof(*entries); i++) {
                if (entries[i].name[0] == 0x00) return (int)count;
                if (entries[i].name[0] == 0xE5 || entries[i].attr == 0x0F ||
                    (entries[i].attr & 0x08)) {
                    continue;
                }
                if (visible_index++ < index) continue;
                memset(&entries_out[count], 0, sizeof(entries_out[count]));
                fat32_entry_to_vfs_entry(&entries[i], &entries_out[count]);
                if (++count == capacity) return (int)count;
            }
        }

        uint32_t next = get_next_cluster_in_chain(&boot_sector, cluster);
        if (is_end_of_cluster_chain(next)) return (int)count;
        if (!is_valid_cluster(&boot_sector, next)) return VFS_ERR_IO;
        cluster = next;
    }
    return VFS_ERR_IO;
}

static int fat32_vfs_readdir_unlocked(vfs_node_t* node, uint32_t index,
                                      vfs_dir_entry_t* entry) {
    int count = fat32_vfs_readdir_batch_unlocked(node, index, entry, 1);
    if (count < 0) return count;
    return count == 1 ? VFS_OK : VFS_ERR_NOT_FOUND;
}

static int fat32_vfs_finddir_unlocked(vfs_node_t* node, const char* name,
                                      vfs_node_t** child) {
    if (!node || !name || !child || !node->fs) return VFS_ERR_INVALID;
    if (node->type != VFS_DIRECTORY) return VFS_ERR_NOT_DIR;
    *child = NULL;
    fat32_activate(node->fs);
    struct fat32_dir_entry* entry =
        find_file_in_directory_cluster(node->inode, name);
    if (!entry) return VFS_ERR_NOT_FOUND;
    *child = fat32_make_node(node->fs, entry, node->inode);
    free(entry);
    return *child ? VFS_OK : VFS_ERR_NO_MEMORY;
}

static int fat32_vfs_mkdir_unlocked(vfs_filesystem_t* fs, const char* path) {
    if (!fs || !path) return VFS_ERR_INVALID;
    fat32_activate(fs);
    uint32_t parent;
    char leaf[13];
    if (!fat32_resolve_parent(fs, path, &parent, leaf)) return VFS_ERR_INVALID;
    struct fat32_dir_entry* existing =
        find_file_in_directory_cluster(parent, leaf);
    if (existing) {
        free(existing);
        return VFS_ERR_EXISTS;
    }
    current_directory_cluster = parent;
    if (!fat32_create_dir(leaf)) return VFS_ERR_IO;
    fat32_flush_context(fs);
    return VFS_OK;
}

static int fat32_vfs_rmdir_unlocked(vfs_filesystem_t* fs, const char* path) {
    if (!fs || !path) return VFS_ERR_INVALID;
    fat32_activate(fs);
    uint32_t parent;
    char leaf[13];
    if (!fat32_resolve_parent(fs, path, &parent, leaf)) return VFS_ERR_INVALID;
    struct fat32_dir_entry* existing =
        find_file_in_directory_cluster(parent, leaf);
    if (!existing) return VFS_ERR_NOT_FOUND;
    bool is_dir = (existing->attr & ATTR_DIRECTORY) != 0;
    bool read_only = (existing->attr & ATTR_READ_ONLY) != 0;
    free(existing);
    if (!is_dir) return VFS_ERR_NOT_DIR;
    if (read_only) return VFS_ERR_READ_ONLY;
    current_directory_cluster = parent;
    if (!fat32_delete_dir(leaf)) return VFS_ERR_IO;
    fat32_flush_context(fs);
    return VFS_OK;
}

static int fat32_vfs_create_unlocked(vfs_filesystem_t* fs, const char* path) {
    if (!fs || !path) return VFS_ERR_INVALID;
    fat32_activate(fs);
    uint32_t parent;
    char leaf[13];
    if (!fat32_resolve_parent(fs, path, &parent, leaf)) return VFS_ERR_INVALID;
    struct fat32_dir_entry* existing =
        find_file_in_directory_cluster(parent, leaf);
    if (existing) {
        free(existing);
        return VFS_ERR_EXISTS;
    }
    current_directory_cluster = parent;
    if (!fat32_create_file(leaf)) return VFS_ERR_IO;
    fat32_flush_context(fs);
    return VFS_OK;
}

static int fat32_vfs_delete_unlocked(vfs_filesystem_t* fs, const char* path) {
    if (!fs || !path) return VFS_ERR_INVALID;
    fat32_activate(fs);
    uint32_t parent;
    char leaf[13];
    if (!fat32_resolve_parent(fs, path, &parent, leaf)) return VFS_ERR_INVALID;
    struct fat32_dir_entry* existing =
        find_file_in_directory_cluster(parent, leaf);
    if (!existing) return VFS_ERR_NOT_FOUND;
    bool is_dir = (existing->attr & ATTR_DIRECTORY) != 0;
    bool read_only = (existing->attr & ATTR_READ_ONLY) != 0;
    free(existing);
    if (is_dir) return VFS_ERR_IS_DIR;
    if (read_only) return VFS_ERR_READ_ONLY;
    current_directory_cluster = parent;
    if (!fat32_delete_file(leaf)) return VFS_ERR_IO;
    fat32_flush_context(fs);
    return VFS_OK;
}

static int fat32_vfs_rename_unlocked(vfs_filesystem_t* fs,
                                     const char* old_path,
                                     const char* new_path) {
    if (!fs || !old_path || !new_path) return VFS_ERR_INVALID;
    fat32_activate(fs);

    uint32_t old_parent;
    uint32_t new_parent;
    char old_leaf[13];
    char new_leaf[13];
    if (!fat32_resolve_parent(fs, old_path, &old_parent, old_leaf) ||
        !fat32_resolve_parent(fs, new_path, &new_parent, new_leaf)) {
        return VFS_ERR_INVALID;
    }
    if (old_parent != new_parent) return VFS_ERR_UNSUPPORTED;
    if (!fat32_is_valid_short_name(old_leaf) ||
        !fat32_is_valid_short_name(new_leaf)) return VFS_ERR_INVALID;
    unsigned char old_name[11];
    unsigned char new_name[11];
    convert_to_83_format(old_name, old_leaf);
    convert_to_83_format(new_name, new_leaf);
    if (memcmp(old_name, new_name, sizeof(old_name)) == 0) return VFS_OK;

    struct fat32_dir_entry source;
    fat32_lookup_result_t source_result = fat32_lookup_entry_in_directory(
        old_parent, old_leaf, &source);
    if (source_result == FAT32_LOOKUP_NOT_FOUND) return VFS_ERR_NOT_FOUND;
    if (source_result != FAT32_LOOKUP_FOUND) return VFS_ERR_IO;
    if (source.attr & ATTR_DIRECTORY) return VFS_ERR_IS_DIR;
    if (source.attr & ATTR_READ_ONLY) return VFS_ERR_READ_ONLY;

    struct fat32_dir_entry destination;
    fat32_lookup_result_t destination_result = fat32_lookup_entry_in_directory(
        new_parent, new_leaf, &destination);
    if (destination_result == FAT32_LOOKUP_ERROR) return VFS_ERR_IO;
    if (destination_result == FAT32_LOOKUP_FOUND &&
        (destination.attr & ATTR_DIRECTORY)) return VFS_ERR_IS_DIR;
    if (destination_result == FAT32_LOOKUP_FOUND &&
        (destination.attr & ATTR_READ_ONLY)) return VFS_ERR_READ_ONLY;

    struct fat32_dir_entry renamed = source;
    convert_to_83_format(renamed.name, new_leaf);
    if (destination_result == FAT32_LOOKUP_NOT_FOUND) {
        if (!update_directory_entry(old_parent, source.name, &renamed))
            return VFS_ERR_IO;
    } else {
        uint32_t replaced_cluster = read_start_cluster(&destination);
        if (!update_directory_entry(new_parent, destination.name, &renamed))
            return VFS_ERR_IO;

        struct fat32_dir_entry tombstone = source;
        tombstone.name[0] = 0xE5;
        if (!update_directory_entry(old_parent, source.name, &tombstone))
            return VFS_ERR_IO;

        uint32_t source_cluster = read_start_cluster(&source);
        if (is_valid_cluster(&boot_sector, replaced_cluster) &&
            replaced_cluster != source_cluster &&
            !free_cluster_chain(&boot_sector, replaced_cluster)) {
            return VFS_ERR_IO;
        }
        (void)write_fsinfo();
    }

    fat32_flush_context(fs);
    return VFS_OK;
}

static int fat32_vfs_stat_unlocked(vfs_filesystem_t* fs, const char* path,
                                   vfs_dir_entry_t* stat) {
    if (!fs || !path || !stat) return VFS_ERR_INVALID;
    memset(stat, 0, sizeof(*stat));

    if (strcmp(path, "/") == 0 || path[0] == '\0') {
        strcpy(stat->name, "/");
        stat->type = VFS_DIRECTORY;
        stat->inode = ((fat32_vfs_context_t*)fs->fs_data)->boot.root_cluster;
        return VFS_OK;
    }

    fat32_activate(fs);
    struct fat32_dir_entry fat_entry;
    uint32_t parent_cluster;
    int resolve_result = fat32_resolve_entry(fs, path, &fat_entry,
                                             &parent_cluster);
    if (resolve_result != VFS_OK) return resolve_result;
    (void)parent_cluster;
    fat32_entry_to_vfs_entry(&fat_entry, stat);
    return VFS_OK;
}

static int fat32_vfs_mount(vfs_filesystem_t* fs, drive_t* drive) {
    uint32_t flags = fat32_operation_begin();
    struct fat32_boot_sector saved_boot = boot_sector;
    struct fat32_fsinfo saved_fsinfo = fsinfo;
    bool saved_fsinfo_valid = fsinfo_valid;
    uint32_t saved_directory = current_directory_cluster;
    uint32_t saved_partition = partition_lba_offset;
    uint16_t saved_base = ata_base_address;
    bool saved_master = ata_is_master;
    drive_t* saved_drive = current_drive;
    int result = fat32_vfs_mount_unlocked(fs, drive);
    if (result != VFS_OK) {
        boot_sector = saved_boot;
        fsinfo = saved_fsinfo;
        fsinfo_valid = saved_fsinfo_valid;
        current_directory_cluster = saved_directory;
        partition_lba_offset = saved_partition;
        ata_base_address = saved_base;
        ata_is_master = saved_master;
        current_drive = saved_drive;
    }
    fat32_operation_end(flags);
    return result;
}

static int fat32_vfs_unmount(vfs_filesystem_t* fs) {
    uint32_t flags = fat32_operation_begin();
    int result = fat32_vfs_unmount_unlocked(fs);
    fat32_operation_end(flags);
    return result;
}

static int fat32_vfs_open(vfs_filesystem_t* fs, const char* path,
                          vfs_node_t** node) {
    uint32_t flags = fat32_operation_begin();
    int result = fat32_vfs_open_unlocked(fs, path, node);
    fat32_operation_end(flags);
    return result;
}

static int fat32_vfs_close(vfs_node_t* node) {
    uint32_t flags = fat32_operation_begin();
    int result = fat32_vfs_close_unlocked(node);
    fat32_operation_end(flags);
    return result;
}

static int fat32_vfs_read(vfs_node_t* node, uint32_t offset, uint32_t size,
                          uint8_t* buffer) {
    uint32_t flags = fat32_operation_begin();
    int result = fat32_vfs_read_unlocked(node, offset, size, buffer);
    fat32_operation_end(flags);
    return result;
}

static int fat32_vfs_write(vfs_node_t* node, uint32_t offset, uint32_t size,
                           const uint8_t* buffer) {
    uint32_t flags = fat32_operation_begin();
    int result = fat32_vfs_write_unlocked(node, offset, size, buffer);
    fat32_operation_end(flags);
    return result;
}

static int fat32_vfs_sync(vfs_node_t* node) {
    if (!node || !node->fs || !node->fs->drive) return VFS_ERR_INVALID;
    drive_t* drive = node->fs->drive;
    if (drive->type != DRIVE_TYPE_ATA && drive->type != DRIVE_TYPE_AHCI &&
        drive->type != DRIVE_TYPE_PARTITION)
        return VFS_ERR_UNSUPPORTED;
    return ata_flush_cache(drive->base, drive->is_master)
        ? VFS_OK : VFS_ERR_IO;
}

static int fat32_vfs_readdir(vfs_node_t* node, uint32_t index,
                             vfs_dir_entry_t* entry) {
    uint32_t flags = fat32_operation_begin();
    int result = fat32_vfs_readdir_unlocked(node, index, entry);
    fat32_operation_end(flags);
    return result;
}

static int fat32_vfs_readdir_batch(vfs_node_t* node, uint32_t index,
                                   vfs_dir_entry_t* entries,
                                   uint32_t capacity) {
    uint32_t flags = fat32_operation_begin();
    int result = fat32_vfs_readdir_batch_unlocked(node, index, entries,
                                                  capacity);
    fat32_operation_end(flags);
    return result;
}

static int fat32_vfs_finddir(vfs_node_t* node, const char* name,
                             vfs_node_t** child) {
    uint32_t flags = fat32_operation_begin();
    int result = fat32_vfs_finddir_unlocked(node, name, child);
    fat32_operation_end(flags);
    return result;
}

static int fat32_vfs_mkdir(vfs_filesystem_t* fs, const char* path) {
    uint32_t flags = fat32_operation_begin();
    int result = fat32_vfs_mkdir_unlocked(fs, path);
    fat32_operation_end(flags);
    return result;
}

static int fat32_vfs_rmdir(vfs_filesystem_t* fs, const char* path) {
    uint32_t flags = fat32_operation_begin();
    int result = fat32_vfs_rmdir_unlocked(fs, path);
    fat32_operation_end(flags);
    return result;
}

static int fat32_vfs_create(vfs_filesystem_t* fs, const char* path) {
    uint32_t flags = fat32_operation_begin();
    int result = fat32_vfs_create_unlocked(fs, path);
    fat32_operation_end(flags);
    return result;
}

static int fat32_vfs_delete(vfs_filesystem_t* fs, const char* path) {
    uint32_t flags = fat32_operation_begin();
    int result = fat32_vfs_delete_unlocked(fs, path);
    fat32_operation_end(flags);
    return result;
}

static int fat32_vfs_rename(vfs_filesystem_t* fs, const char* old_path,
                            const char* new_path) {
    uint32_t flags = fat32_operation_begin();
    int result = fat32_vfs_rename_unlocked(fs, old_path, new_path);
    fat32_operation_end(flags);
    return result;
}

static int fat32_vfs_stat(vfs_filesystem_t* fs, const char* path,
                          vfs_dir_entry_t* stat) {
    uint32_t flags = fat32_operation_begin();
    int result = fat32_vfs_stat_unlocked(fs, path, stat);
    fat32_operation_end(flags);
    return result;
}

static int fat32_vfs_space(vfs_filesystem_t* fs, vfs_space_info_t* info) {
    if (!fs || !fs->fs_data || !info) return VFS_ERR_INVALID;
    uint32_t flags = fat32_operation_begin();
    fat32_activate(fs);
    uint32_t total = get_total_clusters(&boot_sector);
    uint32_t free_clusters = fsinfo_valid ? fsinfo.free_cluster_count
                                         : 0xFFFFFFFFU;
    if (free_clusters == 0xFFFFFFFFU || free_clusters > total) {
        free_clusters = 0;
        for (uint32_t cluster = 2; cluster <= total + 1U; ++cluster) {
            if (read_fat_entry(&boot_sector, cluster) == 0) ++free_clusters;
        }
    }
    uint64_t cluster_bytes = (uint64_t)boot_sector.bytes_per_sector *
                             boot_sector.sectors_per_cluster;
    info->total_bytes = (uint64_t)total * cluster_bytes;
    info->free_bytes = (uint64_t)free_clusters * cluster_bytes;
    fat32_operation_end(flags);
    return VFS_OK;
}

// ===========================================================================
// VFS Operations Table
// ===========================================================================

vfs_filesystem_ops_t fat32_vfs_ops = {
    .mount = fat32_vfs_mount,
    .unmount = fat32_vfs_unmount,
    .open = fat32_vfs_open,
    .close = fat32_vfs_close,
    .read = fat32_vfs_read,
    .write = fat32_vfs_write,
    .sync = fat32_vfs_sync,
    .readdir = fat32_vfs_readdir,
    .readdir_batch = fat32_vfs_readdir_batch,
    .finddir = fat32_vfs_finddir,
    .mkdir = fat32_vfs_mkdir,
    .rmdir = fat32_vfs_rmdir,
    .create = fat32_vfs_create,
    .delete = fat32_vfs_delete,
    .rename = fat32_vfs_rename,
    .stat = fat32_vfs_stat,
    .space = fat32_vfs_space
};

// ===========================================================================
// Registration Function
// ===========================================================================

void fat32_register_vfs(void) {
    vfs_register_filesystem("fat32", &fat32_vfs_ops);
}
