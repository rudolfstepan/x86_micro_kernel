#ifndef EXT2_H
#define EXT2_H

#include <stdint.h>
#include <stdbool.h>

// ===========================================================================
// EXT2 Constants
// ===========================================================================

#define EXT2_SIGNATURE          0xEF53
#define EXT2_SUPERBLOCK_OFFSET  1024
#define EXT2_ROOT_INO           2

// File types
#define EXT2_S_IFSOCK   0xC000
#define EXT2_S_IFLNK    0xA000
#define EXT2_S_IFREG    0x8000
#define EXT2_S_IFBLK    0x6000
#define EXT2_S_IFDIR    0x4000
#define EXT2_S_IFCHR    0x2000
#define EXT2_S_IFIFO    0x1000

// File permissions
#define EXT2_S_IRUSR    0x0100
#define EXT2_S_IWUSR    0x0080
#define EXT2_S_IXUSR    0x0040
#define EXT2_S_IRGRP    0x0020
#define EXT2_S_IWGRP    0x0010
#define EXT2_S_IXGRP    0x0008
#define EXT2_S_IROTH    0x0004
#define EXT2_S_IWOTH    0x0002
#define EXT2_S_IXOTH    0x0001

#define EXT2_NDIR_BLOCKS    12
#define EXT2_IND_BLOCK      EXT2_NDIR_BLOCKS
#define EXT2_DIND_BLOCK     (EXT2_IND_BLOCK + 1)
#define EXT2_TIND_BLOCK     (EXT2_DIND_BLOCK + 1)
#define EXT2_N_BLOCKS       (EXT2_TIND_BLOCK + 1)

#define EXT2_NAME_LEN       255

// Directory entry file types
#define EXT2_FT_UNKNOWN     0
#define EXT2_FT_REG_FILE    1
#define EXT2_FT_DIR         2
#define EXT2_FT_CHRDEV      3
#define EXT2_FT_BLKDEV      4
#define EXT2_FT_FIFO        5
#define EXT2_FT_SOCK        6
#define EXT2_FT_SYMLINK     7

// ===========================================================================
// EXT2 Structures
// ===========================================================================

