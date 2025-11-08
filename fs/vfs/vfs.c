#include "vfs.h"
#include "lib/libc/string.h"
#include "lib/libc/stdio.h"
#include "lib/libc/stdlib.h"

// ===========================================================================
// VFS Internal State
// ===========================================================================

#define MAX_FILESYSTEMS 10
#define MAX_MOUNTS 10

typedef struct {
    char name[32];
    vfs_filesystem_ops_t* ops;
    bool registered;
} fs_registration_t;

static fs_registration_t registered_filesystems[MAX_FILESYSTEMS];
static vfs_mount_t* mount_list = NULL;
static int fs_count = 0;

// ===========================================================================
// VFS Initialization
// ===========================================================================

void vfs_init(void) {
    printf("VFS: Initializing Virtual File System...\n");
    
    // Clear registration table
    for (int i = 0; i < MAX_FILESYSTEMS; i++) {
        registered_filesystems[i].registered = false;
    }
    
    mount_list = NULL;
    fs_count = 0;
    
    printf("VFS: Initialization complete.\n");
}

// ===========================================================================
// Filesystem Registration
// ===========================================================================

int vfs_register_filesystem(const char* name, vfs_filesystem_ops_t* ops) {
    if (fs_count >= MAX_FILESYSTEMS) {
        printf("VFS: Error - maximum filesystems registered.\n");
        return VFS_ERR_NO_MEMORY;
    }
    
    if (!name || !ops) {
        printf("VFS: Error - invalid parameters.\n");
        return VFS_ERR_INVALID;
    }
    
    // Check if already registered
    for (int i = 0; i < MAX_FILESYSTEMS; i++) {
        if (registered_filesystems[i].registered &&
            strcmp(registered_filesystems[i].name, name) == 0) {
            printf("VFS: Filesystem '%s' already registered.\n", name);
            return VFS_ERR_EXISTS;
        }
    }
    
    // Find free slot
    for (int i = 0; i < MAX_FILESYSTEMS; i++) {
        if (!registered_filesystems[i].registered) {
            strncpy(registered_filesystems[i].name, name, 31);
            registered_filesystems[i].name[31] = '\0';
            registered_filesystems[i].ops = ops;
            registered_filesystems[i].registered = true;
            fs_count++;
            printf("VFS: Registered filesystem '%s'\n", name);
            return VFS_OK;
        }
    }
    
    return VFS_ERR_NO_MEMORY;
}

// ===========================================================================
// Mount/Unmount Operations
// ===========================================================================

int vfs_mount(drive_t* drive, const char* fs_type, const char* mount_path) {
    if (!drive || !fs_type || !mount_path) {
        return VFS_ERR_INVALID;
    }
    
    // Find registered filesystem
    vfs_filesystem_ops_t* ops = NULL;
    for (int i = 0; i < MAX_FILESYSTEMS; i++) {
        if (registered_filesystems[i].registered &&
            strcmp(registered_filesystems[i].name, fs_type) == 0) {
            ops = registered_filesystems[i].ops;
            break;
        }
    }
    
    if (!ops) {
        return VFS_ERR_UNSUPPORTED;
    }
    
    // Allocate filesystem structure
    vfs_filesystem_t* fs = (vfs_filesystem_t*)malloc(sizeof(vfs_filesystem_t));
    if (!fs) {
        return VFS_ERR_NO_MEMORY;
    }
    
    strncpy(fs->name, fs_type, 31);
    fs->name[31] = '\0';
    fs->drive = drive;
    fs->ops = ops;
    fs->fs_data = NULL;
    fs->root = NULL;
    
    // Call filesystem-specific mount
    int result = ops->mount(fs, drive);
    if (result != VFS_OK) {
        free(fs);
        return result;
    }
    
    // Create mount point
    vfs_mount_t* mount = (vfs_mount_t*)malloc(sizeof(vfs_mount_t));
    if (!mount) {
        ops->unmount(fs);
        free(fs);
        return VFS_ERR_NO_MEMORY;
    }
    
    strncpy(mount->path, mount_path, 255);
    mount->path[255] = '\0';
    mount->fs = fs;
    mount->next = mount_list;
    mount_list = mount;
    
    printf("VFS: Successfully mounted %s at %s\n", drive->name, mount_path);
    return VFS_OK;
}

