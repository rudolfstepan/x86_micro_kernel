#include "ext2.h"
#include "lib/libc/stdio.h"
#include "lib/libc/string.h"
#include "lib/libc/stdlib.h"
#include "drivers/block/ata.h"
#include "drivers/bus/drives.h"

// Global variables for current drive
static uint16_t ext2_ata_base = 0;
static bool ext2_ata_is_master = false;

// ===========================================================================
// Helper Functions
// ===========================================================================

static bool ext2_read_sector(uint32_t sector, void* buffer) {
    return ata_read_sector(ext2_ata_base, sector, buffer, ext2_ata_is_master);
}

static bool ext2_write_sector(uint32_t sector, const void* buffer) {
    return ata_write_sector(ext2_ata_base, sector, (void*)buffer, ext2_ata_is_master);
}

// ===========================================================================
// Initialization
// ===========================================================================

bool ext2_init(ext2_fs_t* fs, uint16_t base, bool is_master) {
    if (!fs) {
        return false;
    }
    
    printf("\x1B[36mEXT2: Initializing filesystem (base=0x%X, master=%d)\x1B[0m\n", base, is_master);
    
    // Store ATA parameters in filesystem structure
    fs->ata_base = base;
    fs->ata_is_master = is_master;
    
    // Also set globals for compatibility (will be removed later)
    ext2_ata_base = base;
    ext2_ata_is_master = is_master;
    
    // Read superblock (starts at byte 1024, sector 2)
    uint8_t buffer[1024];
    if (!ata_read_sector(fs->ata_base, 2, buffer, fs->ata_is_master)) {
        printf("\x1B[31mEXT2: Failed to read superblock (sector 2)\x1B[0m\n");
        return false;
    }
    if (!ata_read_sector(fs->ata_base, 3, buffer + 512, fs->ata_is_master)) {
        printf("EXT2: Failed to read superblock (sector 3)\n");
        return false;
    }
    
    memcpy(&fs->superblock, buffer, sizeof(ext2_superblock_t));
    
    // Debug: show first 32 bytes of superblock
    printf("EXT2: Superblock first 32 bytes:\n");
    for (int i = 0; i < 32; i++) {
        printf("%02X ", buffer[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    
    // Verify magic number
    printf("EXT2: Magic check: read=0x%04X, expected=0x%04X\n", 
           fs->superblock.s_magic, EXT2_SIGNATURE);
    if (fs->superblock.s_magic != EXT2_SIGNATURE) {
        printf("EXT2: Invalid magic number: 0x%X (expected 0x%X)\n", 
               fs->superblock.s_magic, EXT2_SIGNATURE);
        return false;
    }
    
    // Calculate block size
    printf("EXT2: s_inodes_count=%u\n", fs->superblock.s_inodes_count);
    printf("EXT2: s_blocks_count=%u\n", fs->superblock.s_blocks_count);
    printf("EXT2: s_log_block_size=%u\n", fs->superblock.s_log_block_size);
    fs->block_size = 1024 << fs->superblock.s_log_block_size;
    printf("EXT2: Calculated block_size=%u bytes\n", fs->block_size);
    
    if (fs->block_size == 0 || fs->block_size > 65536) {
        printf("EXT2: Invalid block size %u\n", fs->block_size);
        return false;
    }
    
    // Calculate number of block groups
    fs->num_block_groups = (fs->superblock.s_blocks_count + fs->superblock.s_blocks_per_group - 1) 
                           / fs->superblock.s_blocks_per_group;
    printf("EXT2: num_block_groups=%u\n", fs->num_block_groups);
    
    // Calculate inodes per block
    uint32_t inode_size = fs->superblock.s_inode_size;
    if (inode_size == 0) {
        inode_size = 128;  // Default for old ext2
    }
    fs->inodes_per_block = fs->block_size / inode_size;
    
    // Read group descriptor table
    uint32_t gdt_block = fs->superblock.s_first_data_block + 1;
    uint32_t gdt_size = fs->num_block_groups * sizeof(ext2_group_desc_t);
    printf("EXT2: Allocating %u bytes for group descriptor table\n", gdt_size);
    printf("EXT2: gdt_block=%u, s_first_data_block=%u\n", gdt_block, fs->superblock.s_first_data_block);
    
    fs->group_desc_table = (ext2_group_desc_t*)malloc(gdt_size);
    if (!fs->group_desc_table) {
        printf("EXT2: Failed to allocate group descriptor table (%u bytes)\n", gdt_size);
        return false;
    }
    printf("EXT2: Group descriptor table allocated successfully\n");
    
    // Read group descriptor table blocks
    uint32_t gdt_blocks = (gdt_size + fs->block_size - 1) / fs->block_size;
    printf("EXT2: Reading %u GDT blocks starting at block %u\n", gdt_blocks, gdt_block);
    for (uint32_t i = 0; i < gdt_blocks; i++) {
        printf("EXT2: Reading GDT block %u (absolute block %u)\n", i, gdt_block + i);
        if (!ext2_read_block(fs, gdt_block + i, (uint8_t*)fs->group_desc_table + (i * fs->block_size))) {
            printf("EXT2: Failed to read group descriptor block %u\n", i);
            free(fs->group_desc_table);
            return false;
        }
    }
    
    // Allocate block buffer
    printf("EXT2: Allocating %u byte block buffer\n", fs->block_size);
    fs->block_buffer = malloc(fs->block_size);
    if (!fs->block_buffer) {
        printf("EXT2: Failed to allocate %u byte block buffer (out of memory)\n", fs->block_size);
        free(fs->group_desc_table);
        return false;
    }
    printf("EXT2: Block buffer allocated successfully at %p\n", fs->block_buffer);
    
    // Set current directory to root
    fs->current_dir_inode = EXT2_ROOT_INO;
    
    printf("EXT2: Initialization complete\n");
    return true;
}

void ext2_cleanup(ext2_fs_t* fs) {
    if (!fs) {
        return;
    }
    
    if (fs->group_desc_table) {
        free(fs->group_desc_table);
        fs->group_desc_table = NULL;
    }
    
    if (fs->block_buffer) {
        free(fs->block_buffer);
        fs->block_buffer = NULL;
    }
}

// ===========================================================================
// Block Operations
// ===========================================================================

uint32_t ext2_get_block_size(ext2_fs_t* fs) {
    return fs->block_size;
}

bool ext2_read_block(ext2_fs_t* fs, uint32_t block_num, void* buffer) {
    if (!fs || !buffer) {
        printf("EXT2: read_block - invalid params\n");
        return false;
    }
    
    // Calculate sector number
    uint32_t sectors_per_block = fs->block_size / 512;
    uint32_t start_sector = block_num * sectors_per_block;
    
    printf("EXT2: read_block - block=%u, sector=%u, count=%u, base=0x%X, master=%d\n",
           block_num, start_sector, sectors_per_block, fs->ata_base, fs->ata_is_master);
    
    // Read all sectors in this block using THIS filesystem's ATA parameters
    for (uint32_t i = 0; i < sectors_per_block; i++) {
        if (!ata_read_sector(fs->ata_base, start_sector + i, (uint8_t*)buffer + (i * 512), fs->ata_is_master)) {
            printf("EXT2: read_block - failed to read sector %u\n", start_sector + i);
            return false;
        }
    }
    
    return true;
}

bool ext2_write_block(ext2_fs_t* fs, uint32_t block_num, const void* buffer) {
    if (!fs || !buffer) {
        return false;
    }
    
    // Calculate sector number
    uint32_t sectors_per_block = fs->block_size / 512;
    uint32_t start_sector = block_num * sectors_per_block;
    
    // Write all sectors in this block using THIS filesystem's ATA parameters
    for (uint32_t i = 0; i < sectors_per_block; i++) {
        if (!ata_write_sector(fs->ata_base, start_sector + i, (uint8_t*)buffer + (i * 512), fs->ata_is_master)) {
            return false;
        }
    }
    
    return true;
}

// ===========================================================================
// Inode Operations
// ===========================================================================

uint32_t ext2_get_inode_block_group(ext2_fs_t* fs, uint32_t inode_num) {
    return (inode_num - 1) / fs->superblock.s_inodes_per_group;
}

uint32_t ext2_get_inode_table_index(ext2_fs_t* fs, uint32_t inode_num) {
    return (inode_num - 1) % fs->superblock.s_inodes_per_group;
}

bool ext2_read_inode(ext2_fs_t* fs, uint32_t inode_num, ext2_inode_t* inode) {
    if (!fs || !inode || inode_num == 0) {
        printf("EXT2: read_inode - invalid params (fs=%p, inode=%p, num=%u)\n", fs, inode, inode_num);
        return false;
    }
    
    printf("EXT2: read_inode - reading inode %u\n", inode_num);
    
    // Get block group and index
    uint32_t block_group = ext2_get_inode_block_group(fs, inode_num);
    uint32_t index = ext2_get_inode_table_index(fs, inode_num);
    
    printf("EXT2: read_inode - block_group=%u, index=%u, num_groups=%u\n", 
           block_group, index, fs->num_block_groups);
    
    if (block_group >= fs->num_block_groups) {
        printf("EXT2: Invalid block group %u for inode %u\n", block_group, inode_num);
        return false;
    }
    
    // Get inode table block
    uint32_t inode_table_block = fs->group_desc_table[block_group].bg_inode_table;
    
    // Calculate which block contains this inode
    uint32_t inode_size = fs->superblock.s_inode_size;
    if (inode_size == 0) {
        inode_size = 128;
    }
    
    uint32_t block_offset = (index * inode_size) / fs->block_size;
    uint32_t offset_in_block = (index * inode_size) % fs->block_size;
    
    // Read the block containing the inode
    if (!ext2_read_block(fs, inode_table_block + block_offset, fs->block_buffer)) {
        printf("EXT2: Failed to read inode table block\n");
        return false;
    }
    
    // Copy inode from buffer
    memcpy(inode, (uint8_t*)fs->block_buffer + offset_in_block, sizeof(ext2_inode_t));
    
    return true;
}

bool ext2_write_inode(ext2_fs_t* fs, uint32_t inode_num, const ext2_inode_t* inode) {
    if (!fs || !inode || inode_num == 0) {
        return false;
    }
    
    // Get block group and index
    uint32_t block_group = ext2_get_inode_block_group(fs, inode_num);
    uint32_t index = ext2_get_inode_table_index(fs, inode_num);
    
    if (block_group >= fs->num_block_groups) {
        return false;
    }
    
    // Get inode table block
    uint32_t inode_table_block = fs->group_desc_table[block_group].bg_inode_table;
    
    // Calculate which block contains this inode
    uint32_t inode_size = fs->superblock.s_inode_size;
    if (inode_size == 0) {
        inode_size = 128;
    }
    
    uint32_t block_offset = (index * inode_size) / fs->block_size;
    uint32_t offset_in_block = (index * inode_size) % fs->block_size;
    
    // Read the block containing the inode
    if (!ext2_read_block(fs, inode_table_block + block_offset, fs->block_buffer)) {
        return false;
    }
    
    // Update inode in buffer
    memcpy((uint8_t*)fs->block_buffer + offset_in_block, inode, sizeof(ext2_inode_t));
    
    // Write block back
    return ext2_write_block(fs, inode_table_block + block_offset, fs->block_buffer);
}

// ===========================================================================
// File Operations
// ===========================================================================

int ext2_read_file(ext2_fs_t* fs, ext2_inode_t* inode, uint32_t offset, uint32_t size, void* buffer) {
    if (!fs || !inode || !buffer) {
        return -1;
    }
    
    if (offset >= inode->i_size) {
        return 0;  // EOF
    }
    
    // Adjust size if reading past EOF
    if (offset + size > inode->i_size) {
        size = inode->i_size - offset;
    }
    
    uint32_t bytes_read = 0;
    uint32_t current_offset = offset;
    
    while (bytes_read < size) {
        // Calculate which block we need
        uint32_t block_index = current_offset / fs->block_size;
        uint32_t offset_in_block = current_offset % fs->block_size;
        uint32_t bytes_in_block = fs->block_size - offset_in_block;
        
        if (bytes_in_block > size - bytes_read) {
            bytes_in_block = size - bytes_read;
        }
        
        // Get the actual block number (only support direct blocks for now)
        if (block_index >= EXT2_NDIR_BLOCKS) {
            printf("EXT2: Indirect blocks not yet supported\n");
            break;
        }
        
        uint32_t block_num = inode->i_block[block_index];
        if (block_num == 0) {
            // Sparse file - block not allocated
            memset((uint8_t*)buffer + bytes_read, 0, bytes_in_block);
        } else {
            // Read the block
            if (!ext2_read_block(fs, block_num, fs->block_buffer)) {
                printf("EXT2: Failed to read block %u\n", block_num);
                break;
            }
            
            // Copy data from block to buffer
            memcpy((uint8_t*)buffer + bytes_read, 
                   (uint8_t*)fs->block_buffer + offset_in_block, 
                   bytes_in_block);
        }
        
        bytes_read += bytes_in_block;
        current_offset += bytes_in_block;
    }
    
    return bytes_read;
}

// ===========================================================================
// Directory Operations
// ===========================================================================

bool ext2_read_dir(ext2_fs_t* fs, uint32_t inode_num, ext2_dir_entry_t* entries, uint32_t max_entries, uint32_t* count) {
    if (!fs || !entries || !count) {
        printf("EXT2_DEBUG: Invalid parameters to ext2_read_dir\n");
        return false;
    }
    
    printf("EXT2: read_dir - reading inode %u\n", inode_num);
    
    // Read directory inode
    ext2_inode_t inode;
    if (!ext2_read_inode(fs, inode_num, &inode)) {
        printf("EXT2: read_dir - failed to read inode %u\n", inode_num);
        return false;
    }
    
    printf("EXT2: read_dir - inode mode=0x%X, size=%u\n", inode.i_mode, inode.i_size);
    
    // Check if it's a directory
    if ((inode.i_mode & EXT2_S_IFDIR) != EXT2_S_IFDIR) {
        printf("EXT2: read_dir - inode %u is not a directory (mode=0x%X)\n", inode_num, inode.i_mode);
        return false;
    }
    
    *count = 0;
    uint32_t offset = 0;
    
    while (offset < inode.i_size && *count < max_entries) {
        // Calculate block
        uint32_t block_index = offset / fs->block_size;
        
        if (block_index >= EXT2_NDIR_BLOCKS) {
            printf("EXT2: Directory too large (indirect blocks not supported)\n");
            break;
        }
        
        uint32_t block_num = inode.i_block[block_index];
        if (block_num == 0) {
            break;
        }
        
        // Read directory block
        if (!ext2_read_block(fs, block_num, fs->block_buffer)) {
            return false;
        }
        
        // Parse directory entries in this block
        uint32_t block_offset = 0;
        while (block_offset < fs->block_size && offset < inode.i_size) {
            ext2_dir_entry_t* entry = (ext2_dir_entry_t*)((uint8_t*)fs->block_buffer + block_offset);
            
            if (entry->inode == 0 || entry->rec_len == 0) {
                break;
            }
            
            // Sanity check rec_len
            if (entry->rec_len > fs->block_size || entry->rec_len < 8) {
                break;
            }
            
            // Copy entry - only copy the actual used data (inode, rec_len, name_len, file_type, and name)
            if (*count < max_entries) {
                entries[*count].inode = entry->inode;
                entries[*count].rec_len = entry->rec_len;
                entries[*count].name_len = entry->name_len;
                entries[*count].file_type = entry->file_type;
                
                // Copy only the actual name bytes
                uint32_t copy_len = entry->name_len;
                if (copy_len > EXT2_NAME_LEN) {
                    copy_len = EXT2_NAME_LEN;
                }
                memcpy(entries[*count].name, entry->name, copy_len);
                entries[*count].name[copy_len] = '\0';  // Null-terminate
                
                (*count)++;
            }
            
            block_offset += entry->rec_len;
            offset += entry->rec_len;
        }
        
        // Move to next block
        offset = (block_index + 1) * fs->block_size;
    }
    
    return true;
}

bool ext2_find_entry(ext2_fs_t* fs, uint32_t dir_inode, const char* name, ext2_dir_entry_t* entry) {
    if (!fs || !name || !entry) {
        return false;
    }
    
    ext2_dir_entry_t entries[64];
    uint32_t count = 0;
    
    if (!ext2_read_dir(fs, dir_inode, entries, 64, &count)) {
        return false;
    }
    
    for (uint32_t i = 0; i < count; i++) {
        if (strncmp(entries[i].name, name, entries[i].name_len) == 0 &&
            strlen(name) == entries[i].name_len) {
            memcpy(entry, &entries[i], sizeof(ext2_dir_entry_t));
            return true;
        }
    }
    
    return false;
}
