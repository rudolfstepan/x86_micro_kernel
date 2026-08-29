/**
 * @file fs/fat32/fat32_vfs_adapter.c
 * @brief VFS-Adapter für FAT32.
 *
 * Layer: Ring-0 virtual filesystem and on-disk format.
 * Contract: On-Disk-Werte, Pfade und Ressourcenbereiche werden vor I/O validiert.
 * Safety: Übersetzt VFS-Operationen ohne FAT32-Grenzen oder Schreibfences zu umgehen.
 */
#include "fs/vfs/vfs.h"
#include "fs/vfs/vfs_time.h"
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
    bool write_supported;
    uint32_t current_directory_cluster;
    uint32_t partition_lba;
    uint16_t ata_base;
    bool ata_master;
    uint64_t data_generation;
    bool cache_disabled;
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
            context->write_supported = fat32_write_supported;
        }
    }
}

static void fat32_advance_context_generation(fat32_vfs_context_t* context) {
    if (!context || context->cache_disabled) return;
    if (context->data_generation == UINT64_MAX) {
        context->cache_disabled = true;
        return;
    }
    context->data_generation++;
}

static void fat32_mark_registered_context_mutated(void) {
    for (unsigned int i = 0; i < FAT32_CONTEXT_REGISTRY_SIZE; ++i) {
        fat32_vfs_context_t* context = fat32_context_registry[i];
        if (context && context->ata_base == ata_base_address &&
            context->ata_master == ata_is_master &&
            context->partition_lba == partition_lba_offset) {
            fat32_advance_context_generation(context);
        }
    }
}