#pragma pack(push, 1)
typedef struct {
    uint32_t s_inodes_count;        // Total number of inodes
    uint32_t s_blocks_count;        // Total number of blocks
    uint32_t s_r_blocks_count;      // Number of reserved blocks
    uint32_t s_free_blocks_count;   // Number of free blocks
    uint32_t s_free_inodes_count;   // Number of free inodes
    uint32_t s_first_data_block;    // First data block
    uint32_t s_log_block_size;      // Block size (log2(block_size) - 10)
    uint32_t s_log_frag_size;       // Fragment size
    uint32_t s_blocks_per_group;    // Blocks per group
    uint32_t s_frags_per_group;     // Fragments per group
    uint32_t s_inodes_per_group;    // Inodes per group
    uint32_t s_mtime;               // Mount time
    uint32_t s_wtime;               // Write time
    uint16_t s_mnt_count;           // Mount count
    uint16_t s_max_mnt_count;       // Max mount count
    uint16_t s_magic;               // Magic signature (0xEF53)
    uint16_t s_state;               // File system state
    uint16_t s_errors;              // Behavior when detecting errors
    uint16_t s_minor_rev_level;     // Minor revision level
    uint32_t s_lastcheck;           // Time of last check
    uint32_t s_checkinterval;       // Max time between checks
    uint32_t s_creator_os;          // OS that created the filesystem
    uint32_t s_rev_level;           // Revision level
    uint16_t s_def_resuid;          // Default uid for reserved blocks
    uint16_t s_def_resgid;          // Default gid for reserved blocks
    
    // Extended fields (EXT2_DYNAMIC_REV)
    uint32_t s_first_ino;           // First non-reserved inode
    uint16_t s_inode_size;          // Size of inode structure
    uint16_t s_block_group_nr;      // Block group number of this superblock
    uint32_t s_feature_compat;      // Compatible feature set
    uint32_t s_feature_incompat;    // Incompatible feature set
    uint32_t s_feature_ro_compat;   // Readonly-compatible feature set
    uint8_t  s_uuid[16];            // 128-bit UUID
    char     s_volume_name[16];     // Volume name
    char     s_last_mounted[64];    // Directory where last mounted
    uint32_t s_algo_bitmap;         // Compression algorithm
    
    uint8_t  s_prealloc_blocks;     // Blocks to preallocate for files
    uint8_t  s_prealloc_dir_blocks; // Blocks to preallocate for directories
    uint16_t s_padding1;
    
    uint8_t  s_journal_uuid[16];    // UUID of journal superblock
    uint32_t s_journal_inum;        // Inode number of journal file
    uint32_t s_journal_dev;         // Device number of journal file
    uint32_t s_last_orphan;         // Start of list of orphan inodes
    
    uint8_t  s_reserved[788];       // Padding to 1024 bytes
} ext2_superblock_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
    uint32_t bg_block_bitmap;       // Block bitmap block
    uint32_t bg_inode_bitmap;       // Inode bitmap block
    uint32_t bg_inode_table;        // Inode table block
    uint16_t bg_free_blocks_count;  // Free blocks count
    uint16_t bg_free_inodes_count;  // Free inodes count
    uint16_t bg_used_dirs_count;    // Used directories count
    uint16_t bg_pad;
    uint8_t  bg_reserved[12];
} ext2_group_desc_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
    uint16_t i_mode;                // File mode
    uint16_t i_uid;                 // Owner UID
    uint32_t i_size;                // Size in bytes
    uint32_t i_atime;               // Access time
    uint32_t i_ctime;               // Creation time
    uint32_t i_mtime;               // Modification time
    uint32_t i_dtime;               // Deletion time
    uint16_t i_gid;                 // Group ID
    uint16_t i_links_count;         // Links count
    uint32_t i_blocks;              // Blocks count
    uint32_t i_flags;               // File flags
    uint32_t i_osd1;                // OS-dependent 1
    uint32_t i_block[EXT2_N_BLOCKS]; // Pointers to blocks
    uint32_t i_generation;          // File version (for NFS)
    uint32_t i_file_acl;            // File ACL
    uint32_t i_dir_acl;             // Directory ACL
    uint32_t i_faddr;               // Fragment address
    uint8_t  i_osd2[12];            // OS-dependent 2
} ext2_inode_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
    uint32_t inode;                 // Inode number
    uint16_t rec_len;               // Directory entry length
    uint8_t  name_len;              // Name length
    uint8_t  file_type;             // File type
    char     name[EXT2_NAME_LEN];   // File name
} ext2_dir_entry_t;
#pragma pack(pop)

// ===========================================================================
// EXT2 Filesystem Structure
// ===========================================================================

typedef struct {
    ext2_superblock_t superblock;
    ext2_group_desc_t* group_desc_table;
    uint32_t block_size;
    uint32_t num_block_groups;
    uint32_t inodes_per_block;
    uint32_t current_dir_inode;     // Current directory inode
    void* block_buffer;              // Temporary buffer for block operations
    uint16_t ata_base;               // ATA base port for this filesystem
    bool ata_is_master;              // ATA master/slave flag for this filesystem
} ext2_fs_t;

// ===========================================================================
// EXT2 Function Declarations
// ===========================================================================

// Initialization
bool ext2_init(ext2_fs_t* fs, uint16_t base, bool is_master);
void ext2_cleanup(ext2_fs_t* fs);

// Block operations
bool ext2_read_block(ext2_fs_t* fs, uint32_t block_num, void* buffer);
bool ext2_write_block(ext2_fs_t* fs, uint32_t block_num, const void* buffer);

// Inode operations
bool ext2_read_inode(ext2_fs_t* fs, uint32_t inode_num, ext2_inode_t* inode);
bool ext2_write_inode(ext2_fs_t* fs, uint32_t inode_num, const ext2_inode_t* inode);

// Directory operations
bool ext2_read_dir(ext2_fs_t* fs, uint32_t inode_num, ext2_dir_entry_t* entries, uint32_t max_entries, uint32_t* count);
bool ext2_find_entry(ext2_fs_t* fs, uint32_t dir_inode, const char* name, ext2_dir_entry_t* entry);

// File operations
int ext2_read_file(ext2_fs_t* fs, ext2_inode_t* inode, uint32_t offset, uint32_t size, void* buffer);

// Utility functions
uint32_t ext2_get_block_size(ext2_fs_t* fs);
uint32_t ext2_get_inode_block_group(ext2_fs_t* fs, uint32_t inode_num);
uint32_t ext2_get_inode_table_index(ext2_fs_t* fs, uint32_t inode_num);

#endif // EXT2_H
