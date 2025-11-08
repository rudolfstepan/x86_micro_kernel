#include "ext2.h"
#include "fs/vfs/vfs.h"
#include "lib/libc/stdio.h"
#include "lib/libc/string.h"
#include "lib/libc/stdlib.h"

// Global EXT2 filesystem instance
static ext2_fs_t* g_ext2_fs = NULL;

// ===========================================================================
// Helper Functions
// ===========================================================================

static void ext2_entry_to_vfs_entry(const ext2_dir_entry_t* ext2_entry, vfs_dir_entry_t* vfs_entry) {
    vfs_entry->inode = ext2_entry->inode;
    
    // Copy name (ensure null-terminated)
    uint32_t name_len = ext2_entry->name_len;
    if (name_len > 255) {
        name_len = 255;
    }
    memcpy(vfs_entry->name, ext2_entry->name, name_len);
    vfs_entry->name[name_len] = '\0';
    
    // Convert file type
    switch (ext2_entry->file_type) {
        case EXT2_FT_REG_FILE:
            vfs_entry->type = VFS_FILE;
            break;
        case EXT2_FT_DIR:
            vfs_entry->type = VFS_DIRECTORY;
            break;
        case EXT2_FT_SYMLINK:
            vfs_entry->type = VFS_SYMLINK;
            break;
        default:
            vfs_entry->type = VFS_FILE;
            break;
    }
    
    vfs_entry->size = 0;  // Will be filled by caller if needed
}

// ===========================================================================
// VFS Operations Implementation
// ===========================================================================

static int ext2_vfs_mount(vfs_filesystem_t* fs, drive_t* driver) {
    if (!fs || !driver) {
        return VFS_ERR_INVALID;
    }
    
    drive_t* drive = driver;
    
    // Allocate filesystem structure
    g_ext2_fs = (ext2_fs_t*)malloc(sizeof(ext2_fs_t));
    if (!g_ext2_fs) {
        return VFS_ERR_NO_MEMORY;
    }
    
    memset(g_ext2_fs, 0, sizeof(ext2_fs_t));
    
    // Initialize EXT2
    if (!ext2_init(g_ext2_fs, drive->base, drive->is_master)) {
        free(g_ext2_fs);
        g_ext2_fs = NULL;
        return VFS_ERR_IO;
    }
    
    // Store filesystem pointer
    fs->fs_data = g_ext2_fs;
    
    return VFS_OK;
}

static int ext2_vfs_unmount(vfs_filesystem_t* fs) {
    if (!fs || !fs->fs_data) {
        return VFS_ERR_INVALID;
    }
    
    ext2_fs_t* ext2_fs = (ext2_fs_t*)fs->fs_data;
    ext2_cleanup(ext2_fs);
    free(ext2_fs);
    fs->fs_data = NULL;
    
    return VFS_OK;
}

static int ext2_vfs_open(vfs_filesystem_t* fs, const char* path, vfs_node_t** node) {
    if (!fs || !fs->fs_data || !path || !node) {
        return VFS_ERR_INVALID;
    }
    
    ext2_fs_t* ext2_fs = (ext2_fs_t*)fs->fs_data;
    
    // Handle root directory
    if (strcmp(path, "/") == 0) {
        vfs_node_t* root = (vfs_node_t*)malloc(sizeof(vfs_node_t));
        if (!root) {
            return VFS_ERR_NO_MEMORY;
        }
        
        memset(root, 0, sizeof(vfs_node_t));
        strcpy(root->name, "/");
        root->inode = EXT2_ROOT_INO;
        root->type = VFS_DIRECTORY;
        root->size = 0;
        root->fs = fs;
        
        *node = root;
        return VFS_OK;
    }
    
    // Parse path (skip leading /)
    const char* filename = path;
    if (*filename == '/') {
        filename++;
    }
    
    // Find entry in root directory
    ext2_dir_entry_t entry;
    if (!ext2_find_entry(ext2_fs, EXT2_ROOT_INO, filename, &entry)) {
        printf("EXT2_VFS: File not found: %s\n", filename);
        return VFS_ERR_NOT_FOUND;
    }
    
    // Read inode
    ext2_inode_t inode;
    if (!ext2_read_inode(ext2_fs, entry.inode, &inode)) {
        printf("EXT2_VFS: Failed to read inode %u\n", entry.inode);
        return VFS_ERR_IO;
    }
    
    // Create VFS node
    vfs_node_t* new_node = (vfs_node_t*)malloc(sizeof(vfs_node_t));
    if (!new_node) {
        return VFS_ERR_NO_MEMORY;
    }
    
    memset(new_node, 0, sizeof(vfs_node_t));
    
    // Copy name
    uint32_t name_len = entry.name_len;
    if (name_len > 255) {
        name_len = 255;
    }
    memcpy(new_node->name, entry.name, name_len);
    new_node->name[name_len] = '\0';
    
    new_node->inode = entry.inode;
    new_node->size = inode.i_size;
    new_node->fs = fs;
    
    // Determine type
    if ((inode.i_mode & EXT2_S_IFDIR) == EXT2_S_IFDIR) {
        new_node->type = VFS_DIRECTORY;
    } else if ((inode.i_mode & EXT2_S_IFREG) == EXT2_S_IFREG) {
        new_node->type = VFS_FILE;
    } else if ((inode.i_mode & EXT2_S_IFLNK) == EXT2_S_IFLNK) {
        new_node->type = VFS_SYMLINK;
    } else {
        new_node->type = VFS_FILE;
    }
    
    *node = new_node;
    return VFS_OK;
}

