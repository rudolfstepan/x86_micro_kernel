#include "fs/vfs/vfs.h"
#include "fat12.h"
#include "lib/libc/stdio.h"
#include "lib/libc/string.h"
#include "lib/libc/stdlib.h"
#include "drivers/bus/drives.h"

// ===========================================================================
// FAT12 VFS Adapter
// Wraps existing FAT12 implementation to work with VFS layer
// ===========================================================================

extern bool fat12_init_fs(uint8_t drive);
extern fat12_t* fat12;

// ===========================================================================
// VFS Operations Implementation
// ===========================================================================

static int fat12_vfs_mount(vfs_filesystem_t* fs, drive_t* drive) {
    if (!fs || !drive) {
        return VFS_ERR_INVALID;
    }
    
    printf("FAT12: Mounting drive %s (fdd_drive_no=%d)\n", 
           drive->name, drive->fdd_drive_no);
    
    // Call existing FAT12 initialization
    if (!fat12_init_fs(drive->fdd_drive_no)) {
        printf("FAT12: Mount failed\n");
        return VFS_ERR_IO;
    }
    
    // Store FAT12 structure as filesystem data
    fs->fs_data = fat12;
    
    // Create root node
    vfs_node_t* root = (vfs_node_t*)malloc(sizeof(vfs_node_t));
    if (!root) {
        return VFS_ERR_NO_MEMORY;
    }
    
    strcpy(root->name, "/");
    root->type = VFS_DIRECTORY;
    root->inode = 0;  // Root directory has no cluster in FAT12
    root->size = 0;
    root->flags = 0;
    root->fs = fs;
    root->fs_specific = NULL;
    
    fs->root = root;
    
    printf("FAT12: Successfully mounted\n");
    return VFS_OK;
}

static int fat12_vfs_unmount(vfs_filesystem_t* fs) {
    if (!fs) {
        return VFS_ERR_INVALID;
    }
    
    printf("FAT12: Unmounting filesystem\n");
    
    // FAT12 structure is global, don't free it
    fs->fs_data = NULL;
    
    if (fs->root) {
        free(fs->root);
        fs->root = NULL;
    }
    
    return VFS_OK;
}

static int fat12_vfs_open(vfs_filesystem_t* fs, const char* path, vfs_node_t** node) {
    if (!fs || !path || !node) {
        return VFS_ERR_INVALID;
    }
    
    printf("FAT12: Opening '%s'\n", path);
    
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
    
    // Open file using existing FAT12 function
    fat12_file* file = fat12_open_file(filename, "r");
    if (!file) {
        return VFS_ERR_NOT_FOUND;
    }
    
    // Create VFS node
    vfs_node_t* new_node = (vfs_node_t*)malloc(sizeof(vfs_node_t));
    if (!new_node) {
        fat12_close_file(file);
        return VFS_ERR_NO_MEMORY;
    }
    
    strncpy(new_node->name, filename, 255);
    new_node->name[255] = '\0';
    new_node->type = VFS_FILE;  // Assume file for now
    new_node->inode = file->start_cluster;
    new_node->size = file->size;
    new_node->flags = 0;
    new_node->fs = fs;
    new_node->fs_specific = file;  // Store FAT12 file structure
    
    *node = new_node;
    return VFS_OK;
}

static int fat12_vfs_close(vfs_node_t* node) {
    if (!node) {
        return VFS_ERR_INVALID;
    }
    
    // Don't free root node
    if (node == node->fs->root) {
        return VFS_OK;
    }
    
    if (node->fs_specific) {
        fat12_close_file((fat12_file*)node->fs_specific);
    }
    free(node);
    
    return VFS_OK;
}

static int fat12_vfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    if (!node || !buffer) {
        return VFS_ERR_INVALID;
    }
    
    if (node->type != VFS_FILE) {
        return VFS_ERR_IS_DIR;
    }
    
    fat12_file* file = (fat12_file*)node->fs_specific;
    if (!file) {
        return VFS_ERR_INVALID;
    }
    
    // For now, only support reading from start (offset must be 0)
    if (offset != 0) {
        printf("FAT12: Warning - offset %u not supported, reading from start\n", offset);
    }
    
    uint32_t bytes_to_read = (size < node->size) ? size : node->size;
    int bytes_read = fat12_read_file(file, buffer, size, bytes_to_read);
    
    if (bytes_read < 0) {
        return VFS_ERR_IO;
    }
    
    return bytes_read;
}