static bool fat32_register_context(fat32_vfs_context_t* context) {
    for (unsigned int i = 0; i < FAT32_CONTEXT_REGISTRY_SIZE; ++i) {
        if (!fat32_context_registry[i]) {
            fat32_context_registry[i] = context;
            fat32_context_sync_hook = fat32_sync_registered_contexts;
            fat32_context_mutation_hook =
                fat32_mark_registered_context_mutated;
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
    if (!any && fat32_context_mutation_hook ==
                    fat32_mark_registered_context_mutated) {
        fat32_context_mutation_hook = NULL;
    }
}

typedef struct {
    struct fat32_dir_entry entry;
    uint32_t parent_cluster;
    char name[MAX_PATH_LENGTH];
    fat32_read_cursor_t read_cursor;
    fat32_write_cursor_t write_cursor;
    uint32_t tail_chain_start;
    uint32_t tail_cluster;
    uint32_t tail_file_size;
    uint64_t cache_generation;
    bool tail_valid;
} fat32_vfs_handle_t;

static void fat32_vfs_cache_reset(fat32_vfs_handle_t* handle) {
    if (!handle) return;
    handle->read_cursor.valid = false;
    handle->write_cursor.valid = false;
    handle->tail_valid = false;
}

static void fat32_vfs_note_data_mutation(vfs_node_t* node,
                                         fat32_vfs_handle_t* handle) {
    if (!node || !node->fs || !node->fs->fs_data || !handle) return;
    fat32_vfs_context_t* context =
        (fat32_vfs_context_t*)node->fs->fs_data;
    fat32_advance_context_generation(context);
    handle->read_cursor.valid = false;
    if (context->cache_disabled) fat32_vfs_cache_reset(handle);
    handle->cache_generation = context->data_generation;
}

static bool fat32_vfs_resolve_tail(vfs_node_t* node,
                                   fat32_vfs_handle_t* handle,
                                   uint32_t* tail_out) {
    if (!node || !node->fs || !node->fs->fs_data || !handle || !tail_out ||
        !is_valid_cluster(&boot_sector, node->inode)) {
        return false;
    }
    fat32_vfs_context_t* context =
        (fat32_vfs_context_t*)node->fs->fs_data;
    if (!context->cache_disabled && handle->tail_valid &&
        handle->cache_generation == context->data_generation &&
        handle->tail_chain_start == node->inode &&
        handle->tail_file_size == node->size &&
        is_valid_cluster(&boot_sector, handle->tail_cluster)) {
        *tail_out = handle->tail_cluster;
        return true;
    }
    if (!fat32_get_chain_tail(&boot_sector, node->inode, tail_out)) {
        fat32_vfs_cache_reset(handle);
        return false;
    }
    handle->tail_chain_start = node->inode;
    handle->tail_cluster = *tail_out;
    handle->tail_file_size = node->size;
    handle->tail_valid = !context->cache_disabled;
    return true;
}

static void fat32_vfs_cache_written_tail(vfs_node_t* node,
                                         fat32_vfs_handle_t* handle,
                                         uint32_t original_tail,
                                         uint32_t expected_end) {
    fat32_vfs_context_t* context = node && node->fs
        ? (fat32_vfs_context_t*)node->fs->fs_data : NULL;
    if (!context || context->cache_disabled || !handle ||
        !handle->write_cursor.valid ||
        handle->write_cursor.chain_start != node->inode ||
        handle->write_cursor.next_offset != expected_end ||
        !is_valid_cluster(&boot_sector, handle->write_cursor.cluster)) {
        if (handle) handle->tail_valid = false;
        return;
    }

    uint32_t next = get_next_cluster_in_chain(
        &boot_sector, handle->write_cursor.cluster);
    uint32_t tail = INVALID_CLUSTER;
    if (is_end_of_cluster_chain(next)) {
        tail = handle->write_cursor.cluster;
    } else if (is_valid_cluster(&boot_sector, next) &&
               is_valid_cluster(&boot_sector, original_tail)) {
        /* A chain may legally be longer than the logical file size.  If the
         * final written cluster has a successor, the previously verified
         * physical tail remains authoritative. */
        tail = original_tail;
    }
    if (!is_valid_cluster(&boot_sector, tail)) {
        handle->tail_valid = false;
        return;
    }
    handle->tail_chain_start = node->inode;
    handle->tail_cluster = tail;
    handle->tail_file_size = node->size;
    handle->tail_valid = true;
}

static void fat32_activate(vfs_filesystem_t* fs) {
    fat32_vfs_context_t* context = (fat32_vfs_context_t*)fs->fs_data;
    boot_sector = context->boot;
    fsinfo = context->fsinfo;
    fsinfo_valid = context->fsinfo_valid;
    fat32_write_supported = context->write_supported;
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
    context->write_supported = fat32_write_supported;
    context->current_directory_cluster = current_directory_cluster;
    context->partition_lba = partition_lba_offset;
    context->ata_base = ata_base_address;
    context->ata_master = ata_is_master;
}

static int fat32_require_write(vfs_filesystem_t* fs) {
    if (!fs || !fs->fs_data) return VFS_ERR_INVALID;
    fat32_activate(fs);
    fat32_vfs_context_t* context = (fat32_vfs_context_t*)fs->fs_data;
    if (!context->write_supported || !fat32_prepare_write()) {
        context->write_supported = false;
        return VFS_ERR_READ_ONLY;
    }
    return VFS_OK;
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
                                 uint32_t* parent_cluster,
                                 char leaf[MAX_PATH_LENGTH]) {
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
            if (!fat32_is_valid_name(token) ||
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
    
    // FAT access time has date-only precision; midnight is the defined value.
    vfs_entry->create_time = vfs_time_from_fat(fat_entry->crt_date,
                                               fat_entry->crt_time);
    vfs_entry->modify_time = vfs_time_from_fat(fat_entry->write_date,
                                               fat_entry->write_time);
    vfs_entry->access_time = vfs_time_from_fat(fat_entry->last_access_date, 0);
}

static vfs_node_t* fat32_make_node(vfs_filesystem_t* fs,
                                   const struct fat32_dir_entry* entry,
                                   uint32_t parent_cluster,
                                   const char* resolved_name) {
    vfs_node_t* node = (vfs_node_t*)malloc(sizeof(vfs_node_t));
    fat32_vfs_handle_t* handle =
        (fat32_vfs_handle_t*)malloc(sizeof(fat32_vfs_handle_t));
    if (!node || !handle) {
        if (node) free(node);
        if (handle) free(handle);
        return NULL;
    }

    memset(node, 0, sizeof(*node));
    memset(handle, 0, sizeof(*handle));
    handle->entry = *entry;
    handle->parent_cluster = parent_cluster;
    if (fs->fs_data) {
        handle->cache_generation =
            ((fat32_vfs_context_t*)fs->fs_data)->data_generation;
    }
    if (resolved_name && fat32_is_valid_name(resolved_name))
        strcpy(handle->name, resolved_name);
    else
        fat32_format_short_name(entry, handle->name);

    vfs_dir_entry_t converted;
    memset(&converted, 0, sizeof(converted));
    fat32_entry_to_vfs_entry(&handle->entry, &converted);
    strcpy(node->name, handle->name);
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
    /* FSInfo persistence is itself a sector mutation.  Capture the handle's
     * generation only after that final write, otherwise every valid-FSInfo
     * volume would discard the newly built sequential hints immediately. */
    fat32_vfs_note_data_mutation(node, handle);
    return true;
}

static int fat32_refresh_file_node(vfs_node_t* node) {
    if (!node || !node->fs || !node->fs->fs_data || !node->fs_specific) {
        return VFS_ERR_INVALID;
    }
    fat32_vfs_handle_t* handle = (fat32_vfs_handle_t*)node->fs_specific;
    struct fat32_dir_entry current;
    char resolved_name[MAX_PATH_LENGTH];
    fat32_lookup_result_t result = fat32_lookup_entry_named(
        handle->parent_cluster, handle->name, &current, resolved_name);
    if (result == FAT32_LOOKUP_NOT_FOUND) return VFS_ERR_NOT_FOUND;
    if (result != FAT32_LOOKUP_FOUND || (current.attr & ATTR_DIRECTORY)) {
        return VFS_ERR_IO;
    }
    fat32_vfs_context_t* context =
        (fat32_vfs_context_t*)node->fs->fs_data;
    if (context->cache_disabled ||
        handle->cache_generation != context->data_generation) {
        fat32_vfs_cache_reset(handle);
        handle->cache_generation = context->data_generation;
    }
    handle->entry = current;
    strcpy(handle->name, resolved_name);
    strcpy(node->name, resolved_name);
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
    context->write_supported = fat32_write_supported;
    context->current_directory_cluster = boot_sector.root_cluster;
    context->partition_lba = partition_lba_offset;
    context->ata_base = ata_base_address;
    context->ata_master = ata_is_master;
    context->data_generation = 1U;
    context->cache_disabled = false;
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

    const char* resolved_name = path;
    for (const char* cursor = path; *cursor; cursor++)
        if (*cursor == '/' && cursor[1] != '\0') resolved_name = cursor + 1;
    vfs_node_t* new_node = fat32_make_node(fs, &fat_entry, parent_cluster,
                                           resolved_name);
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
    fat32_vfs_handle_t* handle = (fat32_vfs_handle_t*)node->fs_specific;
    return (int)read_file_data_at_cursor(
        node->inode, offset, (char*)buffer, size, bytes_to_read,
        &handle->read_cursor);
}

static int fat32_vfs_write_unlocked(vfs_node_t* node, uint32_t offset,
                                    uint32_t size,
                                    const uint8_t* buffer) {
    if (!node || (!buffer && size != 0) || !node->fs) return VFS_ERR_INVALID;
    if (node->type != VFS_FILE) return VFS_ERR_IS_DIR;
    if (size == 0) return 0;
    if (size > INT_MAX) return VFS_ERR_INVALID;
    if (offset > UINT32_MAX - size || !node->fs_specific) return VFS_ERR_INVALID;

    int write_result = fat32_require_write(node->fs);
    if (write_result != VFS_OK) return write_result;
    int refresh = fat32_refresh_file_node(node);
    if (refresh != VFS_OK) return refresh;
    fat32_vfs_handle_t* handle = (fat32_vfs_handle_t*)node->fs_specific;
    if (handle->entry.attr & ATTR_READ_ONLY) return VFS_ERR_READ_ONLY;
    uint32_t start_cluster = node->inode;
    uint32_t durable_size = node->size;
    uint32_t original_tail = INVALID_CLUSTER;
    if (is_valid_cluster(&boot_sector, node->inode)) {
        if (!fat32_vfs_resolve_tail(node, handle, &original_tail)) {
            return VFS_ERR_IO;
        }
    } else if (node->inode != 0U || node->size != 0U) {
        fat32_vfs_cache_reset(handle);
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
            int written = write_file_data_at_checked_cursor(
                &start_cluster, position, zeroes, amount,
                &chain_reclaim_safe, &handle->write_cursor);
            if (written > 0) {
                position += (uint32_t)written;
                durable_size = position;
            }
            if (!chain_reclaim_safe) {
                fat32_vfs_cache_reset(handle);
                return VFS_ERR_IO;
            }
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
                    fat32_vfs_cache_reset(handle);
                    return VFS_ERR_IO;
                }
                if ((durable_size != node->size || start_cluster != node->inode) &&
                    !fat32_commit_node_data(node, start_cluster, durable_size,
                                            original_tail,
                                            chain_reclaim_safe)) {
                    fat32_vfs_cache_reset(handle);
                    return VFS_ERR_IO;
                }
                fat32_vfs_cache_reset(handle);
                return VFS_ERR_IO;
            }
        }
        durable_size = offset;
    }

    int written = write_file_data_at_checked_cursor(
        &start_cluster, offset, buffer, size, &chain_reclaim_safe,
        &handle->write_cursor);
    if (!chain_reclaim_safe) {
        fat32_vfs_cache_reset(handle);
        return VFS_ERR_IO;
    }
    if (written < 0) {
        if (durable_size == node->size) {
            if (is_valid_cluster(&boot_sector, node->inode)) {
                (void)fat32_reclaim_chain_suffix(&boot_sector,
                                                 original_tail);
            } else if (is_valid_cluster(&boot_sector, start_cluster)) {
                (void)free_cluster_chain(&boot_sector, start_cluster);
            }
            fat32_flush_context(node->fs);
            fat32_vfs_cache_reset(handle);
            return VFS_ERR_IO;
        }
        if ((durable_size != node->size || start_cluster != node->inode) &&
            !fat32_commit_node_data(node, start_cluster, durable_size,
                                    original_tail, chain_reclaim_safe)) {
            fat32_vfs_cache_reset(handle);
            return VFS_ERR_IO;
        }
        fat32_vfs_cache_reset(handle);
        return VFS_ERR_IO;
    }

    uint32_t end = offset + (uint32_t)written;
    if (end < durable_size) end = durable_size;
    if (!fat32_commit_node_data(node, start_cluster, end, original_tail,
                                chain_reclaim_safe)) {
        fat32_vfs_cache_reset(handle);
        return VFS_ERR_IO;
    }
    fat32_vfs_cache_written_tail(node, handle, original_tail,
                                 offset + (uint32_t)written);
    return written;
}