int vfs_unmount(const char* mount_path) {
    if (!mount_path) {
        return VFS_ERR_INVALID;
    }
    
    vfs_mount_t** current = &mount_list;
    while (*current) {
        if (strcmp((*current)->path, mount_path) == 0) {
            vfs_mount_t* to_remove = *current;
            
            // Unmount filesystem
            if (to_remove->fs->ops->unmount) {
                to_remove->fs->ops->unmount(to_remove->fs);
            }
            
            // Remove from list
            *current = to_remove->next;
            free(to_remove->fs);
            free(to_remove);
            
            printf("VFS: Unmounted %s\n", mount_path);
            return VFS_OK;
        }
        current = &(*current)->next;
    }
    
    return VFS_ERR_NOT_FOUND;
}

// ===========================================================================
// Path Resolution
// ===========================================================================

vfs_filesystem_t* vfs_get_filesystem(const char* path) {
    if (!path || path[0] != '/') {
        return NULL;
    }
    
    // Find longest matching mount point
    vfs_mount_t* best_match = NULL;
    size_t best_match_len = 0;
    
    vfs_mount_t* current = mount_list;
    while (current) {
        size_t mount_len = strlen(current->path);
        if (strncmp(path, current->path, mount_len) == 0) {
            if (mount_len > best_match_len) {
                best_match = current;
                best_match_len = mount_len;
            }
        }
        current = current->next;
    }
    
    return best_match ? best_match->fs : NULL;
}

const char* vfs_get_relative_path(const char* absolute_path, vfs_filesystem_t* fs) {
    if (!absolute_path || !fs) {
        return NULL;
    }
    
    // Find the mount point for this filesystem
    vfs_mount_t* current = mount_list;
    while (current) {
        if (current->fs == fs) {
            size_t mount_len = strlen(current->path);
            if (strncmp(absolute_path, current->path, mount_len) == 0) {
                const char* relative = absolute_path + mount_len;
                return (*relative == '\0') ? "/" : relative;
            }
        }
        current = current->next;
    }
    
    return absolute_path;
}

// ===========================================================================
// File Operations
// ===========================================================================

int vfs_open(const char* path, vfs_node_t** node) {
    if (!path || !node) {
        return VFS_ERR_INVALID;
    }
    
    vfs_filesystem_t* fs = vfs_get_filesystem(path);
    if (!fs) {
        return VFS_ERR_NOT_FOUND;
    }
    
    const char* relative_path = vfs_get_relative_path(path, fs);
    if (!fs->ops->open) {
        return VFS_ERR_UNSUPPORTED;
    }
    
    return fs->ops->open(fs, relative_path, node);
}

int vfs_close(vfs_node_t* node) {
    if (!node || !node->fs) {
        return VFS_ERR_INVALID;
    }
    
    if (!node->fs->ops->close) {
        return VFS_ERR_UNSUPPORTED;
    }
    
    return node->fs->ops->close(node);
}

int vfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    if (!node || !node->fs || !buffer) {
        return VFS_ERR_INVALID;
    }
    
    if (!node->fs->ops->read) {
        return VFS_ERR_UNSUPPORTED;
    }
    
    return node->fs->ops->read(node, offset, size, buffer);
}

int vfs_write(vfs_node_t* node, uint32_t offset, uint32_t size, const uint8_t* buffer) {
    if (!node || !node->fs || !buffer) {
        return VFS_ERR_INVALID;
    }
    
    if (!node->fs->ops->write) {
        return VFS_ERR_READ_ONLY;
    }
    
    return node->fs->ops->write(node, offset, size, buffer);
}

