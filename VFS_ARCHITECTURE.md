# Virtual File System (VFS) Architecture

## Overview
The VFS provides a unified abstraction layer for multiple filesystem types, following professional OS design patterns (similar to Linux VFS).

## Architecture Layers

```
┌─────────────────────────────────────────────────────────┐
│  User Applications (shell commands, programs)           │
└────────────────────┬────────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────────┐
│  VFS Layer (vfs.c/vfs.h)                                │
│  - Path resolution                                      │
│  - Mount point management                               │
│  - Generic file/dir operations                          │
│  - Filesystem registration                              │
└────┬──────────────┬──────────────┬─────────────────────┘
     │              │              │
┌────▼──────┐  ┌───▼──────┐  ┌───▼──────┐
│  FAT32    │  │  FAT12   │  │  Future  │
│  Adapter  │  │  Adapter │  │  (ext2)  │
└────┬──────┘  └───┬──────┘  └───┬──────┘
     │             │              │
┌────▼─────────────▼──────────────▼───────────────────────┐
│  Hardware Drivers (ATA, FDD, etc.)                      │
└─────────────────────────────────────────────────────────┘
```

## Key Components

### 1. VFS Core (`vfs.h`, `vfs.c`)
- **Filesystem Registration**: `vfs_register_filesystem()`
- **Mount Management**: `vfs_mount()`, `vfs_unmount()`
- **Path Resolution**: Finds correct filesystem for absolute paths
- **Generic Operations**: Unified API for all filesystems

### 2. Filesystem Operations Table (`vfs_filesystem_ops_t`)
Each filesystem implements these operations:
- **mount/unmount**: Initialize/cleanup filesystem
- **open/close/read/write**: File operations
- **readdir/finddir**: Directory traversal
- **mkdir/rmdir**: Directory management
- **create/delete/stat**: File management

### 3. VFS Node (`vfs_node_t`)
Represents any filesystem object (file, directory, device):
- Type (file/directory/device)
- Inode/cluster number
- Size, flags
- Pointer to owning filesystem
- Filesystem-specific data

### 4. Mount Points (`vfs_mount_t`)
Maps paths to filesystems:
- `/` → hdd0 (FAT32)
- `/mnt/floppy` → fdd0 (FAT12)
- `/mnt/usb` → Future USB drive

## Usage Example

### Registering a Filesystem
```c
// In fat32_adapter.c
vfs_filesystem_ops_t fat32_ops = {
    .mount = fat32_vfs_mount,
    .open = fat32_vfs_open,
    .read = fat32_vfs_read,
    // ... other operations
};

// At boot
vfs_register_filesystem("fat32", &fat32_ops);
```

### Mounting a Drive
```c
drive_t* drive = get_drive_by_name("hdd0");
vfs_mount(drive, "fat32", "/");  // Mount as root
```

### Using VFS Operations
```c
// Open file (VFS automatically resolves path to correct filesystem)
vfs_node_t* node;
if (vfs_open("/readme.txt", &node) == VFS_OK) {
    uint8_t buffer[256];
    vfs_read(node, 0, 256, buffer);
    vfs_close(node);
}

// List directory
vfs_dir_entry_t entry;
int index = 0;
while (vfs_readdir("/docs", index++, &entry) == VFS_OK) {
    printf("%s\n", entry.name);
}
```

## Benefits

### 1. **Separation of Concerns**
- VFS: Path resolution, mount management
- Filesystem: Only filesystem-specific logic

### 2. **Extensibility**
- Add new filesystems by implementing ops table
- No changes to VFS core needed

### 3. **Consistency**
- All filesystems use same error codes
- Unified API for applications

### 4. **Professional Design**
- Similar to Linux VFS, BSD VFS
- Industry-standard architecture

## Implementation Status

### Phase 1: VFS Core ✅
- [x] VFS initialization
- [x] Filesystem registration
- [x] Mount/unmount
- [x] Path resolution
- [x] Generic operations API

### Phase 2: FAT32 Adapter (TODO)
- [ ] Implement `fat32_vfs_mount()`
- [ ] Implement `fat32_vfs_open()`
- [ ] Implement `fat32_vfs_read()`
- [ ] Implement `fat32_vfs_readdir()`
- [ ] Wrap existing FAT32 functions

### Phase 3: FAT12 Adapter (TODO)
- [ ] Implement FAT12 VFS operations
- [ ] Integrate with existing code

### Phase 4: Integration (TODO)
- [ ] Update shell commands to use VFS
- [ ] Update filesystem.c to use VFS
- [ ] Test multi-filesystem mounting

## Error Handling
VFS uses consistent error codes:
- `VFS_OK` (0): Success
- `VFS_ERR_NOT_FOUND` (-1): File/dir not found
- `VFS_ERR_NO_MEMORY` (-2): Allocation failed
- `VFS_ERR_INVALID` (-3): Invalid parameters
- `VFS_ERR_IO` (-4): I/O error
- `VFS_ERR_UNSUPPORTED` (-10): Operation not supported

## Next Steps
1. Create FAT32 adapter (`fat32_vfs_adapter.c`)
2. Create FAT12 adapter (`fat12_vfs_adapter.c`)
3. Update shell commands to use VFS API
4. Test mounting multiple drives with different filesystems
