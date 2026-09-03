/**
 * @file test/test_ext2_host.c
 * @brief Hostseitiger Regressionstest für ext2.
 *
 * Layer: Host test harness.
 * Contract: Prüft beobachtbares Verhalten und feste Fehlergrenzen ohne Zielhardware.
 * Safety: Testdoubles dürfen Produktionsverträge nicht abschwächen oder Erfolg vortäuschen.
 */
#include "fs/ext2/ext2.h"
#include <stdlib.h>
#include <string.h>

#define PARTITION_LBA 4u
#define EXT2_BLOCK_SIZE 1024u
#define EXT2_BLOCKS 1057u
#define TEST_SECTORS (PARTITION_LBA + EXT2_BLOCKS * 2u)
#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

static uint8_t test_disk[TEST_SECTORS][512];

bool ata_read_sector(unsigned short base, unsigned int lba, void* buffer,
                     bool is_master) {
    (void)base;
    (void)is_master;
    if (!buffer || lba >= TEST_SECTORS) return false;
    memcpy(buffer, test_disk[lba], 512);
    return true;
}

bool ata_write_sector(unsigned short base, unsigned int lba, void* buffer,
                      bool is_master) {
    (void)base;
    (void)is_master;
    if (!buffer || lba >= TEST_SECTORS) return false;
    memcpy(test_disk[lba], buffer, 512);
    return true;
}

static uint8_t* block(uint32_t number) {
    return &test_disk[PARTITION_LBA + number * 2u][0];
}

static uint16_t directory_record_size(uint8_t name_length) {
    return (uint16_t)((8u + name_length + 3u) & ~3u);
}

static uint32_t add_directory_entry(uint8_t* data, uint32_t offset,
                                    uint32_t inode, uint8_t type,
                                    const char* name, bool final) {
    uint8_t length = (uint8_t)strlen(name);
    uint16_t record = final ? (uint16_t)(EXT2_BLOCK_SIZE - offset) :
                              directory_record_size(length);
    memcpy(data + offset, &inode, sizeof(inode));
    memcpy(data + offset + 4, &record, sizeof(record));
    data[offset + 6] = length;
    data[offset + 7] = type;
    memcpy(data + offset + 8, name, length);
    return offset + record;
}

static void make_test_volume(void) {
    memset(test_disk, 0, sizeof(test_disk));
    ext2_superblock_t superblock;
    memset(&superblock, 0, sizeof(superblock));
    superblock.s_inodes_count = 528;
    superblock.s_blocks_count = EXT2_BLOCKS;
    superblock.s_first_data_block = 1;
    superblock.s_log_block_size = 0;
    superblock.s_blocks_per_group = 32;
    superblock.s_inodes_per_group = 16;
    superblock.s_magic = EXT2_SIGNATURE;
    superblock.s_rev_level = 1;
    superblock.s_inode_size = sizeof(ext2_inode_t);
    superblock.s_feature_incompat = 2;
    memcpy(block(1), &superblock, sizeof(superblock));

    ext2_group_desc_t descriptor;
    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.bg_inode_table = 4;
    ext2_group_desc_t* descriptors = (ext2_group_desc_t*)block(2);
    for (uint32_t i = 0; i < 33; i++) descriptors[i] = descriptor;

    ext2_inode_t* inodes = (ext2_inode_t*)block(4);
    ext2_inode_t* root = &inodes[1];
    root->i_mode = EXT2_S_IFDIR | 0755;
    root->i_size = EXT2_BLOCK_SIZE;
    root->i_block[0] = 5;

    ext2_inode_t* direct = &inodes[2];
    direct->i_mode = EXT2_S_IFREG | 0644;
    direct->i_size = 14;
    direct->i_block[0] = 6;
    memcpy(block(6), "direct-content", 14);

    ext2_inode_t* indirect = &inodes[3];
    indirect->i_mode = EXT2_S_IFREG | 0644;
    indirect->i_size = 13 * EXT2_BLOCK_SIZE;
    indirect->i_block[EXT2_IND_BLOCK] = 7;
    ((uint32_t*)block(7))[0] = 8;
    memset(block(8), 0xA5, EXT2_BLOCK_SIZE);

    ext2_inode_t* double_indirect = &inodes[4];
    double_indirect->i_mode = EXT2_S_IFREG | 0644;
    double_indirect->i_size = (12 + 256 + 1) * EXT2_BLOCK_SIZE;
    double_indirect->i_block[EXT2_DIND_BLOCK] = 9;
    ((uint32_t*)block(9))[0] = 10;
    ((uint32_t*)block(10))[0] = 11;
    memset(block(11), 0x3C, EXT2_BLOCK_SIZE);

    ext2_inode_t* symlink = &inodes[5];
    symlink->i_mode = EXT2_S_IFLNK | 0777;
    symlink->i_size = 6;
    memcpy(symlink->i_block, "target", 6);

    ext2_inode_t* block_symlink = &inodes[6];
    block_symlink->i_mode = EXT2_S_IFLNK | 0777;
    block_symlink->i_size = 64;
    block_symlink->i_blocks = 2;
    block_symlink->i_block[0] = 12;
    memcpy(block(12),
           "directory/../directory/../directory/../directory/target-file",
           64);

    uint8_t* directory = block(5);
    uint32_t offset = 0;
    offset = add_directory_entry(directory, offset, 2, EXT2_FT_DIR, ".", false);
    offset = add_directory_entry(directory, offset, 2, EXT2_FT_DIR, "..", false);
    offset = add_directory_entry(directory, offset, 3, EXT2_FT_REG_FILE,
                                 "hello", false);
    offset = add_directory_entry(directory, offset, 4, EXT2_FT_REG_FILE,
                                 "indirect", false);
    offset = add_directory_entry(directory, offset, 5, EXT2_FT_REG_FILE,
                                 "double", false);
    char name[4];
    for (uint32_t i = 0; i < 70; i++) {
        name[0] = 'f';
        name[1] = (char)('0' + i / 10);
        name[2] = (char)('0' + i % 10);
        name[3] = '\0';
        offset = add_directory_entry(directory, offset, 3,
                                     EXT2_FT_REG_FILE, name, i == 69);
    }
}