// ===========================================================================
// Directory Operations
// ===========================================================================

int vfs_readdir(const char* path, uint32_t index, vfs_dir_entry_t* entry) {
    if (!path || !entry) {
        return VFS_ERR_INVALID;
    }
    
    vfs_filesystem_t* fs = vfs_get_filesystem(path);
    if (!fs) {
        return VFS_ERR_NOT_FOUND;
    }
    
    // Open directory node
    vfs_node_t* dir_node;
    const char* relative_path = vfs_get_relative_path(path, fs);
    
    if (!fs->ops->open) {
        return VFS_ERR_UNSUPPORTED;
    }
    
    int result = fs->ops->open(fs, relative_path, &dir_node);
    if (result != VFS_OK) {
        return result;
    }
    
    if (dir_node->type != VFS_DIRECTORY) {
        if (fs->ops->close) {
            fs->ops->close(dir_node);
        }
        return VFS_ERR_NOT_DIR;
    }
    
    if (!fs->ops->readdir) {
        if (fs->ops->close) {
            fs->ops->close(dir_node);
        }
        return VFS_ERR_UNSUPPORTED;
    }
    
    result = fs->ops->readdir(dir_node, index, entry);
    
    if (fs->ops->close) {
        fs->ops->close(dir_node);
    }
    
    return result;
}

int vfs_mkdir(const char* path) {
    if (!path) {
        return VFS_ERR_INVALID;
    }
    
    vfs_filesystem_t* fs = vfs_get_filesystem(path);
    if (!fs) {
        return VFS_ERR_NOT_FOUND;
    }
    
    const char* relative_path = vfs_get_relative_path(path, fs);
    if (!fs->ops->mkdir) {
        return VFS_ERR_UNSUPPORTED;
    }
    
    return fs->ops->mkdir(fs, relative_path);
}

int vfs_rmdir(const char* path) {
    if (!path) {
        return VFS_ERR_INVALID;
    }
    
    vfs_filesystem_t* fs = vfs_get_filesystem(path);
    if (!fs) {
        return VFS_ERR_NOT_FOUND;
    }
    
    const char* relative_path = vfs_get_relative_path(path, fs);
    if (!fs->ops->rmdir) {
        return VFS_ERR_UNSUPPORTED;
    }
    
    return fs->ops->rmdir(fs, relative_path);
}

// ===========================================================================
// File Management
// ===========================================================================

int vfs_create(const char* path) {
    if (!path) {
        return VFS_ERR_INVALID;
    }
    
    vfs_filesystem_t* fs = vfs_get_filesystem(path);
    if (!fs) {
        return VFS_ERR_NOT_FOUND;
    }
    
    const char* relative_path = vfs_get_relative_path(path, fs);
    if (!fs->ops->create) {
        return VFS_ERR_UNSUPPORTED;
    }
    
    return fs->ops->create(fs, relative_path);
}

int vfs_delete(const char* path) {
    if (!path) {
        return VFS_ERR_INVALID;
    }
    
    vfs_filesystem_t* fs = vfs_get_filesystem(path);
    if (!fs) {
        return VFS_ERR_NOT_FOUND;
    }
    
    const char* relative_path = vfs_get_relative_path(path, fs);
    if (!fs->ops->delete) {
        return VFS_ERR_UNSUPPORTED;
    }
    
    return fs->ops->delete(fs, relative_path);
}

int vfs_stat(const char* path, vfs_dir_entry_t* stat) {
    if (!path || !stat) {
        return VFS_ERR_INVALID;
    }
    
    vfs_filesystem_t* fs = vfs_get_filesystem(path);
    if (!fs) {
        return VFS_ERR_NOT_FOUND;
    }
    
    const char* relative_path = vfs_get_relative_path(path, fs);
    if (!fs->ops->stat) {
        return VFS_ERR_UNSUPPORTED;
    }
    
    return fs->ops->stat(fs, relative_path, stat);
}