static int ext2_vfs_close(vfs_node_t* node) {
    if (!node) {
        return VFS_ERR_INVALID;
    }
    
    free(node);
    return VFS_OK;
}

static int ext2_vfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    if (!node || !node->fs || !node->fs->fs_data || !buffer) {
        return VFS_ERR_INVALID;
    }
    
    ext2_fs_t* ext2_fs = (ext2_fs_t*)node->fs->fs_data;
    
    // Read inode
    ext2_inode_t inode;
    if (!ext2_read_inode(ext2_fs, node->inode, &inode)) {
        return VFS_ERR_IO;
    }
    
    // Read file data
    int result = ext2_read_file(ext2_fs, &inode, offset, size, buffer);
    if (result < 0) {
        return VFS_ERR_IO;
    }
    
    return result;
}

static int ext2_vfs_write(vfs_node_t* node, uint32_t offset, uint32_t size, const uint8_t* buffer) {
    // Not implemented yet
    return VFS_ERR_UNSUPPORTED;
}

static int ext2_vfs_readdir(vfs_node_t* node, uint32_t index, vfs_dir_entry_t* entry) {
    if (!node || !node->fs || !node->fs->fs_data || !entry) {
        printf("EXT2_VFS: readdir - invalid parameters\n");
        return VFS_ERR_INVALID;
    }
    
    if (node->type != VFS_DIRECTORY) {
        printf("EXT2_VFS: readdir - not a directory\n");
        return VFS_ERR_NOT_DIR;
    }
    
    ext2_fs_t* ext2_fs = (ext2_fs_t*)node->fs->fs_data;
    
    printf("EXT2_VFS: readdir - reading inode %u, index %u\n", node->inode, index);
    
    // Read directory entries
    ext2_dir_entry_t ext2_entries[64];
    uint32_t ext2_count = 0;
    
    if (!ext2_read_dir(ext2_fs, node->inode, ext2_entries, 64, &ext2_count)) {
        printf("EXT2_VFS: readdir - ext2_read_dir failed\n");
        return VFS_ERR_IO;
    }
    
    printf("EXT2_VFS: readdir - found %u entries\n", ext2_count);
    
    // Find entry at index (skip deleted/null entries)
    uint32_t current_index = 0;
    for (uint32_t i = 0; i < ext2_count; i++) {
        if (ext2_entries[i].inode == 0) {
            continue;
        }
        
        if (current_index == index) {
            // Found the entry
            ext2_entry_to_vfs_entry(&ext2_entries[i], entry);
            
            // Read inode to get file size
            ext2_inode_t inode;
            if (ext2_read_inode(ext2_fs, ext2_entries[i].inode, &inode)) {
                entry->size = inode.i_size;
            }
            
            return VFS_OK;
        }
        
        current_index++;
    }
    
    // Index out of range
    return VFS_ERR_NOT_FOUND;
}

