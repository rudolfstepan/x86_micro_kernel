/** Host behavior test for the bounded Ring-3 EXT2 stat parser. */
#include <stdint.h>
#include <string.h>

#include "userspace/storage/include/reist/vfs_shadow_ext2.h"

#define TEST_BLOCK_SIZE 1024U
#define TEST_BLOCKS 128U
#define TEST_SECTORS (TEST_BLOCKS * 2U)
#define CURSOR_DIRECTORY_BLOCKS 8U
#define CURSOR_VISIBLE_ENTRIES (CURSOR_DIRECTORY_BLOCKS * 8U - 2U)

typedef struct {
    uint8_t image[TEST_SECTORS * X86OS_STORAGE_BLOCK_SIZE];
    uint32_t reads;
    uint32_t directory_reads;
} test_context_t;

static void put16(uint8_t *data, uint16_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
}

static void put32(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static uint8_t *block_at(test_context_t *context, uint32_t block) {
    return context->image + block * TEST_BLOCK_SIZE;
}

static uint8_t *inode_at(test_context_t *context, uint32_t number) {
    uint32_t offset = (number - 1U) * 128U;
    return block_at(context, 5U + offset / TEST_BLOCK_SIZE) +
        offset % TEST_BLOCK_SIZE;
}

static void make_inode(test_context_t *context, uint32_t number,
                       uint16_t mode, uint32_t size, uint32_t block) {
    uint8_t *inode = inode_at(context, number);
    put16(inode + 0U, mode);
    put32(inode + 4U, size);
    put32(inode + 8U, 1700000001U);
    put32(inode + 12U, 1700000002U);
    put32(inode + 16U, 1700000003U);
    put16(inode + 26U, 1U);
    put32(inode + 28U, block == 0U ? 0U : 2U);
    put32(inode + 40U, block);
}

static uint32_t add_entry(uint8_t *directory, uint32_t offset,
                          uint32_t inode, const char *name,
                          uint8_t type, uint16_t record_length) {
    uint32_t length = (uint32_t)strlen(name);
    put32(directory + offset, inode);
    put16(directory + offset + 4U, record_length);
    directory[offset + 6U] = (uint8_t)length;
    directory[offset + 7U] = type;
    memcpy(directory + offset + 8U, name, length);
    return offset + record_length;
}

static void fill_empty_directory_block(uint8_t *block) {
    put16(block + 4U, TEST_BLOCK_SIZE);
}

static void initialize(test_context_t *context) {
    memset(context, 0, sizeof(*context));
    uint8_t *superblock = block_at(context, 1U);
    put32(superblock + 0U, 128U);
    put32(superblock + 4U, TEST_BLOCKS);
    put32(superblock + 20U, 1U);
    put32(superblock + 24U, 0U);
    put32(superblock + 28U, 0U);
    put32(superblock + 32U, TEST_BLOCKS);
    put32(superblock + 36U, TEST_BLOCKS);
    put32(superblock + 40U, 128U);
    put16(superblock + 56U, 0xEF53U);
    put16(superblock + 58U, 1U);
    put32(superblock + 76U, 1U);
    put32(superblock + 84U, 11U);
    put16(superblock + 88U, 128U);
    put32(superblock + 96U, 2U);

    uint8_t *descriptor = block_at(context, 2U);
    put32(descriptor + 0U, 3U);
    put32(descriptor + 4U, 4U);
    put32(descriptor + 8U, 5U);

    make_inode(context, 2U, 0x41EDU, TEST_BLOCK_SIZE, 21U);
    put16(inode_at(context, 2U) + 26U, 4U);
    make_inode(context, 12U, 0x81A4U, 15U, 22U);
    make_inode(context, 13U, 0x41EDU, TEST_BLOCK_SIZE, 23U);
    put16(inode_at(context, 13U) + 26U, 2U);
    make_inode(context, 14U, 0x81A4U, 4U, 24U);
    make_inode(context, 15U, 0x41EDU, 13U * TEST_BLOCK_SIZE, 25U);
    put16(inode_at(context, 15U) + 26U, 2U);
    for (uint32_t index = 0U; index < 12U; ++index) {
        put32(inode_at(context, 15U) + 40U + index * 4U, 25U + index);
        fill_empty_directory_block(block_at(context, 25U + index));
    }
    put32(inode_at(context, 15U) + 40U + 12U * 4U, 38U);
    put32(block_at(context, 38U), 39U);
    make_inode(context, 16U, 0x81A4U, 5U, 40U);

    uint8_t *root = block_at(context, 21U);
    uint32_t offset = add_entry(root, 0U, 2U, ".", 2U, 12U);
    offset = add_entry(root, offset, 2U, "..", 2U, 12U);
    offset = add_entry(root, offset, 12U, "readme.txt", 1U, 20U);
    offset = add_entry(root, offset, 13U, "dir", 2U, 12U);
    add_entry(root, offset, 15U, "big", 2U,
              (uint16_t)(TEST_BLOCK_SIZE - offset));
    memcpy(block_at(context, 22U), "EXT2 AUTHORITY\n", 15U);

    uint8_t *directory = block_at(context, 23U);
    offset = add_entry(directory, 0U, 13U, ".", 2U, 12U);
    offset = add_entry(directory, offset, 2U, "..", 2U, 12U);
    add_entry(directory, offset, 14U, "nested.bin", 1U,
              (uint16_t)(TEST_BLOCK_SIZE - offset));
    memcpy(block_at(context, 24U), "nest", 4U);
    add_entry(block_at(context, 39U), 0U, 16U, "deep.txt", 1U,
              TEST_BLOCK_SIZE);
    memcpy(block_at(context, 40U), "deep\n", 5U);
}

static void cursor_name(char name[8], uint32_t value) {
    name[0U] = 'f';
    for (int position = 6; position >= 1; --position) {
        name[position] = (char)('0' + value % 10U);
        value /= 10U;
    }
    name[7U] = '\0';
}

static void initialize_cursor_directory(test_context_t *context) {
    initialize(context);
    make_inode(context, 13U, 0x41EDU,
               CURSOR_DIRECTORY_BLOCKS * TEST_BLOCK_SIZE, 23U);
    put16(inode_at(context, 13U) + 26U, 2U);
    for (uint32_t logical = 0U; logical < CURSOR_DIRECTORY_BLOCKS; ++logical) {
        uint32_t block = logical == 0U ? 23U : 40U + logical;
        put32(inode_at(context, 13U) + 40U + logical * 4U, block);
        uint8_t *data = block_at(context, block);
        memset(data, 0, TEST_BLOCK_SIZE);
        for (uint32_t slot = 0U; slot < 8U; ++slot) {
            uint32_t ordinal = logical * 8U + slot;
            if (ordinal == 0U)
                add_entry(data, slot * 128U, 13U, ".", 2U, 128U);
            else if (ordinal == 1U)
                add_entry(data, slot * 128U, 2U, "..", 2U, 128U);
            else {
                char name[8];
                cursor_name(name, ordinal - 2U);
                add_entry(data, slot * 128U, 14U, name, 1U, 128U);
            }
        }
    }
}

static int drive_info(void *opaque, uint32_t resource,
                      x86os_drive_info_t *info) {
    (void)opaque;
    memset(info, 0, sizeof(*info));
    if (resource > 1U) return 0;
    info->type = X86OS_DRIVE_ATA;
    info->sectors = TEST_SECTORS;
    strcpy(info->mount_point, resource == 0U ? "/" : "/mnt/ext2");
    return 1;
}

static int read_sector(void *opaque, uint32_t resource, uint32_t sector,
                       uint8_t *data) {
    test_context_t *context = opaque;
    if (resource != 1U || sector >= TEST_SECTORS) return -5;
    ++context->reads;
    uint32_t block = sector / (TEST_BLOCK_SIZE / X86OS_STORAGE_BLOCK_SIZE);
    if (block == 23U || (block >= 41U && block <= 47U))
        ++context->directory_reads;
    memcpy(data, context->image + sector * X86OS_STORAGE_BLOCK_SIZE,
           X86OS_STORAGE_BLOCK_SIZE);
    return 0;
}

static int stat_path(test_context_t *context, const char *path,
                     x86os_file_info_t *info) {
    const reist_vfs_shadow_io_t io = {context, drive_info, read_sector};
    context->reads = 0U;
    context->directory_reads = 0U;
    return reist_vfs_shadow_ext2_stat(
        &io, path, (uint32_t)strlen(path), info);
}

static int read_path(test_context_t *context, const char *path,
                     uint32_t offset, uint8_t *data, uint32_t capacity,
                     uint32_t *transferred) {
    const reist_vfs_shadow_io_t io = {context, drive_info, read_sector};
    context->reads = 0U;
    context->directory_reads = 0U;
    return reist_vfs_shadow_ext2_read(
        &io, path, (uint32_t)strlen(path), offset, data, capacity, transferred);
}

static int readdir_path(test_context_t *context, const char *path,
                        uint32_t index, x86os_file_info_t *info) {
    const reist_vfs_shadow_io_t io = {context, drive_info, read_sector};
    context->reads = 0U;
    context->directory_reads = 0U;
    return reist_vfs_shadow_ext2_readdir(
        &io, path, (uint32_t)strlen(path), index, info);
}

int main(void) {
    static test_context_t context;
    x86os_file_info_t info;
    initialize(&context);
    if (stat_path(&context, "/mnt/ext2", &info) != 0 ||
        strcmp(info.name, "/") != 0 || info.type != X86OS_DIRECTORY)
        return 1;
    if (stat_path(&context, "/mnt/ext2/readme.txt", &info) != 0 ||
        strcmp(info.name, "readme.txt") != 0 || info.type != X86OS_FILE ||
        info.size != 15U || info.create_time != 1700000002U ||
        context.reads > REIST_VFS_SHADOW_EXT2_MAX_SECTOR_READS) return 2;
    if (stat_path(&context, "/mnt/ext2/dir/nested.bin", &info) != 0 ||
        info.size != 4U) return 3;
    if (stat_path(&context, "/mnt/ext2/big/deep.txt", &info) != 0 ||
        info.size != 5U) return 4;
    if (stat_path(&context, "/mnt/ext2/README.TXT", &info) != -2) return 5;
    uint8_t data[16];
    uint32_t transferred = 0U;
    if (read_path(&context, "/mnt/ext2/readme.txt", 5U, data, 10U,
                  &transferred) != 0 || transferred != 10U ||
        memcmp(data, "AUTHORITY\n", 10U) != 0 ||
        context.reads > REIST_VFS_SHADOW_EXT2_MAX_SECTOR_READS) return 11;
    if (read_path(&context, "/mnt/ext2/readme.txt", 15U, data, 10U,
                  &transferred) != 0 || transferred != 0U) return 12;
    if (read_path(&context, "/mnt/ext2/dir", 0U, data, 10U,
                  &transferred) != -21) return 13;
    if (readdir_path(&context, "/mnt/ext2", 0U, &info) != 0 ||
        strcmp(info.name, "readme.txt") != 0) return 14;
    if (readdir_path(&context, "/mnt/ext2", 1U, &info) != 0 ||
        strcmp(info.name, "dir") != 0 || info.type != X86OS_DIRECTORY)
        return 15;
    if (readdir_path(&context, "/mnt/ext2", 3U, &info) != 1 ||
        info.name[0U] != '\0') return 16;
    if (readdir_path(&context, "/mnt/ext2/readme.txt", 0U, &info) != -20)
        return 17;

    const reist_vfs_shadow_io_t object_io = {
        &context, drive_info, read_sector
    };
    reist_vfs_shadow_object_t object;
    if (reist_vfs_shadow_ext2_object_open(
            &object_io, "/mnt/ext2/readme.txt", 20U, &object, &info) != 0 ||
        object.filesystem != REIST_VFS_SHADOW_OBJECT_EXT2 ||
        object.locator_a != 12U)
        return 23;
    memcpy(block_at(&context, 21U) + 32U, "renamed.tx", 10U);
    if (reist_vfs_shadow_ext2_object_read(
            &object_io, &object, 5U, data, 10U, &transferred) != 0 ||
        transferred != 10U || memcmp(data, "AUTHORITY\n", 10U) != 0 ||
        stat_path(&context, "/mnt/ext2/readme.txt", &info) != -2 ||
        stat_path(&context, "/mnt/ext2/renamed.tx", &info) != 0)
        return 24;
    block_at(&context, 1U)[200U] ^= 0x5AU;
    if (reist_vfs_shadow_ext2_object_stat(
            &object_io, &object, &info) != -116)
        return 25;
    initialize(&context);
    if (reist_vfs_shadow_ext2_object_open(
            &object_io, "/mnt/ext2/readme.txt", 20U, &object, &info) != 0)
        return 26;
    put32(inode_at(&context, 12U) + 100U, 1U);
    if (reist_vfs_shadow_ext2_object_read(
            &object_io, &object, 0U, data, 10U, &transferred) != -116)
        return 27;

    initialize_cursor_directory(&context);
    const reist_vfs_shadow_io_t cursor_io = {
        &context, drive_info, read_sector
    };
    reist_vfs_shadow_ext2_readdir_cursor_t cursor = {0};
    context.reads = 0U;
    context.directory_reads = 0U;
    for (uint32_t index = 0U; index < CURSOR_VISIBLE_ENTRIES; ++index)
        if (reist_vfs_shadow_ext2_readdir_continue(
                &cursor_io, "/mnt/ext2/dir", 13U, index,
                &cursor, &info) != 0)
            return 28;
    if (reist_vfs_shadow_ext2_readdir_continue(
            &cursor_io, "/mnt/ext2/dir", 13U,
            CURSOR_VISIBLE_ENTRIES, &cursor, &info) != 1 ||
        context.directory_reads > (CURSOR_VISIBLE_ENTRIES + 1U) * 2U)
        return 29;
    memset(&cursor, 0, sizeof(cursor));
    if (reist_vfs_shadow_ext2_readdir_continue(
            &cursor_io, "/mnt/ext2/dir", 13U, 0U,
            &cursor, &info) != 0)
        return 30;
    cursor.logical_block = REIST_VFS_SHADOW_EXT2_MAX_DIRECTORY_BLOCKS;
    cursor.entry_offset = 3U;
    if (reist_vfs_shadow_ext2_readdir_continue(
            &cursor_io, "/mnt/ext2/dir", 13U, 1U,
            &cursor, &info) != 0 || strcmp(info.name, "f000001") != 0)
        return 31;
    initialize_cursor_directory(&context);
    memset(&cursor, 0, sizeof(cursor));
    if (reist_vfs_shadow_ext2_readdir_continue(
            &cursor_io, "/mnt/ext2/dir", 13U, 0U,
            &cursor, &info) != 0)
        return 32;
    uint8_t *replacement = block_at(&context, 48U);
    memset(replacement, 0, TEST_BLOCK_SIZE);
    for (uint32_t slot = 0U; slot < 8U; ++slot) {
        char name[8];
        cursor_name(name, slot);
        name[0U] = 'r';
        add_entry(replacement, slot * 128U, 14U, name, 1U, 128U);
    }
    put32(inode_at(&context, 13U) + 40U, 48U);
    if (reist_vfs_shadow_ext2_readdir_continue(
            &cursor_io, "/mnt/ext2/dir", 13U, 1U,
            &cursor, &info) != 0 || strcmp(info.name, "r000001") != 0)
        return 33;

    initialize(&context);
    put16(block_at(&context, 1U) + 56U, 0U);
    if (stat_path(&context, "/mnt/ext2/readme.txt", &info) != -2) return 18;
    initialize(&context);
    put32(block_at(&context, 1U) + 96U, 0x40U);
    if (stat_path(&context, "/mnt/ext2/readme.txt", &info) != -2) return 19;
    initialize(&context);
    put16(block_at(&context, 21U) + 4U, 6U);
    if (stat_path(&context, "/mnt/ext2/readme.txt", &info) != -5) return 20;
    initialize(&context);
    put32(block_at(&context, 2U) + 8U, 127U);
    if (stat_path(&context, "/mnt/ext2/readme.txt", &info) != -5) return 21;
    initialize(&context);
    put32(inode_at(&context, 15U) + 4U, 33U * TEST_BLOCK_SIZE);
    if (stat_path(&context, "/mnt/ext2/big/missing", &info) != -110)
        return 22;
    return 0;
}
