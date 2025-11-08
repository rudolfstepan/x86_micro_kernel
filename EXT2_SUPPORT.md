# EXT2 Filesystem Support

## Overview
The x86 microkernel now supports the EXT2 (Second Extended Filesystem) in addition to FAT32 and FAT12. EXT2 is a Linux-native filesystem that provides better performance and features than FAT filesystems.

## Architecture

### VFS Integration
EXT2 is fully integrated into the Virtual File System (VFS) layer:

```
VFS Layer
  ├── FAT32 Adapter    (HDD/USB support)
  ├── FAT12 Adapter    (Floppy support)
  └── EXT2 Adapter     (Linux filesystem support)
```

### Files Structure
```
fs/ext2/
├── ext2.h              - EXT2 structures and constants
├── ext2.c              - Core EXT2 implementation
└── ext2_vfs_adapter.c  - VFS integration layer
```

## EXT2 Implementation

### Supported Features
- ✅ Superblock reading and validation
- ✅ Block group descriptor table
- ✅ Inode reading and parsing
- ✅ Directory traversal
- ✅ File reading (direct blocks)
- ✅ Root directory access
- ✅ File/directory type detection
- ✅ Automatic filesystem detection

### Not Yet Supported
- ❌ Indirect blocks (files > 48KB)
- ❌ Double/triple indirect blocks
- ❌ Writing files
- ❌ Creating directories
- ❌ Symbolic links
- ❌ Extended attributes

### Data Structures

#### Superblock (1024 bytes at offset 1024)
```c
typedef struct {
    uint32_t s_inodes_count;      // Total inodes
    uint32_t s_blocks_count;      // Total blocks
    uint32_t s_r_blocks_count;    // Reserved blocks
    uint32_t s_free_blocks_count; // Free blocks
    uint32_t s_free_inodes_count; // Free inodes
    uint32_t s_first_data_block;  // First data block
    uint32_t s_log_block_size;    // Block size = 1024 << s_log_block_size
    // ... more fields
    uint16_t s_magic;             // Magic signature (0xEF53)
} ext2_superblock_t;
```

#### Inode (128 bytes)
```c
typedef struct {
    uint16_t i_mode;          // File mode (permissions + type)
    uint16_t i_uid;           // User ID
    uint32_t i_size;          // File size in bytes
    uint32_t i_atime;         // Access time
    uint32_t i_ctime;         // Creation time
    uint32_t i_mtime;         // Modification time
    uint32_t i_dtime;         // Deletion time
    uint16_t i_gid;           // Group ID
    uint16_t i_links_count;   // Hard links count
    uint32_t i_blocks;        // Blocks count
    uint32_t i_flags;         // File flags
    uint32_t i_block[15];     // Block pointers (12 direct, 1 indirect, 1 double indirect, 1 triple indirect)
} ext2_inode_t;
```

#### Directory Entry (variable length)
```c
typedef struct {
    uint32_t inode;       // Inode number
    uint16_t rec_len;     // Entry length
    uint8_t  name_len;    // Name length
    uint8_t  file_type;   // File type
    char     name[255];   // Filename
} ext2_dir_entry_t;
```

## Creating EXT2 Disk Images

### Using the Script
```bash
sudo ./scripts/create_ext2_disk.sh
```

This creates `ext2_disk.img` (10MB) with:
- `/bin/` - Binary directory
- `/etc/` - Configuration files
- `/usr/` - User programs
- `/home/` - Home directory
- `/tmp/` - Temporary files
- Test files: `readme.txt`, `welcome.txt`

### Manual Creation
```bash
# Create 10MB image
dd if=/dev/zero of=ext2_disk.img bs=1M count=10

# Format with EXT2
mkfs.ext2 -F ext2_disk.img

# Mount and add files
sudo mount -o loop ext2_disk.img /mnt
sudo mkdir -p /mnt/test
sudo cp myfiles/* /mnt/test/
sudo umount /mnt
```

### Custom Size
Edit `DISK_SIZE` variable in script:
```bash
DISK_SIZE="20M"  # 20 megabytes
DISK_SIZE="100M" # 100 megabytes
```

## Usage in Kernel

### Attaching to QEMU

#### Single EXT2 Drive
```bash
qemu-system-i386 -cdrom kernel.iso -drive file=ext2_disk.img,format=raw,if=ide
```

#### Multiple Drives (FAT32 + EXT2)
```bash
qemu-system-i386 -cdrom kernel.iso \
  -drive file=disk.vmdk,format=vmdk,if=ide \
  -drive file=ext2_disk.img,format=raw,if=ide
```

### Shell Commands
```
# List files on EXT2 drive
ls /mnt/ext2disk

# Read EXT2 file
cat /mnt/ext2disk/readme.txt

# Change to EXT2 drive
cd ext2disk

# List current directory
ls
```

### Automatic Detection
The kernel automatically detects EXT2 filesystems by reading the superblock magic number (0xEF53) at offset 1024+56.

## Implementation Details