int main(void) {
    make_test_volume();
    ext2_fs_t fs;
    CHECK(ext2_init_volume(&fs, 0x1F0, true, PARTITION_LBA,
                           EXT2_BLOCKS * 2u));
    CHECK(fs.block_size == EXT2_BLOCK_SIZE);
    CHECK(fs.num_block_groups == 33);

    ext2_dir_entry_t entry;
    CHECK(ext2_find_entry(&fs, EXT2_ROOT_INO, "hello", &entry));
    CHECK(entry.inode == 3);
    CHECK(ext2_find_entry(&fs, EXT2_ROOT_INO, "f69", &entry));
    CHECK(ext2_get_dir_entry(&fs, EXT2_ROOT_INO, 74, &entry) == 1);
    CHECK(strcmp(entry.name, "f69") == 0);
    CHECK(ext2_get_dir_entry(&fs, EXT2_ROOT_INO, 75, &entry) == 0);

    ext2_inode_t inode;
    CHECK(ext2_read_inode(&fs, 3, &inode));
    char direct[8];
    CHECK(ext2_read_file(&fs, &inode, 7, sizeof(direct), direct) == 7);
    CHECK(memcmp(direct, "content", 7) == 0);

    CHECK(ext2_read_inode(&fs, 4, &inode));
    uint8_t indirect[32];
    CHECK(ext2_read_file(&fs, &inode, 12 * EXT2_BLOCK_SIZE + 100,
                         sizeof(indirect), indirect) == (int)sizeof(indirect));
    for (uint32_t i = 0; i < sizeof(indirect); i++) CHECK(indirect[i] == 0xA5);

    CHECK(ext2_read_inode(&fs, 5, &inode));
    uint8_t double_data[16];
    CHECK(ext2_read_file(&fs, &inode,
                         (12 + 256) * EXT2_BLOCK_SIZE + 3,
                         sizeof(double_data), double_data) ==
          (int)sizeof(double_data));
    for (uint32_t i = 0; i < sizeof(double_data); i++)
        CHECK(double_data[i] == 0x3C);

    CHECK(ext2_read_inode(&fs, 6, &inode));
    char link_target[6];
    CHECK(ext2_read_file(&fs, &inode, 0, sizeof(link_target), link_target) == 6);
    CHECK(memcmp(link_target, "target", 6) == 0);

    CHECK(ext2_read_inode(&fs, 7, &inode));
    char block_link_target[64];
    CHECK(ext2_read_file(&fs, &inode, 0, sizeof(block_link_target),
                         block_link_target) == 64);
    CHECK(memcmp(block_link_target,
                 "directory/../directory/../directory/../directory/target-file",
                 64) == 0);

    ext2_cleanup(&fs);
    return 0;
}