static int ext2_vfs_finddir(vfs_node_t* node, const char* name, vfs_node_t** child) {
    if (!node || !name || !child || !node->fs || !node->fs->fs_data) {
        return VFS_ERR_INVALID;
    }
    
    if (node->type != VFS_DIRECTORY) {
        return VFS_ERR_NOT_DIR;
    }
    
    ext2_fs_t* ext2_fs = (ext2_fs_t*)node->fs->fs_data;
    
    // Find entry
    ext2_dir_entry_t entry;
    if (!ext2_find_entry(ext2_fs, node->inode, name, &entry)) {
        return VFS_ERR_NOT_FOUND;
    }
    
    // Read inode
    ext2_inode_t inode;
    if (!ext2_read_inode(ext2_fs, entry.inode, &inode)) {
        return VFS_ERR_IO;
    }
    
    // Create VFS node
    vfs_node_t* new_node = (vfs_node_t*)malloc(sizeof(vfs_node_t));
    if (!new_node) {
        return VFS_ERR_NO_MEMORY;
    }
    
    memset(new_node, 0, sizeof(vfs_node_t));
    
    // Copy name
    uint32_t name_len = entry.name_len;
    if (name_len > 255) {
        name_len = 255;
    }
    memcpy(new_node->name, entry.name, name_len);
    new_node->name[name_len] = '\0';
    
    new_node->inode = entry.inode;
    new_node->size = inode.i_size;
    new_node->fs = node->fs;
    
    // Determine type
    if ((inode.i_mode & EXT2_S_IFDIR) == EXT2_S_IFDIR) {
        new_node->type = VFS_DIRECTORY;
    } else if ((inode.i_mode & EXT2_S_IFREG) == EXT2_S_IFREG) {
        new_node->type = VFS_FILE;
    } else if ((inode.i_mode & EXT2_S_IFLNK) == EXT2_S_IFLNK) {
        new_node->type = VFS_SYMLINK;
    } else {
        new_node->type = VFS_FILE;
    }
    
    *child = new_node;
    return VFS_OK;
}

static int ext2_vfs_mkdir(vfs_filesystem_t* fs, const char* path) {
    // Not implemented yet
    return VFS_ERR_UNSUPPORTED;
}

static int ext2_vfs_rmdir(vfs_filesystem_t* fs, const char* path) {
    // Not implemented yet
    return VFS_ERR_UNSUPPORTED;
}

static int ext2_vfs_create(vfs_filesystem_t* fs, const char* path) {
    // Not implemented yet
    return VFS_ERR_UNSUPPORTED;
}

static int ext2_vfs_delete(vfs_filesystem_t* fs, const char* path) {
    // Not implemented yet
    return VFS_ERR_UNSUPPORTED;
}

static int ext2_vfs_stat(vfs_filesystem_t* fs, const char* path, vfs_dir_entry_t* stat) {
    if (!fs || !fs->fs_data || !path || !stat) {
        return VFS_ERR_INVALID;
    }
    
    ext2_fs_t* ext2_fs = (ext2_fs_t*)fs->fs_data;
    
    // Parse path
    const char* filename = path;
    if (*filename == '/') {
        filename++;
    }
    
    // Handle root
    if (*filename == '\0') {
        strcpy(stat->name, "/");
        stat->inode = EXT2_ROOT_INO;
        stat->type = VFS_DIRECTORY;
        stat->size = 0;
        return VFS_OK;
    }
    
    // Find entry
    ext2_dir_entry_t entry;
    if (!ext2_find_entry(ext2_fs, EXT2_ROOT_INO, filename, &entry)) {
        return VFS_ERR_NOT_FOUND;
    }
    
    // Read inode
    ext2_inode_t inode;
    if (!ext2_read_inode(ext2_fs, entry.inode, &inode)) {
        return VFS_ERR_IO;
    }
    
    // Copy name
    uint32_t name_len = entry.name_len;
    if (name_len > 255) {
        name_len = 255;
    }
    memcpy(stat->name, entry.name, name_len);
    stat->name[name_len] = '\0';
    
    stat->inode = entry.inode;
    stat->size = inode.i_size;
    
    if ((inode.i_mode & EXT2_S_IFDIR) == EXT2_S_IFDIR) {
        stat->type = VFS_DIRECTORY;
    } else if ((inode.i_mode & EXT2_S_IFREG) == EXT2_S_IFREG) {
        stat->type = VFS_FILE;
    } else {
        stat->type = VFS_FILE;
    }
    
    return VFS_OK;
}

// ===========================================================================
// VFS Operations Table
// ===========================================================================

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

// ===========================================================================
// Registration Function
// ===========================================================================

bool ext2_register_vfs(void) {
    printf("EXT2_VFS: Registering filesystem\n");
    return vfs_register_filesystem("ext2", &ext2_vfs_ops) == VFS_OK;
}