### Initialization Flow
1. **VFS Registration**: `ext2_register_vfs()` registers EXT2 with VFS
2. **Filesystem Detection**: Read sectors 2-3 and check magic at offset 56
3. **Mount**: `ext2_vfs_mount()` calls `ext2_init()`
4. **Superblock**: Read and validate superblock at byte 1024
5. **Group Descriptors**: Load block group descriptor table
6. **Ready**: Filesystem ready for operations

### Reading Files
```c
// VFS layer call
vfs_node_t* node;
vfs_open(fs, "/readme.txt", &node);

uint8_t buffer[1024];
uint32_t bytes_read;
vfs_read(node, 0, 1024, buffer);

// Internally:
// 1. ext2_vfs_open() finds file in root directory
// 2. Reads inode to get file size and block pointers
// 3. ext2_vfs_read() reads data from direct blocks
// 4. Returns data to caller
```

### Directory Traversal
```c
// List directory entries
vfs_dir_entry_t entries[64];
for (uint32_t i = 0; ; i++) {
    vfs_dir_entry_t entry;
    int result = vfs_readdir(node, i, &entry);
    if (result != VFS_OK) break;
    
    printf("%s (%s)\n", entry.name, 
           entry.type == VFS_DIRECTORY ? "DIR" : "FILE");
}
```

## Limitations

### Current Limitations
1. **File Size**: Maximum 48KB (12 direct blocks × 4KB)
   - Files larger require indirect block support
2. **Read-Only**: No write operations yet
3. **Root Only**: Only root directory supported (no subdirectories yet)
4. **No Symbolic Links**: Symlinks not implemented
5. **Block Size**: Assumes 4KB blocks (configurable in superblock but not tested)

### Performance Considerations
- Each file read requires:
  - 1 inode read (128 bytes from inode table)
  - N block reads (where N = file_size / block_size)
- Directory listing reads entire directory in one pass
- No caching (yet)

## Testing

### Verify EXT2 Support
```bash
# Build kernel
make clean && make all

# Create test disk
sudo ./scripts/create_ext2_disk.sh

# Run with EXT2 disk
qemu-system-i386 -cdrom kernel.iso -drive file=ext2_disk.img,format=raw,if=ide

# At kernel shell:
drives           # Should show ext2disk
cd ext2disk      # Switch to EXT2
ls               # List files
cat readme.txt   # Read file
```

### Expected Output
```
EXT2: Initializing filesystem (base=0x170, master=0)
EXT2: Block size: 4096 bytes
EXT2: Total inodes: 2560
EXT2: Total blocks: 2560
EXT2: Inodes per group: 2560
EXT2: Blocks per group: 8192
EXT2: Number of block groups: 1
EXT2: Initialization successful
VFS: Mounted ext2disk at /mnt/ext2disk
```

## Future Enhancements

### Planned Features
1. **Indirect Blocks**: Support files > 48KB
2. **Write Support**: File creation and modification
3. **Directory Operations**: mkdir, rmdir
4. **Subdirectory Navigation**: Full path support
5. **Symbolic Links**: Follow and create symlinks
6. **Extended Attributes**: xattr support
7. **Journaling**: EXT3 compatibility
8. **Performance**: Block caching

### EXT3/EXT4 Migration
Current implementation uses compatible structures that can be extended:
- EXT3: Add journal support (s_feature_compat & 0x4)
- EXT4: Support 48-bit block numbers, extents

## Troubleshooting

### "Invalid magic number" Error
- Verify image is formatted as EXT2: `file ext2_disk.img`
- Should show: "ext2_disk.img: Linux rev 1.0 ext2 filesystem data"

### "Failed to read superblock"
- Check QEMU drive attachment
- Verify sectors 2-3 are readable
- Try re-creating disk image

### Files Not Appearing
- EXT2 currently only supports root directory
- Subdirectories will show as directories but can't be opened yet
- Check file permissions (should be 644 or 755)

### "Indirect blocks not yet supported"
- File is larger than 48KB (12 × 4KB direct blocks)
- Split into smaller files or implement indirect block support

## References

- [EXT2 Specification](https://www.nongnu.org/ext2-doc/ext2.html)
- [OSDev Wiki: EXT2](https://wiki.osdev.org/Ext2)
- Linux kernel source: `fs/ext2/`
- e2fsprogs tools: `mkfs.ext2`, `dumpe2fs`, `debugfs`

## Code Examples

### Registering EXT2 with VFS
```c
// In filesystem.c
ext2_register_vfs();

// Internally in ext2_vfs_adapter.c:
bool ext2_register_vfs(void) {
    return vfs_register_filesystem("ext2", &ext2_vfs_ops) == VFS_OK;
}
```

### Reading EXT2 File
```c
ext2_fs_t fs;
ext2_init(&fs, ata_base, is_master);

ext2_inode_t inode;
ext2_read_inode(&fs, 12, &inode);  // Read inode 12

uint8_t buffer[4096];
int bytes = ext2_read_file(&fs, &inode, 0, 4096, buffer);
printf("Read %d bytes\n", bytes);
```

---

**Status**: ✅ EXT2 read support complete and functional
**VFS Integration**: ✅ Fully integrated via adapter pattern
**Testing**: ✅ Tested with 10MB disk image containing multiple files and directories