static int fat12_vfs_write(vfs_node_t* node, uint32_t offset, uint32_t size, const uint8_t* buffer) {
    // Write not implemented yet
    return VFS_ERR_UNSUPPORTED;
}

static int fat12_vfs_readdir(vfs_node_t* node, uint32_t index, vfs_dir_entry_t* entry) {
    if (!node || !entry) {
        return VFS_ERR_INVALID;
    }
    
    if (node->type != VFS_DIRECTORY) {
        return VFS_ERR_NOT_DIR;
    }
    
    // Read directory entries using existing FAT12 function
    directory_entry dir_entry;
    int result = fat12_read_dir_entries(&dir_entry);
    
    if (result < 0) {
        return VFS_ERR_IO;
    }
    
    // For now, we can only return one entry at a time
    // TODO: Improve this to support multiple entries properly
    if (index > 0) {
        return VFS_ERR_NOT_FOUND;
    }
    
    // Convert FAT12 entry to VFS entry
    directory_entry* fat_entry = &dir_entry;
    
    // Copy name (convert from 8.3 format)
    char name[13];
    int name_idx = 0;
    
    for (int i = 0; i < 8 && fat_entry->filename[i] != ' '; i++) {
        name[name_idx++] = fat_entry->filename[i];
    }
    
    if (fat_entry->extension[0] != ' ') {
        name[name_idx++] = '.';
        for (int i = 0; i < 3 && fat_entry->extension[i] != ' '; i++) {
            name[name_idx++] = fat_entry->extension[i];
        }
    }
    name[name_idx] = '\0';
    
    strncpy(entry->name, name, 255);
    entry->name[255] = '\0';
    
    entry->type = (fat_entry->attributes & FILE_ATTR_DIRECTORY) ? VFS_DIRECTORY : VFS_FILE;
    entry->size = fat_entry->file_size;
    entry->inode = fat_entry->first_cluster_low;
    entry->attributes = fat_entry->attributes;
    entry->create_time = 0;  // TODO: Convert FAT date/time
    entry->modify_time = 0;
    entry->access_time = 0;
    
    return VFS_OK;
}

static int fat12_vfs_finddir(vfs_node_t* node, const char* name, vfs_node_t** child) {
    return VFS_ERR_UNSUPPORTED;  // TODO: Implement
}

static int fat12_vfs_mkdir(vfs_filesystem_t* fs, const char* path) {
    return VFS_ERR_UNSUPPORTED;  // TODO: Implement
}

static int fat12_vfs_rmdir(vfs_filesystem_t* fs, const char* path) {
    return VFS_ERR_UNSUPPORTED;  // TODO: Implement
}

static int fat12_vfs_create(vfs_filesystem_t* fs, const char* path) {
    return VFS_ERR_UNSUPPORTED;  // TODO: Implement
}

static int fat12_vfs_delete(vfs_filesystem_t* fs, const char* path) {
    return VFS_ERR_UNSUPPORTED;  // TODO: Implement
}

static int fat12_vfs_stat(vfs_filesystem_t* fs, const char* path, vfs_dir_entry_t* stat) {
    // Open file and get info
    vfs_node_t* node;
    int result = fat12_vfs_open(fs, path, &node);
    if (result != VFS_OK) {
        return result;
    }
    
    strncpy(stat->name, node->name, 255);
    stat->name[255] = '\0';
    stat->type = node->type;
    stat->size = node->size;
    stat->inode = node->inode;
    
    fat12_vfs_close(node);
    return VFS_OK;
}

// ===========================================================================
// VFS Operations Table
// ===========================================================================

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
    .stat = fat12_vfs_stat
};

// ===========================================================================
// Registration Function
// ===========================================================================

void fat12_register_vfs(void) {
    vfs_register_filesystem("fat12", &fat12_vfs_ops);
}
