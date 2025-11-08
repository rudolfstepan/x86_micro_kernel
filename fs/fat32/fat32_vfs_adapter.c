#include "fs/vfs/vfs.h"
#include "fat32.h"
#include "lib/libc/stdio.h"
#include "lib/libc/string.h"
#include "lib/libc/stdlib.h"
#include "drivers/bus/drives.h"

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

// ===========================================================================
// Helper Functions
// ===========================================================================

static void fat32_entry_to_vfs_entry(struct fat32_dir_entry* fat_entry, vfs_dir_entry_t* vfs_entry) {
    if (!fat_entry || !vfs_entry) return;
    
    // Copy name (convert from 8.3 format)
    char name[13];
    int name_idx = 0;
    
    // Copy filename (skip trailing spaces)
    int i;
    for (i = 0; i < 8 && fat_entry->name[i] != ' '; i++) {
        name[name_idx++] = fat_entry->name[i];
    }
    
    // Add extension if present
    if (fat_entry->name[8] != ' ') {
        name[name_idx++] = '.';
        for (i = 8; i < 11 && fat_entry->name[i] != ' '; i++) {
            name[name_idx++] = fat_entry->name[i];
        }
    }
    name[name_idx] = '\0';
    
    strncpy(vfs_entry->name, name, 255);
    vfs_entry->name[255] = '\0';
    
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

// ===========================================================================
// VFS Operations Implementation
// ===========================================================================

static int fat32_vfs_mount(vfs_filesystem_t* fs, drive_t* drive) {
    if (!fs || !drive) {
        return VFS_ERR_INVALID;
    }
    
    printf("FAT32: Mounting drive %s (base=0x%X, master=%d)\n", 
           drive->name, drive->base, drive->is_master);
    
    // Call existing FAT32 initialization
    int result = fat32_init_fs(drive->base, drive->is_master);
    if (result != SUCCESS) {
        printf("FAT32: Mount failed (init returned %d)\n", result);
        return VFS_ERR_IO;
    }
    
    // Store boot sector as filesystem data
    struct fat32_boot_sector* bs = (struct fat32_boot_sector*)malloc(sizeof(struct fat32_boot_sector));
    if (!bs) {
        return VFS_ERR_NO_MEMORY;
    }
    memcpy(bs, &boot_sector, sizeof(struct fat32_boot_sector));
    fs->fs_data = bs;
    
    // Create root node
    vfs_node_t* root = (vfs_node_t*)malloc(sizeof(vfs_node_t));
    if (!root) {
        free(bs);
        return VFS_ERR_NO_MEMORY;
    }
    
    strcpy(root->name, "/");
    root->type = VFS_DIRECTORY;
    root->inode = boot_sector.root_cluster;
    root->size = 0;
    root->flags = 0;
    root->fs = fs;
    root->fs_specific = NULL;
    
    fs->root = root;
    
    printf("FAT32: Successfully mounted (root cluster=%u)\n", boot_sector.root_cluster);
    return VFS_OK;
}

static int fat32_vfs_unmount(vfs_filesystem_t* fs) {
    if (!fs) {
        return VFS_ERR_INVALID;
    }
    
    printf("FAT32: Unmounting filesystem\n");
    
    if (fs->fs_data) {
        free(fs->fs_data);
        fs->fs_data = NULL;
    }
    
    if (fs->root) {
        free(fs->root);
        fs->root = NULL;
    }
    
    return VFS_OK;
}

static int fat32_vfs_open(vfs_filesystem_t* fs, const char* path, vfs_node_t** node) {
    if (!fs || !path || !node) {
        return VFS_ERR_INVALID;
    }
    
    extern drive_t* current_drive;
    current_drive = fs->drive;  // Set current drive for FAT32 functions
    
    printf("FAT32: Opening '%s'\n", path);
    
    // Handle root directory
    if (strcmp(path, "/") == 0) {
        *node = fs->root;
        return VFS_OK;
    }
    
    // Remove leading slash
    const char* filename = path;
    if (*filename == '/') {
        filename++;
    }
    
    // Find file in current directory (simplified - assumes flat structure for now)
    struct fat32_dir_entry* fat_entry = find_file_in_directory(filename);
    if (!fat_entry) {
        return VFS_ERR_NOT_FOUND;
    }
    
    // Create VFS node
    vfs_node_t* new_node = (vfs_node_t*)malloc(sizeof(vfs_node_t));
    if (!new_node) {
        free(fat_entry);
        return VFS_ERR_NO_MEMORY;
    }
    
    // Convert filename
    char name[13];
    int name_idx = 0;
    for (int i = 0; i < 8 && fat_entry->name[i] != ' '; i++) {
        name[name_idx++] = fat_entry->name[i];
    }
    if (fat_entry->name[8] != ' ') {
        name[name_idx++] = '.';
        for (int i = 8; i < 11 && fat_entry->name[i] != ' '; i++) {
            name[name_idx++] = fat_entry->name[i];
        }
    }
    name[name_idx] = '\0';
    
    strncpy(new_node->name, name, 255);
    new_node->name[255] = '\0';
    new_node->type = (fat_entry->attr & 0x10) ? VFS_DIRECTORY : VFS_FILE;
    new_node->inode = ((uint32_t)fat_entry->first_cluster_high << 16) | fat_entry->first_cluster_low;
    new_node->size = fat_entry->file_size;
    new_node->flags = 0;
    new_node->fs = fs;
    new_node->fs_specific = fat_entry;  // Store FAT entry
    
    *node = new_node;
    return VFS_OK;
}

static int fat32_vfs_close(vfs_node_t* node) {
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

static int fat32_vfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    if (!node || !buffer) {
        return VFS_ERR_INVALID;
    }
    
    if (node->type != VFS_FILE) {
        return VFS_ERR_IS_DIR;
    }
    
    extern drive_t* current_drive;
    current_drive = node->fs->drive;  // Set current drive
    
    // For now, only support reading from start (offset must be 0)
    if (offset != 0) {
        printf("FAT32: Warning - offset %u not supported, reading from start\n", offset);
    }
    
    uint32_t bytes_to_read = (size < node->size) ? size : node->size;
    unsigned int bytes_read = read_file_data(node->inode, (char*)buffer, size, bytes_to_read);
    
    return bytes_read;
}

static int fat32_vfs_write(vfs_node_t* node, uint32_t offset, uint32_t size, const uint8_t* buffer) {
    // Write not implemented yet
    return VFS_ERR_UNSUPPORTED;
}

static int fat32_vfs_readdir(vfs_node_t* node, uint32_t index, vfs_dir_entry_t* entry) {
    if (!node || !entry) {
        return VFS_ERR_INVALID;
    }
    
    if (node->type != VFS_DIRECTORY) {
        return VFS_ERR_NOT_DIR;
    }
    
    extern drive_t* current_drive;
    current_drive = node->fs->drive;  // Set current drive
    
    // Read directory entries
    unsigned int sector = cluster_to_sector(&boot_sector, node->inode);
    struct fat32_dir_entry* entries = (struct fat32_dir_entry*)malloc(SECTOR_SIZE * boot_sector.sectors_per_cluster);
    
    if (!entries) {
        return VFS_ERR_NO_MEMORY;
    }
    
    // Read all sectors in cluster
    for (unsigned int i = 0; i < boot_sector.sectors_per_cluster; i++) {
        if (!ata_read_sector(current_drive->base, sector + i, 
                            &entries[i * (SECTOR_SIZE / sizeof(struct fat32_dir_entry))], 
                            current_drive->is_master)) {
            free(entries);
            return VFS_ERR_IO;
        }
    }
    
    // Find the entry at the requested index (skip deleted/special entries)
    uint32_t current_index = 0;
    uint32_t max_entries = boot_sector.sectors_per_cluster * (SECTOR_SIZE / sizeof(struct fat32_dir_entry));
    
    for (uint32_t i = 0; i < max_entries; i++) {
        // Skip deleted entries and volume labels
        if (entries[i].name[0] == 0x00) {
            break;  // End of directory
        }
        if (entries[i].name[0] == 0xE5) {
            continue;  // Deleted entry
        }
        if (entries[i].attr == 0x0F) {
            continue;  // Long filename entry
        }
        if (entries[i].attr & 0x08) {
            continue;  // Volume label
        }
        
        if (current_index == index) {
            fat32_entry_to_vfs_entry(&entries[i], entry);
            free(entries);
            return VFS_OK;
        }
        current_index++;
    }
    
    free(entries);
    return VFS_ERR_NOT_FOUND;  // Index out of range
}

static int fat32_vfs_finddir(vfs_node_t* node, const char* name, vfs_node_t** child) {
    // Use existing find_file_in_directory
    return VFS_ERR_UNSUPPORTED;  // TODO: Implement
}

static int fat32_vfs_mkdir(vfs_filesystem_t* fs, const char* path) {
    // Use existing fat32_create_dir
    return VFS_ERR_UNSUPPORTED;  // TODO: Implement
}

static int fat32_vfs_rmdir(vfs_filesystem_t* fs, const char* path) {
    // Use existing fat32_delete_dir
    return VFS_ERR_UNSUPPORTED;  // TODO: Implement
}

static int fat32_vfs_create(vfs_filesystem_t* fs, const char* path) {
    return VFS_ERR_UNSUPPORTED;  // TODO: Implement
}

static int fat32_vfs_delete(vfs_filesystem_t* fs, const char* path) {
    return VFS_ERR_UNSUPPORTED;  // TODO: Implement
}

static int fat32_vfs_stat(vfs_filesystem_t* fs, const char* path, vfs_dir_entry_t* stat) {
    // Open file and get info
    vfs_node_t* node;
    int result = fat32_vfs_open(fs, path, &node);
    if (result != VFS_OK) {
        return result;
    }
    
    strncpy(stat->name, node->name, 255);
    stat->name[255] = '\0';
    stat->type = node->type;
    stat->size = node->size;
    stat->inode = node->inode;
    
    fat32_vfs_close(node);
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
    .readdir = fat32_vfs_readdir,
    .finddir = fat32_vfs_finddir,
    .mkdir = fat32_vfs_mkdir,
    .rmdir = fat32_vfs_rmdir,
    .create = fat32_vfs_create,
    .delete = fat32_vfs_delete,
    .stat = fat32_vfs_stat
};

// ===========================================================================
// Registration Function
// ===========================================================================

void fat32_register_vfs(void) {
    vfs_register_filesystem("fat32", &fat32_vfs_ops);
}