static int fat32_vfs_truncate_unlocked(vfs_node_t* node, uint32_t size) {
    if (!node || !node->fs || !node->fs_specific)
        return VFS_ERR_INVALID;
    if (node->type != VFS_FILE) return VFS_ERR_IS_DIR;
    int admission = fat32_require_write(node->fs);
    if (admission != VFS_OK) return admission;
    int refresh = fat32_refresh_file_node(node);
    if (refresh != VFS_OK) return refresh;

    fat32_vfs_handle_t* handle = (fat32_vfs_handle_t*)node->fs_specific;
    fat32_vfs_cache_reset(handle);
    if ((handle->entry.attr & ATTR_READ_ONLY) != 0U)
        return VFS_ERR_READ_ONLY;
    struct fat32_dir_entry original_entry = handle->entry;
    uint32_t original_cluster = read_start_cluster(&original_entry);
    uint32_t original_size = node->size;
    uint32_t original_tail = INVALID_CLUSTER;
    if (is_valid_cluster(&boot_sector, original_cluster)) {
        if (!fat32_get_chain_tail(&boot_sector, original_cluster,
                                  &original_tail)) return VFS_ERR_IO;
    } else if (original_cluster != 0U || original_size != 0U) {
        return VFS_ERR_IO;
    }

    uint32_t cluster_bytes = boot_sector.bytes_per_sector *
        boot_sector.sectors_per_cluster;
    if (cluster_bytes == 0U ||
        (uint64_t)size > (uint64_t)get_total_clusters(&boot_sector) *
                         cluster_bytes) return VFS_ERR_NO_SPACE;
    if (size == original_size) return VFS_OK;

    if (size > original_size) {
        uint32_t start_cluster = original_cluster;
        uint32_t position = original_size;
        bool chain_reclaim_safe = true;
        uint8_t zeroes[SECTOR_SIZE];
        memset(zeroes, 0, sizeof(zeroes));
        while (position < size) {
            uint32_t amount = size - position;
            if (amount > sizeof(zeroes)) amount = sizeof(zeroes);
            int written = write_file_data_at_checked(
                &start_cluster, position, zeroes, amount,
                &chain_reclaim_safe);
            if (!chain_reclaim_safe || written != (int)amount) {
                if (is_valid_cluster(&boot_sector, original_cluster)) {
                    (void)fat32_reclaim_chain_suffix(&boot_sector,
                                                     original_tail);
                } else if (is_valid_cluster(&boot_sector, start_cluster)) {
                    (void)free_cluster_chain(&boot_sector, start_cluster);
                }
                fat32_flush_context(node->fs);
                return VFS_ERR_IO;
            }
            position += amount;
        }
        if (!fat32_commit_node_data(node, start_cluster, size, original_tail,
                                    chain_reclaim_safe)) return VFS_ERR_IO;
        return VFS_OK;
    }

    uint32_t retained_tail = INVALID_CLUSTER;
    if (size != 0U) {
        uint32_t retained_clusters = (uint32_t)(
            ((uint64_t)size + cluster_bytes - 1U) / cluster_bytes);
        retained_tail = original_cluster;
        for (uint32_t index = 1U; index < retained_clusters; ++index) {
            uint32_t next = get_next_cluster_in_chain(&boot_sector,
                                                      retained_tail);
            if (!is_valid_cluster(&boot_sector, next)) return VFS_ERR_IO;
            retained_tail = next;
        }
        uint32_t suffix = get_next_cluster_in_chain(&boot_sector,
                                                    retained_tail);
        if (suffix == INVALID_CLUSTER ||
            (!is_end_of_cluster_chain(suffix) &&
             !is_valid_cluster(&boot_sector, suffix))) return VFS_ERR_IO;
    }

    uint32_t published_cluster = size == 0U ? 0U : original_cluster;
    /* Publish the smaller logical prefix first. Any excess chain is private
     * only after the directory size no longer makes it visible. */
    if (!fat32_commit_node_data(node, published_cluster, size, original_tail,
                                true))
        return VFS_ERR_IO;
    if (size == 0U) {
        if (is_valid_cluster(&boot_sector, original_cluster) &&
            !free_cluster_chain(&boot_sector, original_cluster))
            return VFS_ERR_IO;
    } else if (!fat32_reclaim_chain_suffix(&boot_sector, retained_tail)) {
        return VFS_ERR_IO;
    }
    (void)write_fsinfo();
    fat32_flush_context(node->fs);
    return VFS_OK;
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
    uint32_t count = 0;
    while (count < capacity) {
        struct fat32_dir_entry entry;
        char resolved_name[MAX_PATH_LENGTH];
        fat32_lookup_result_t result = fat32_get_directory_entry(
            node->inode, index + count, &entry, resolved_name);
        if (result == FAT32_LOOKUP_NOT_FOUND) return (int)count;
        if (result != FAT32_LOOKUP_FOUND) return VFS_ERR_IO;
        memset(&entries_out[count], 0, sizeof(entries_out[count]));
        fat32_entry_to_vfs_entry(&entry, &entries_out[count]);
        strcpy(entries_out[count].name, resolved_name);
        count++;
    }
    return (int)count;
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
    char resolved_name[MAX_PATH_LENGTH];
    if (fat32_lookup_entry_named(node->inode, name, entry, resolved_name) !=
        FAT32_LOOKUP_FOUND) {
        free(entry);
        return VFS_ERR_NOT_FOUND;
    }
    *child = fat32_make_node(node->fs, entry, node->inode, resolved_name);
    free(entry);
    return *child ? VFS_OK : VFS_ERR_NO_MEMORY;
}

