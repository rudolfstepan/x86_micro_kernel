#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <stdbool.h>
#include "drivers/bus/drives.h"

// Forward declarations
struct vfs_node;
struct vfs_filesystem;
struct vfs_file;
struct vfs_dir_entry;

// ===========================================================================
// VFS File Types
// ===========================================================================
typedef enum {
    VFS_FILE = 1,
    VFS_DIRECTORY = 2,
    VFS_CHARDEVICE = 3,
    VFS_BLOCKDEVICE = 4,
    VFS_PIPE = 5,
    VFS_SYMLINK = 6,
    VFS_MOUNTPOINT = 7
} vfs_node_type_t;

// ===========================================================================
// VFS File/Directory Entry
// ===========================================================================
typedef struct vfs_dir_entry {
    char name[256];              // Entry name
    vfs_node_type_t type;        // Entry type
    uint32_t size;               // File size
    uint32_t inode;              // Inode/cluster number
    uint32_t create_time;        // Creation time
    uint32_t modify_time;        // Modification time
    uint32_t access_time;        // Access time
    uint8_t attributes;          // File attributes
} vfs_dir_entry_t;

// ===========================================================================
// VFS Node (represents a file, directory, device, etc.)
// ===========================================================================
typedef struct vfs_node {
    char name[256];                          // Node name
    vfs_node_type_t type;                    // Node type
    uint32_t inode;                          // Inode number (or cluster for FAT)
    uint32_t size;                           // Size in bytes
    uint32_t flags;                          // Flags
    struct vfs_filesystem* fs;               // Pointer to filesystem
    void* fs_specific;                       // Filesystem-specific data
} vfs_node_t;

// ===========================================================================
// VFS Filesystem Operations (function pointers)
// ===========================================================================
typedef struct vfs_filesystem_ops {
    // Filesystem lifecycle
    int (*mount)(struct vfs_filesystem* fs, drive_t* drive);
    int (*unmount)(struct vfs_filesystem* fs);
    
    // File operations
    int (*open)(struct vfs_filesystem* fs, const char* path, vfs_node_t** node);
    int (*close)(vfs_node_t* node);
    int (*read)(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer);
    int (*write)(vfs_node_t* node, uint32_t offset, uint32_t size, const uint8_t* buffer);
    
    // Directory operations
    int (*readdir)(vfs_node_t* node, uint32_t index, vfs_dir_entry_t* entry);
    int (*finddir)(vfs_node_t* node, const char* name, vfs_node_t** child);
    int (*mkdir)(struct vfs_filesystem* fs, const char* path);
    int (*rmdir)(struct vfs_filesystem* fs, const char* path);
    
    // File management
    int (*create)(struct vfs_filesystem* fs, const char* path);
    int (*delete)(struct vfs_filesystem* fs, const char* path);
    int (*stat)(struct vfs_filesystem* fs, const char* path, vfs_dir_entry_t* stat);
} vfs_filesystem_ops_t;

// ===========================================================================
// VFS Filesystem Structure
// ===========================================================================
typedef struct vfs_filesystem {
    char name[32];                    // Filesystem type name (e.g., "fat32", "fat12")
    drive_t* drive;                   // Associated drive
    vfs_filesystem_ops_t* ops;        // Operations table
    void* fs_data;                    // Filesystem-specific data (boot sector, etc.)
    vfs_node_t* root;                 // Root directory node
} vfs_filesystem_t;

// ===========================================================================
// VFS Mount Point
// ===========================================================================
typedef struct vfs_mount {
    char path[256];                   // Mount path (e.g., "/", "/mnt/usb")
    vfs_filesystem_t* fs;             // Mounted filesystem
    struct vfs_mount* next;           // Next mount point
} vfs_mount_t;

// ===========================================================================
// VFS Error Codes
// ===========================================================================
#define VFS_OK              0
#define VFS_ERR_NOT_FOUND   -1
#define VFS_ERR_NO_MEMORY   -2
#define VFS_ERR_INVALID     -3
#define VFS_ERR_IO          -4
#define VFS_ERR_EXISTS      -5
#define VFS_ERR_NOT_DIR     -6
#define VFS_ERR_IS_DIR      -7
#define VFS_ERR_NO_SPACE    -8
#define VFS_ERR_READ_ONLY   -9
#define VFS_ERR_UNSUPPORTED -10

// ===========================================================================
// VFS Public API
// ===========================================================================

// Initialization
void vfs_init(void);

// Filesystem registration
int vfs_register_filesystem(const char* name, vfs_filesystem_ops_t* ops);

// Mount/unmount
int vfs_mount(drive_t* drive, const char* fs_type, const char* mount_path);
int vfs_unmount(const char* mount_path);

// File operations (path-based)
int vfs_open(const char* path, vfs_node_t** node);
int vfs_close(vfs_node_t* node);
int vfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer);
int vfs_write(vfs_node_t* node, uint32_t offset, uint32_t size, const uint8_t* buffer);

// Directory operations
int vfs_readdir(const char* path, uint32_t index, vfs_dir_entry_t* entry);
int vfs_mkdir(const char* path);
int vfs_rmdir(const char* path);

// File management
int vfs_create(const char* path);
int vfs_delete(const char* path);
int vfs_stat(const char* path, vfs_dir_entry_t* stat);

// Utility functions
vfs_filesystem_t* vfs_get_filesystem(const char* path);
const char* vfs_get_relative_path(const char* absolute_path, vfs_filesystem_t* fs);

#endif // VFS_H