static int fat32_vfs_mkdir_unlocked(vfs_filesystem_t* fs, const char* path) {
    if (!fs || !path) return VFS_ERR_INVALID;
    int write_result = fat32_require_write(fs);
    if (write_result != VFS_OK) return write_result;
    uint32_t parent;
    char leaf[MAX_PATH_LENGTH];
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
    int write_result = fat32_require_write(fs);
    if (write_result != VFS_OK) return write_result;
    uint32_t parent;
    char leaf[MAX_PATH_LENGTH];
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
    int write_result = fat32_require_write(fs);
    if (write_result != VFS_OK) return write_result;
    uint32_t parent;
    char leaf[MAX_PATH_LENGTH];
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
    int write_result = fat32_require_write(fs);
    if (write_result != VFS_OK) return write_result;
    uint32_t parent;
    char leaf[MAX_PATH_LENGTH];
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

typedef struct {
    uint32_t sector;
    uint32_t index;
} fat32_rename_slot_t;

/* Rename runs inside one VFS/ATA journal transaction. Use journal-aware
 * single-sector reads here: a whole-cluster transport read may not include a
 * destination alias update that is still pending in that transaction. */
static bool fat32_tombstone_rename_source(
        uint32_t parent_cluster, const struct fat32_dir_entry* source) {
    if (!source || !is_valid_cluster(&boot_sector, parent_cluster) ||
        boot_sector.bytes_per_sector != SECTOR_SIZE) return false;
    fat32_rename_slot_t slots[FAT32_MAX_LFN_ENTRIES + 1U];
    uint32_t slot_count = 0U;
    uint32_t current = parent_cluster;
    uint32_t traversed = 0U;
    uint32_t cluster_limit = get_total_clusters(&boot_sector);
    struct fat32_dir_entry entries[
        SECTOR_SIZE / sizeof(struct fat32_dir_entry)];
    struct fat32_dir_entry verify[
        SECTOR_SIZE / sizeof(struct fat32_dir_entry)];

    while (is_valid_cluster(&boot_sector, current) &&
           traversed++ < cluster_limit) {
        uint32_t first_sector = cluster_to_sector(&boot_sector, current);
        if (first_sector == INVALID_CLUSTER) return false;
        for (uint32_t sector_index = 0U;
             sector_index < boot_sector.sectors_per_cluster;
             ++sector_index) {
            uint32_t sector = first_sector + sector_index;
            if (!ata_read_sector(ata_base_address, sector, entries,
                                 ata_is_master)) return false;
            for (uint32_t index = 0U;
                 index < SECTOR_SIZE / sizeof(struct fat32_dir_entry);
                 ++index) {
                if (entries[index].name[0] == 0x00U) return false;
                if (entries[index].name[0] == 0xE5U) {
                    slot_count = 0U;
                    continue;
                }
                if (entries[index].attr == ATTR_LONG_NAME) {
                    if (slot_count == FAT32_MAX_LFN_ENTRIES)
                        slot_count = 0U;
                    slots[slot_count++] = (fat32_rename_slot_t){
                        .sector = sector,
                        .index = index,
                    };
                    continue;
                }
                if (memcmp(&entries[index], source, sizeof(*source)) != 0) {
                    slot_count = 0U;
                    continue;
                }
                slots[slot_count++] = (fat32_rename_slot_t){
                    .sector = sector,
                    .index = index,
                };
                uint32_t first = 0U;
                while (first < slot_count) {
                    uint32_t target = slots[first].sector;
                    if (!ata_read_sector(ata_base_address, target, entries,
                                         ata_is_master)) return false;
                    uint32_t cursor = first;
                    while (cursor < slot_count &&
                           slots[cursor].sector == target) {
                        entries[slots[cursor].index].name[0] = 0xE5U;
                        ++cursor;
                    }
                    if (!fat32_write_sector(target, entries) ||
                        !ata_read_sector(ata_base_address, target, verify,
                                         ata_is_master) ||
                        memcmp(entries, verify, SECTOR_SIZE) != 0)
                        return false;
                    first = cursor;
                }
                return true;
            }
        }
        uint32_t next = get_next_cluster_in_chain(&boot_sector, current);
        if (next == INVALID_CLUSTER || is_end_of_cluster_chain(next))
            return false;
        current = next;
    }
    return false;
}

static int fat32_vfs_rename_unlocked(vfs_filesystem_t* fs,
                                     const char* old_path,
                                     const char* new_path) {
    if (!fs || !old_path || !new_path) return VFS_ERR_INVALID;
    int write_result = fat32_require_write(fs);
    if (write_result != VFS_OK) return write_result;

    uint32_t old_parent;
    uint32_t new_parent;
    char old_leaf[MAX_PATH_LENGTH];
    char new_leaf[MAX_PATH_LENGTH];
    if (!fat32_resolve_parent(fs, old_path, &old_parent, old_leaf) ||
        !fat32_resolve_parent(fs, new_path, &new_parent, new_leaf)) {
        return VFS_ERR_INVALID;
    }
    if (old_parent != new_parent) return VFS_ERR_UNSUPPORTED;
    if (!fat32_is_valid_name(old_leaf) || !fat32_is_valid_name(new_leaf))
        return VFS_ERR_INVALID;
    bool short_rename = fat32_is_valid_short_name(old_leaf) &&
                        fat32_is_valid_short_name(new_leaf);
    unsigned char old_name[11];
    unsigned char new_name[11];
    if (short_rename) {
        convert_to_83_format(old_name, old_leaf);
        convert_to_83_format(new_name, new_leaf);
        if (memcmp(old_name, new_name, sizeof(old_name)) == 0) return VFS_OK;
    }

    struct fat32_dir_entry source;
    fat32_lookup_result_t source_result = fat32_lookup_entry_in_directory(
        old_parent, old_leaf, &source);
    if (source_result == FAT32_LOOKUP_NOT_FOUND) return VFS_ERR_NOT_FOUND;
    if (source_result != FAT32_LOOKUP_FOUND) return VFS_ERR_IO;
    if (source.attr & ATTR_READ_ONLY) return VFS_ERR_READ_ONLY;

    struct fat32_dir_entry destination;
    fat32_lookup_result_t destination_result = fat32_lookup_entry_in_directory(
        new_parent, new_leaf, &destination);
    if (destination_result == FAT32_LOOKUP_ERROR) return VFS_ERR_IO;
    if (destination_result == FAT32_LOOKUP_FOUND &&
        (destination.attr & ATTR_DIRECTORY)) return VFS_ERR_IS_DIR;
    if (destination_result == FAT32_LOOKUP_FOUND &&
        (destination.attr & ATTR_READ_ONLY)) return VFS_ERR_READ_ONLY;
    /* A directory may be renamed inside its current parent without changing
     * either dot entry. Replacing an existing directory remains fail-closed;
     * cross-parent directory moves are rejected above because they would
     * require a bounded update of the child's dot-dot authority. */
    if ((source.attr & ATTR_DIRECTORY) &&
        destination_result == FAT32_LOOKUP_FOUND)
        return VFS_ERR_UNSUPPORTED;
    if (destination_result == FAT32_LOOKUP_FOUND &&
        memcmp(&source, &destination, sizeof(source)) == 0)
        return VFS_OK;

    if (!short_rename) {
        if (destination_result == FAT32_LOOKUP_FOUND) {
            /* Keep the destination's validated LFN slots and checksum-bound
             * alias. The enclosing VFS mutation journal publishes this alias
             * update and the complete source-sequence tombstone together. */
            uint32_t replaced_cluster = read_start_cluster(&destination);
            struct fat32_dir_entry replacement = source;
            memcpy(replacement.name, destination.name,
                   sizeof(replacement.name));
            replacement.nt_res = destination.nt_res;
            if (!update_directory_entry(new_parent, destination.name,
                                        &replacement))
                return VFS_ERR_IO;
            if (!fat32_tombstone_rename_source(old_parent, &source))
                return VFS_ERR_IO;

            uint32_t source_cluster = read_start_cluster(&source);
            if (is_valid_cluster(&boot_sector, replaced_cluster) &&
                replaced_cluster != source_cluster &&
                !free_cluster_chain(&boot_sector, replaced_cluster))
                return VFS_ERR_IO;
            (void)write_fsinfo();
            fat32_flush_context(fs);
            return VFS_OK;
        }
        if (!add_entry_to_directory(&boot_sector, new_parent, new_leaf,
                                    read_start_cluster(&source), source.attr))
            return VFS_ERR_IO;
        struct fat32_dir_entry published;
        if (fat32_lookup_entry_in_directory(new_parent, new_leaf, &published) !=
            FAT32_LOOKUP_FOUND) return VFS_ERR_IO;
        struct fat32_dir_entry clone = source;
        memcpy(clone.name, published.name, sizeof(clone.name));
        clone.nt_res = published.nt_res;
        if (!update_directory_entry(new_parent, published.name, &clone)) {
            (void)remove_entry_from_directory(&boot_sector, new_parent,
                                              &published);
            return VFS_ERR_IO;
        }
        if (!fat32_tombstone_rename_source(old_parent, &source))
            return VFS_ERR_IO;
        (void)write_fsinfo();
        fat32_flush_context(fs);
        return VFS_OK;
    }

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
    const char* requested_name = path;
    for (const char* cursor = path; *cursor; cursor++)
        if (*cursor == '/' && cursor[1] != '\0') requested_name = cursor + 1;
    char resolved_name[MAX_PATH_LENGTH];
    if (fat32_lookup_entry_named(parent_cluster, requested_name, &fat_entry,
                                 resolved_name) != FAT32_LOOKUP_FOUND)
        return VFS_ERR_IO;
    fat32_entry_to_vfs_entry(&fat_entry, stat);
    strcpy(stat->name, resolved_name);
    return VFS_OK;
}

static int fat32_vfs_fstat_unlocked(vfs_node_t* node,
                                    vfs_dir_entry_t* stat) {
    if (!node || !node->fs || !node->fs_specific || !stat ||
        node->type != VFS_FILE) return VFS_ERR_INVALID;
    fat32_activate(node->fs);
    int refresh = fat32_refresh_file_node(node);
    if (refresh != VFS_OK) return refresh;
    fat32_vfs_handle_t* handle = (fat32_vfs_handle_t*)node->fs_specific;
    fat32_entry_to_vfs_entry(&handle->entry, stat);
    strcpy(stat->name, handle->name);
    return VFS_OK;
}

static bool fat32_vfs_same_object(const vfs_node_t* first,
                                  const vfs_node_t* second) {
    if (!first || !second || first->fs != second->fs) return false;
    if (first == first->fs->root || second == second->fs->root)
        return first == second;
    if (!first->fs_specific || !second->fs_specific) return false;
    const fat32_vfs_handle_t* left =
        (const fat32_vfs_handle_t*)first->fs_specific;
    const fat32_vfs_handle_t* right =
        (const fat32_vfs_handle_t*)second->fs_specific;
    return left->parent_cluster == right->parent_cluster &&
           memcmp(left->entry.name, right->entry.name,
                  sizeof(left->entry.name)) == 0;
}

static int fat32_vfs_mount(vfs_filesystem_t* fs, drive_t* drive) {
    uint32_t flags = fat32_operation_begin();
    struct fat32_boot_sector saved_boot = boot_sector;
    struct fat32_fsinfo saved_fsinfo = fsinfo;
    bool saved_fsinfo_valid = fsinfo_valid;
    bool saved_write_supported = fat32_write_supported;
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
        fat32_write_supported = saved_write_supported;
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

static int fat32_vfs_truncate(vfs_node_t* node, uint32_t size) {
    uint32_t flags = fat32_operation_begin();
    int result = fat32_vfs_truncate_unlocked(node, size);
    fat32_operation_end(flags);
    return result;
}

static int fat32_vfs_fstat(vfs_node_t* node, vfs_dir_entry_t* stat) {
    uint32_t flags = fat32_operation_begin();
    int result = fat32_vfs_fstat_unlocked(node, stat);
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

static int fat32_vfs_touch(vfs_filesystem_t* fs, const char* path) {
    if (!fs || !path) return VFS_ERR_INVALID;
    uint32_t flags = fat32_operation_begin();
    int result = fat32_require_write(fs);
    if (result != VFS_OK) {
        fat32_operation_end(flags);
        return result;
    }
    struct fat32_dir_entry entry;
    uint32_t parent_cluster;
    result = fat32_resolve_entry(fs, path, &entry, &parent_cluster);
    if (result == VFS_OK) {
        if (entry.attr & ATTR_DIRECTORY) result = VFS_ERR_IS_DIR;
        else if (entry.attr & ATTR_READ_ONLY) result = VFS_ERR_READ_ONLY;
        else {
            set_fat32_time(&entry.write_time, &entry.write_date);
            set_fat32_time(NULL, &entry.last_access_date);
            result = update_directory_entry(parent_cluster, entry.name,
                                            &entry) ? VFS_OK : VFS_ERR_IO;
            if (result == VFS_OK) fat32_flush_context(fs);
        }
    }
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
    .truncate = fat32_vfs_truncate,
    .fstat = fat32_vfs_fstat,
    .same_object = fat32_vfs_same_object,
    .sync = fat32_vfs_sync,
    .readdir = fat32_vfs_readdir,
    .readdir_batch = fat32_vfs_readdir_batch,
    .finddir = fat32_vfs_finddir,
    .mkdir = fat32_vfs_mkdir,
    .rmdir = fat32_vfs_rmdir,
    .create = fat32_vfs_create,
    .delete = fat32_vfs_delete,
    .rename = fat32_vfs_rename,
    .touch = fat32_vfs_touch,
    .stat = fat32_vfs_stat,
    .space = fat32_vfs_space
};

// ===========================================================================
// Registration Function
// ===========================================================================

void fat32_register_vfs(void) {
    vfs_register_filesystem("fat32", &fat32_vfs_ops);
}
