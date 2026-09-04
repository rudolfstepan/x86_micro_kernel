/** Host behavior and interruption test for bounded Ring-3 EXT2 symlinks. */
#include <stdint.h>
#include <string.h>

#include "userspace/storage/include/reist/vfs_shadow_ext2.h"
#include "userspace/storage/include/reist/vfs_symlink_client.h"

#define BLOCK_SIZE 1024U
#define BLOCKS 256U
#define SECTORS (BLOCKS * 2U)
#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

typedef struct {
    uint8_t image[SECTORS * X86OS_STORAGE_BLOCK_SIZE];
    uint64_t now_ms;
    uint32_t reads;
    uint32_t writes;
    uint32_t flushes;
    uint32_t fail_write;
    uint32_t fail_flush;
} context_t;

static void put16(uint8_t *data, uint16_t value) {
    data[0U] = (uint8_t)value;
    data[1U] = (uint8_t)(value >> 8U);
}

static void put32(uint8_t *data, uint32_t value) {
    data[0U] = (uint8_t)value;
    data[1U] = (uint8_t)(value >> 8U);
    data[2U] = (uint8_t)(value >> 16U);
    data[3U] = (uint8_t)(value >> 24U);
}

static uint16_t read16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0U] |
                      (uint16_t)((uint16_t)data[1U] << 8U));
}

static uint32_t read32(const uint8_t *data) {
    return (uint32_t)data[0U] | ((uint32_t)data[1U] << 8U) |
           ((uint32_t)data[2U] << 16U) | ((uint32_t)data[3U] << 24U);
}

static uint8_t *block_at(context_t *context, uint32_t block) {
    return context->image + block * BLOCK_SIZE;
}

static uint8_t *inode_at(context_t *context, uint32_t number) {
    uint32_t offset = (number - 1U) * 128U;
    return block_at(context, 5U + offset / BLOCK_SIZE) + offset % BLOCK_SIZE;
}

static void make_inode(context_t *context, uint32_t number, uint16_t mode,
                       uint32_t size, uint32_t block) {
    uint8_t *inode = inode_at(context, number);
    put16(inode + 0U, mode);
    put32(inode + 4U, size);
    put16(inode + 26U, 1U);
    put32(inode + 28U, block == 0U ? 0U : 2U);
    put32(inode + 40U, block);
    put32(inode + 100U, number);
}

static void make_fast_link(context_t *context, uint32_t number,
                           const char *target) {
    uint32_t length = (uint32_t)strlen(target);
    make_inode(context, number, 0xA1FFU, length, 0U);
    memcpy(inode_at(context, number) + 40U, target, length);
}

static uint16_t record_size(uint32_t length) {
    return (uint16_t)((8U + length + 3U) & ~3U);
}

static uint32_t add_entry(uint8_t *directory, uint32_t offset,
                          uint32_t inode, const char *name, uint8_t type,
                          uint16_t record) {
    uint32_t length = (uint32_t)strlen(name);
    put32(directory + offset, inode);
    put16(directory + offset + 4U, record);
    directory[offset + 6U] = (uint8_t)length;
    directory[offset + 7U] = type;
    memcpy(directory + offset + 8U, name, length);
    return offset + record;
}

static void mark_allocated(uint8_t *bitmap, uint32_t first,
                           uint32_t last) {
    for (uint32_t number = first; number <= last; ++number) {
        uint32_t bit = number - first;
        bitmap[bit / 8U] |= (uint8_t)(1U << (bit & 7U));
    }
}

static void initialize(context_t *context) {
    memset(context, 0, sizeof(*context));
    context->now_ms = 100U;
    context->fail_write = UINT32_MAX;
    context->fail_flush = UINT32_MAX;
    uint8_t *superblock = block_at(context, 1U);
    put32(superblock + 0U, 128U);
    put32(superblock + 4U, BLOCKS);
    put32(superblock + 12U, 216U);
    put32(superblock + 16U, 106U);
    put32(superblock + 20U, 1U);
    put32(superblock + 24U, 0U);
    put32(superblock + 32U, BLOCKS);
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
    put16(descriptor + 12U, 216U);
    put16(descriptor + 14U, 106U);
    mark_allocated(block_at(context, 3U), 1U, 39U);
    mark_allocated(block_at(context, 4U), 1U, 22U);

    make_inode(context, 2U, 0x41EDU, BLOCK_SIZE, 21U);
    put16(inode_at(context, 2U) + 26U, 3U);
    make_inode(context, 12U, 0x81A4U, 8U, 22U);
    memcpy(block_at(context, 22U), "payload\n", 8U);
    make_inode(context, 13U, 0x41EDU, BLOCK_SIZE, 23U);
    put16(inode_at(context, 13U) + 26U, 2U);
    make_fast_link(context, 14U, "target.txt");
    make_fast_link(context, 15U, "/mnt/ext2/target.txt");
    make_fast_link(context, 16U, "fast-link");
    make_fast_link(context, 17U, "missing.txt");
    make_fast_link(context, 18U, "cycle-b");
    make_fast_link(context, 19U, "cycle-a");
    const char *long_target =
        "dir/../dir/../dir/../dir/../dir/../dir/../dir/../dir/../target.txt";
    make_inode(context, 20U, 0xA1FFU, (uint32_t)strlen(long_target), 39U);
    memcpy(block_at(context, 39U), long_target, strlen(long_target));
    make_inode(context, 21U, 0x81A4U,
               REIST_VFS_SHADOW_EXT2_JOURNAL_SECTORS *
                   X86OS_STORAGE_BLOCK_SIZE,
               25U);
    for (uint32_t logical = 0U; logical < 12U; ++logical)
        put32(inode_at(context, 21U) + 40U + logical * 4U, 25U + logical);
    put32(inode_at(context, 21U) + 40U + 12U * 4U, 38U);
    put32(block_at(context, 38U), 37U);
    put32(inode_at(context, 21U) + 28U, 28U);
    make_fast_link(context, 22U, "../target.txt");

    uint8_t *root = block_at(context, 21U);
    uint32_t offset = add_entry(root, 0U, 2U, ".", 2U, 12U);
    offset = add_entry(root, offset, 2U, "..", 2U, 12U);
    offset = add_entry(root, offset, 12U, "target.txt", 1U,
                       record_size(10U));
    offset = add_entry(root, offset, 13U, "dir", 2U, record_size(3U));
    offset = add_entry(root, offset, 14U, "fast-link", 7U,
                       record_size(9U));
    offset = add_entry(root, offset, 15U, "absolute-link", 7U,
                       record_size(13U));
    offset = add_entry(root, offset, 16U, "chain-link", 7U,
                       record_size(10U));
    offset = add_entry(root, offset, 17U, "dangling-link", 7U,
                       record_size(13U));
    offset = add_entry(root, offset, 18U, "cycle-a", 7U,
                       record_size(7U));
    offset = add_entry(root, offset, 19U, "cycle-b", 7U,
                       record_size(7U));
    offset = add_entry(root, offset, 20U, "long-link", 7U,
                       record_size(9U));
    add_entry(root, offset, 21U, ".reist-symlink-journal", 1U,
              (uint16_t)(BLOCK_SIZE - offset));

    uint8_t *directory = block_at(context, 23U);
    offset = add_entry(directory, 0U, 13U, ".", 2U, 12U);
    offset = add_entry(directory, offset, 2U, "..", 2U, 12U);
    add_entry(directory, offset, 22U, "relative-link", 7U,
              (uint16_t)(BLOCK_SIZE - offset));
}

static void initialize_without_source_sector_slack(context_t *context) {
    initialize(context);
    uint8_t *root = block_at(context, 21U);
    uint32_t cursor = 0U;
    while (cursor < X86OS_STORAGE_BLOCK_SIZE) {
        uint32_t record = read16(root + cursor + 4U);
        uint32_t name_length = root[cursor + 6U];
        if (name_length == strlen(".reist-symlink-journal") &&
            memcmp(root + cursor + 8U, ".reist-symlink-journal",
                   name_length) == 0)
            break;
        cursor += record;
    }
    uint16_t journal_record = record_size(
        (uint32_t)strlen(".reist-symlink-journal"));
    put16(root + cursor + 4U, journal_record);
    cursor += journal_record;
    char padding[256U];
    memset(padding, 'p', sizeof(padding) - 1U);
    padding[sizeof(padding) - 1U] = '\0';
    add_entry(root, cursor, 14U, padding, 7U,
              (uint16_t)(X86OS_STORAGE_BLOCK_SIZE - cursor));
    add_entry(root, X86OS_STORAGE_BLOCK_SIZE, 0U, "", 0U,
              X86OS_STORAGE_BLOCK_SIZE);
}

static void allocate_block(context_t *context, uint32_t block) {
    uint32_t bit = block - 1U;
    block_at(context, 3U)[bit / 8U] |=
        (uint8_t)(1U << (bit & 7U));
}

static uint32_t regular_allocation_block(uint32_t index) {
    if (index == 0U) return 22U;
    if (index <= 11U) return 39U + index;
    return index == 12U ? 51U : 52U;
}

static void initialize_indirect_regular(context_t *context) {
    initialize(context);
    uint8_t *inode = inode_at(context, 12U);
    memset(inode, 0, 128U);
    make_inode(context, 12U, 0x81A4U, 13U * BLOCK_SIZE, 22U);
    for (uint32_t index = 1U; index < 12U; ++index)
        put32(inode + 40U + index * 4U, 39U + index);
    put32(inode + 40U + 12U * 4U, 52U);
    put32(inode + 28U, 28U);
    put32(block_at(context, 52U), 51U);
    for (uint32_t index = 0U; index < 13U; ++index) {
        uint32_t block = regular_allocation_block(index);
        memset(block_at(context, block), (int)(uint8_t)block, BLOCK_SIZE);
        if (block >= 40U) allocate_block(context, block);
    }
    memset(block_at(context, 52U), 0, BLOCK_SIZE);
    put32(block_at(context, 52U), 51U);
    allocate_block(context, 52U);
    put32(block_at(context, 1U) + 12U, 203U);
    put16(block_at(context, 2U) + 12U, 203U);
}

static int drive_info(void *opaque, uint32_t resource,
                      x86os_drive_info_t *info) {
    (void)opaque;
    memset(info, 0, sizeof(*info));
    if (resource > 1U) return 0;
    info->type = X86OS_DRIVE_ATA;
    info->sectors = SECTORS;
    strcpy(info->mount_point, resource == 0U ? "/" : "/mnt/ext2");
    return 1;
}

static int read_sector(void *opaque, uint32_t resource, uint32_t sector,
                       uint8_t *data) {
    context_t *context = opaque;
    ++context->reads;
    if (resource != 1U || sector >= SECTORS) {
        memset(data, 0, X86OS_STORAGE_BLOCK_SIZE);
        return resource == 0U && sector < SECTORS ? 0 : -5;
    }
    memcpy(data, context->image + sector * X86OS_STORAGE_BLOCK_SIZE,
           X86OS_STORAGE_BLOCK_SIZE);
    return 0;
}

static int write_sector(void *opaque, uint32_t resource, uint32_t sector,
                        const uint8_t *data) {
    context_t *context = opaque;
    uint32_t ordinal = context->writes++;
    if (ordinal == context->fail_write) return -5;
    if (resource != 1U || sector >= SECTORS) return -5;
    memcpy(context->image + sector * X86OS_STORAGE_BLOCK_SIZE, data,
           X86OS_STORAGE_BLOCK_SIZE);
    return 0;
}

static int flush(void *opaque, uint32_t resource) {
    context_t *context = opaque;
    uint32_t ordinal = context->flushes++;
    if (ordinal == context->fail_flush) return -5;
    return resource == 1U ? 0 : -5;
}

static int monotonic(void *opaque, uint64_t *milliseconds) {
    context_t *context = opaque;
    *milliseconds = context->now_ms++;
    return 0;
}

static reist_vfs_shadow_ext2_io_t io_for(context_t *context) {
    const reist_vfs_shadow_ext2_io_t io = {
        context, drive_info, read_sector, write_sector, flush, monotonic
    };
    return io;
}

static int lstat_path(context_t *context, const char *path,
                      x86os_file_info_t *info) {
    reist_vfs_shadow_ext2_io_t io = io_for(context);
    return reist_vfs_shadow_ext2_stat_bounded(
        &io, path, (uint32_t)strlen(path),
        REIST_VFS_SHADOW_EXT2_NOFOLLOW_FINAL,
        context->now_ms + 10000U, info);
}

static int stat_path(context_t *context, const char *path,
                     x86os_file_info_t *info) {
    reist_vfs_shadow_ext2_io_t io = io_for(context);
    return reist_vfs_shadow_ext2_stat_bounded(
        &io, path, (uint32_t)strlen(path), 0U,
        context->now_ms + 10000U, info);
}

static int readlink_path(context_t *context, const char *path,
                         char target[X86OS_VFS_SYMLINK_TARGET_CAPACITY],
                         uint32_t *length) {
    reist_vfs_shadow_ext2_io_t io = io_for(context);
    return reist_vfs_shadow_ext2_readlink(
        &io, path, (uint32_t)strlen(path), target, length,
        context->now_ms + 10000U);
}

static int create_link(context_t *context, const char *target,
                       const char *path) {
    reist_vfs_shadow_ext2_io_t io = io_for(context);
    return reist_vfs_shadow_ext2_symlink(
        &io, target, (uint32_t)strlen(target), path,
        (uint32_t)strlen(path), context->now_ms + 10000U);
}

static int unlink_link(context_t *context, const char *path) {
    reist_vfs_shadow_ext2_io_t io = io_for(context);
    return reist_vfs_shadow_ext2_unlink_symlink(
        &io, path, (uint32_t)strlen(path), context->now_ms + 10000U);
}

static int unlink_entry(context_t *context, const char *path) {
    reist_vfs_shadow_ext2_io_t io = io_for(context);
    return reist_vfs_shadow_ext2_unlink(
        &io, path, (uint32_t)strlen(path), context->now_ms + 10000U);
}

static int rename_entry(context_t *context, const char *source,
                        const char *destination) {
    reist_vfs_shadow_ext2_io_t io = io_for(context);
    return reist_vfs_shadow_ext2_rename(
        &io, source, (uint32_t)strlen(source), destination,
        (uint32_t)strlen(destination), context->now_ms + 10000U);
}

static int rename_link_only(context_t *context, const char *source,
                            const char *destination) {
    reist_vfs_shadow_ext2_io_t io = io_for(context);
    return reist_vfs_shadow_ext2_rename_symlink(
        &io, source, (uint32_t)strlen(source), destination,
        (uint32_t)strlen(destination), context->now_ms + 10000U);
}

static int read_path(context_t *context, const char *path, uint8_t *data,
                     uint32_t capacity, uint32_t *transferred) {
    reist_vfs_shadow_ext2_io_t io = io_for(context);
    return reist_vfs_shadow_ext2_read_bounded(
        &io, path, (uint32_t)strlen(path), 0U, data, capacity,
        context->now_ms + 10000U, transferred);
}

static int recover_link(context_t *context, const char *path,
                        const char *expected_target, int created) {
    context->fail_write = UINT32_MAX;
    context->fail_flush = UINT32_MAX;
    context->writes = 0U;
    context->flushes = 0U;
    reist_vfs_shadow_ext2_io_t io = io_for(context);
    CHECK(reist_vfs_shadow_ext2_recover_path(
        &io, path, (uint32_t)strlen(path),
        context->now_ms + 10000U) == 0);
    x86os_file_info_t info;
    int visible = lstat_path(context, path, &info);
    CHECK(visible == -2 || visible == 0);
    if (visible == 0) {
        char target[X86OS_VFS_SYMLINK_TARGET_CAPACITY];
        uint32_t length = 0U;
        CHECK(info.type == X86OS_SYMLINK);
        CHECK(readlink_path(context, path, target, &length) == 0);
        CHECK(length == strlen(expected_target));
        CHECK(memcmp(target, expected_target, length) == 0);
    }
    CHECK(created == 0 || created == -5);
    CHECK(context->reads <=
          REIST_VFS_SHADOW_EXT2_MAX_TRANSACTION_READS * 4U);
    return 0;
}

static int recover_unlink(context_t *context, const char *path,
                          const char *expected_target,
                          uint32_t inode_number, uint32_t target_block) {
    context->fail_write = UINT32_MAX;
    context->fail_flush = UINT32_MAX;
    context->writes = 0U;
    context->flushes = 0U;
    reist_vfs_shadow_ext2_io_t io = io_for(context);
    CHECK(reist_vfs_shadow_ext2_recover_path(
        &io, path, (uint32_t)strlen(path),
        context->now_ms + 10000U) == 0);
    x86os_file_info_t info;
    int visible = lstat_path(context, path, &info);
    CHECK(visible == 0 || visible == -2);
    uint32_t inode_bit = inode_number - 1U;
    uint8_t inode_allocated = (uint8_t)(
        block_at(context, 4U)[inode_bit / 8U] &
        (uint8_t)(1U << (inode_bit & 7U)));
    uint8_t block_allocated = 0U;
    if (target_block != 0U) {
        uint32_t block_bit = target_block - 1U;
        block_allocated = (uint8_t)(
            block_at(context, 3U)[block_bit / 8U] &
            (uint8_t)(1U << (block_bit & 7U)));
    }
    if (visible == 0) {
        char target[X86OS_VFS_SYMLINK_TARGET_CAPACITY];
        uint32_t length = 0U;
        CHECK(info.type == X86OS_SYMLINK);
        CHECK(readlink_path(context, path, target, &length) == 0);
        CHECK(length == strlen(expected_target));
        CHECK(memcmp(target, expected_target, length) == 0);
        CHECK(inode_allocated != 0U);
        CHECK(target_block == 0U || block_allocated != 0U);
        CHECK(read32(block_at(context, 1U) + 16U) == 106U);
        CHECK(read16(block_at(context, 2U) + 14U) == 106U);
        CHECK(read32(block_at(context, 1U) + 12U) == 216U);
        CHECK(read16(block_at(context, 2U) + 12U) == 216U);
    } else {
        CHECK(inode_allocated == 0U);
        CHECK(target_block == 0U || block_allocated == 0U);
        CHECK(read32(block_at(context, 1U) + 16U) == 107U);
        CHECK(read16(block_at(context, 2U) + 14U) == 107U);
        CHECK(read32(block_at(context, 1U) + 12U) ==
              (target_block == 0U ? 216U : 217U));
        CHECK(read16(block_at(context, 2U) + 12U) ==
              (target_block == 0U ? 216U : 217U));
        for (uint32_t index = 0U; index < 128U; ++index)
            CHECK(inode_at(context, inode_number)[index] == 0U);
    }
    CHECK(stat_path(context, "/mnt/ext2/target.txt", &info) == 0);
    CHECK(info.type == X86OS_FILE && info.size == 8U);
    return 0;
}

static int recover_rename(context_t *context, const char *source,
                          const char *destination,
                          const char *expected_target) {
    context->fail_write = UINT32_MAX;
    context->fail_flush = UINT32_MAX;
    context->writes = 0U;
    context->flushes = 0U;
    reist_vfs_shadow_ext2_io_t io = io_for(context);
    CHECK(reist_vfs_shadow_ext2_recover_path(
        &io, source, (uint32_t)strlen(source),
        context->now_ms + 10000U) == 0);
    x86os_file_info_t source_info;
    x86os_file_info_t destination_info;
    int source_visible = lstat_path(context, source, &source_info);
    int destination_visible = lstat_path(
        context, destination, &destination_info);
    CHECK((source_visible == 0 && destination_visible == -2) ||
          (source_visible == -2 && destination_visible == 0));
    const char *visible = source_visible == 0 ? source : destination;
    char target[X86OS_VFS_SYMLINK_TARGET_CAPACITY];
    uint32_t length = 0U;
    CHECK(readlink_path(context, visible, target, &length) == 0);
    CHECK(length == strlen(expected_target));
    CHECK(memcmp(target, expected_target, length) == 0);
    CHECK(read32(block_at(context, 1U) + 16U) == 106U);
    CHECK(read16(block_at(context, 2U) + 14U) == 106U);
    CHECK(read32(block_at(context, 1U) + 12U) == 216U);
    CHECK(read16(block_at(context, 2U) + 12U) == 216U);
    for (uint32_t index = X86OS_STORAGE_BLOCK_SIZE;
         index < BLOCK_SIZE; ++index)
        CHECK(block_at(context, 21U)[index] == 0U);
    return 0;
}

static int recover_regular_rename(context_t *context, const char *source,
                                  const char *destination,
                                  const uint8_t expected_inode[128U],
                                  const uint8_t expected_data[BLOCK_SIZE]) {
    context->fail_write = UINT32_MAX;
    context->fail_flush = UINT32_MAX;
    context->writes = 0U;
    context->flushes = 0U;
    reist_vfs_shadow_ext2_io_t io = io_for(context);
    CHECK(reist_vfs_shadow_ext2_recover_path(
        &io, source, (uint32_t)strlen(source),
        context->now_ms + 10000U) == 0);
    x86os_file_info_t source_info;
    x86os_file_info_t destination_info;
    int source_visible = lstat_path(context, source, &source_info);
    int destination_visible = lstat_path(
        context, destination, &destination_info);
    CHECK((source_visible == 0 && destination_visible == -2) ||
          (source_visible == -2 && destination_visible == 0));
    const char *visible = source_visible == 0 ? source : destination;
    const x86os_file_info_t *visible_info = source_visible == 0
        ? &source_info : &destination_info;
    CHECK(visible_info->type == X86OS_FILE && visible_info->size == 8U);
    uint8_t data[8U];
    uint32_t transferred = 0U;
    CHECK(read_path(context, visible, data, sizeof(data), &transferred) == 0);
    CHECK(transferred == sizeof(data));
    CHECK(memcmp(data, "payload\n", sizeof(data)) == 0);
    CHECK(memcmp(inode_at(context, 12U), expected_inode, 128U) == 0);
    CHECK(memcmp(block_at(context, 22U), expected_data, BLOCK_SIZE) == 0);
    CHECK(read32(block_at(context, 1U) + 16U) == 106U);
    CHECK(read16(block_at(context, 2U) + 14U) == 106U);
    CHECK(read32(block_at(context, 1U) + 12U) == 216U);
    CHECK(read16(block_at(context, 2U) + 12U) == 216U);
    CHECK(read32(block_at(context, 25U) + 8U) == 0U);
    CHECK(read32(block_at(context, 25U) + 512U + 8U) == 0U);
    for (uint32_t index = X86OS_STORAGE_BLOCK_SIZE;
         index < BLOCK_SIZE; ++index)
        CHECK(block_at(context, 21U)[index] == 0U);
    return 0;
}

static int regular_data_unchanged(context_t *context, uint8_t indirect) {
    if (indirect == 0U) {
        CHECK(memcmp(block_at(context, 22U), "payload\n", 8U) == 0);
        for (uint32_t index = 8U; index < BLOCK_SIZE; ++index)
            CHECK(block_at(context, 22U)[index] == 0U);
        return 0;
    }
    for (uint32_t index = 0U; index < 13U; ++index) {
        uint32_t block = regular_allocation_block(index);
        for (uint32_t byte = 0U; byte < BLOCK_SIZE; ++byte)
            CHECK(block_at(context, block)[byte] == (uint8_t)block);
    }
    CHECK(read32(block_at(context, 52U)) == 51U);
    for (uint32_t byte = 4U; byte < BLOCK_SIZE; ++byte)
        CHECK(block_at(context, 52U)[byte] == 0U);
    return 0;
}

static int recover_regular_unlink(context_t *context, const char *path,
                                  uint8_t indirect) {
    context->fail_write = UINT32_MAX;
    context->fail_flush = UINT32_MAX;
    context->writes = 0U;
    context->flushes = 0U;
    reist_vfs_shadow_ext2_io_t io = io_for(context);
    CHECK(reist_vfs_shadow_ext2_recover_path(
        &io, path, (uint32_t)strlen(path),
        context->now_ms + 10000U) == 0);
    x86os_file_info_t info;
    int visible = lstat_path(context, path, &info);
    CHECK(visible == 0 || visible == -2);
    uint32_t allocation_count = indirect != 0U ? 14U : 1U;
    for (uint32_t index = 0U; index < allocation_count; ++index) {
        uint32_t block = indirect != 0U
            ? regular_allocation_block(index) : 22U;
        uint32_t bit = block - 1U;
        uint8_t allocated = (uint8_t)(block_at(context, 3U)[bit / 8U] &
            (uint8_t)(1U << (bit & 7U)));
        CHECK((visible == 0 && allocated != 0U) ||
              (visible == -2 && allocated == 0U));
    }
    uint32_t inode_bit = 11U;
    uint8_t inode_allocated = (uint8_t)(
        block_at(context, 4U)[inode_bit / 8U] &
        (uint8_t)(1U << (inode_bit & 7U)));
    uint32_t initial_free = indirect != 0U ? 203U : 216U;
    if (visible == 0) {
        CHECK(info.type == X86OS_FILE);
        CHECK(info.size == (indirect != 0U ? 13U * BLOCK_SIZE : 8U));
        CHECK(inode_allocated != 0U);
        CHECK(read32(block_at(context, 1U) + 12U) == initial_free);
        CHECK(read16(block_at(context, 2U) + 12U) == initial_free);
        CHECK(read32(block_at(context, 1U) + 16U) == 106U);
        CHECK(read16(block_at(context, 2U) + 14U) == 106U);
    } else {
        CHECK(inode_allocated == 0U);
        CHECK(read32(block_at(context, 1U) + 12U) ==
              initial_free + allocation_count);
        CHECK(read16(block_at(context, 2U) + 12U) ==
              initial_free + allocation_count);
        CHECK(read32(block_at(context, 1U) + 16U) == 107U);
        CHECK(read16(block_at(context, 2U) + 14U) == 107U);
        for (uint32_t index = 0U; index < 128U; ++index)
            CHECK(inode_at(context, 12U)[index] == 0U);
    }
    CHECK(regular_data_unchanged(context, indirect) == 0);
    CHECK(lstat_path(context, "/mnt/ext2/fast-link", &info) == 0);
    CHECK(info.type == X86OS_SYMLINK);
    CHECK(read32(block_at(context, 25U) + 8U) == 0U);
    CHECK(read32(block_at(context, 25U) + 512U + 8U) == 0U);
    return 0;
}

static x86os_storage_submit_t client_request;
static x86os_vfs_symlink_frame_t client_frame;
static uint64_t client_now;
static uint8_t client_corrupt_reply;
static uint8_t client_recover_once;
static uint32_t client_submits;

int x86os_getcwd(char *buffer, size_t size) {
    const char *cwd = "/mnt/ext2";
    if (buffer == 0 || size <= strlen(cwd)) return -22;
    strcpy(buffer, cwd);
    return 0;
}

int x86os_drive_info(uint32_t index, x86os_drive_info_t *info) {
    (void)index;
    (void)info;
    return 0;
}

int x86os_storage_submit(const x86os_storage_submit_t *request,
                         const void *data, x86os_storage_handle_t *handle) {
    if (request == 0 || data == 0 || handle == 0) return -22;
    client_request = *request;
    memcpy(&client_frame, data, sizeof(client_frame));
    ++client_submits;
    *handle = 0x10001U;
    return 0;
}

int x86os_storage_collect(x86os_storage_handle_t handle, int32_t *result,
                          void *data) {
    if (handle != 0x10001U || result == 0 || data == 0) return -22;
    if (client_frame.operation == X86OS_VFS_SHADOW_FS_READLINK) {
        memcpy(client_frame.target, "target.txt", 10U);
        client_frame.target[10U] = '\0';
        client_frame.target_length = 10U;
    }
    if (client_corrupt_reply != 0U) client_frame.path[1U] ^= 1;
    client_frame.result = client_recover_once != 0U && client_submits == 1U
        ? -11 : 0;
    memcpy(data, &client_frame, sizeof(client_frame));
    *result = 0;
    return 0;
}

int x86os_storage_cancel(x86os_storage_handle_t handle) {
    return handle == 0x10001U ? 0 : -22;
}

int x86os_monotonic_ms(uint64_t *value) {
    if (value == 0) return -22;
    *value = client_now++;
    return 0;
}

int x86os_sleep_ms(uint32_t milliseconds) {
    (void)milliseconds;
    return 0;
}

int x86os_yield(void) {
    return 0;
}

static int client_contract(void) {
    memset(&client_request, 0, sizeof(client_request));
    client_now = 1U;
    client_corrupt_reply = 0U;
    client_recover_once = 0U;
    client_submits = 0U;
    CHECK(reist_vfs_symlink("target.txt", "created", 100U) == 0);
    CHECK(client_submits == 1U);
    CHECK(client_request.operation == X86OS_STORAGE_VFS_SYMLINK);
    CHECK(client_request.length == X86OS_STORAGE_BLOCK_SIZE);
    CHECK(client_frame.operation == X86OS_VFS_SHADOW_FS_SYMLINK);
    CHECK(strcmp(client_frame.path, "/mnt/ext2/created") == 0);
    CHECK(client_frame.target_length == 10U);
    char short_target[5U] = {'X', 'X', 'X', 'X', 'X'};
    CHECK(reist_vfs_readlink("fast-link", short_target, 4U, 100U) == 4);
    CHECK(memcmp(short_target, "targ", 4U) == 0);
    CHECK(short_target[4U] == 'X');
    CHECK(client_request.operation == X86OS_STORAGE_VFS_SHADOW_STAT);
    CHECK(client_frame.operation == X86OS_VFS_SHADOW_FS_READLINK);
    CHECK(reist_vfs_symlink("", "bad", 100U) == -36);
    client_submits = 0U;
    client_recover_once = 1U;
    CHECK(reist_vfs_symlink("target.txt", "recovered", 100U) == 0);
    CHECK(client_submits == 2U);
    client_recover_once = 0U;
    client_corrupt_reply = 1U;
    CHECK(reist_vfs_readlink("fast-link", short_target, 4U, 100U) == -84);
    return 0;
}

int main(void) {
    CHECK(client_contract() == 0);
    static context_t context;
    initialize(&context);
    x86os_file_info_t info;
    char target[X86OS_VFS_SYMLINK_TARGET_CAPACITY];
    uint32_t length = 0U;
    CHECK(lstat_path(&context, "/mnt/ext2/fast-link", &info) == 0);
    CHECK(info.type == X86OS_SYMLINK && info.size == 10U);
    CHECK(stat_path(&context, "/mnt/ext2/fast-link", &info) == 0);
    CHECK(info.type == X86OS_FILE && info.size == 8U);
    CHECK(readlink_path(&context, "/mnt/ext2/fast-link", target, &length) == 0);
    CHECK(length == 10U && memcmp(target, "target.txt", 10U) == 0);
    CHECK(stat_path(&context, "/mnt/ext2/absolute-link", &info) == 0);
    CHECK(stat_path(&context, "/mnt/ext2/chain-link", &info) == 0);
    CHECK(stat_path(&context, "/mnt/ext2/dir/relative-link", &info) == 0);
    CHECK(stat_path(&context, "/mnt/ext2/dangling-link", &info) == -2);
    CHECK(lstat_path(&context, "/mnt/ext2/dangling-link", &info) == 0);
    CHECK(info.type == X86OS_SYMLINK);
    CHECK(stat_path(&context, "/mnt/ext2/cycle-a", &info) == -40);
    CHECK(stat_path(&context, "/mnt/ext2/long-link", &info) == 0);
    CHECK(readlink_path(&context, "/mnt/ext2/target.txt", target, &length) ==
          -22);

    inode_at(&context, 14U)[40U] = 1U;
    CHECK(stat_path(&context, "/mnt/ext2/fast-link", &info) == -5);
    inode_at(&context, 14U)[40U] = 't';
    reist_vfs_shadow_ext2_io_t expired_io = io_for(&context);
    memset(&info, 0xA5, sizeof(info));
    CHECK(reist_vfs_shadow_ext2_stat_bounded(
        &expired_io, "/mnt/ext2/target.txt", 20U, 0U,
        context.now_ms, &info) == -110);
    CHECK(info.type == 0U);

    reist_vfs_shadow_ext2_io_t io = io_for(&context);
    reist_vfs_shadow_object_t object;
    CHECK(reist_vfs_shadow_ext2_object_open_bounded(
        &io, "/mnt/ext2/fast-link", 19U, 0U,
        context.now_ms + 10000U, &object, &info) == 0);
    CHECK(object.locator_a == 12U);
    memset(&object, 0xA5, sizeof(object));
    CHECK(reist_vfs_shadow_ext2_object_open_bounded(
        &io, "/mnt/ext2/fast-link", 19U, X86OS_O_NOFOLLOW,
        context.now_ms + 10000U, &object, &info) == -40);
    CHECK(object.version == 0U);

    CHECK(create_link(&context, "target.txt", "/mnt/ext2/created") == 0);
    CHECK(readlink_path(&context, "/mnt/ext2/created", target, &length) == 0);
    CHECK(length == 10U && memcmp(target, "target.txt", 10U) == 0);
    CHECK(stat_path(&context, "/mnt/ext2/created", &info) == 0);
    CHECK(create_link(&context, "target.txt", "/mnt/ext2/created") == -17);
    const char *long_target =
        "dir/../dir/../dir/../dir/../dir/../dir/../dir/../dir/../target.txt";
    CHECK(create_link(&context, long_target, "/mnt/ext2/created-long") == 0);
    CHECK(readlink_path(
        &context, "/mnt/ext2/created-long", target, &length) == 0);
    CHECK(length == strlen(long_target) &&
          memcmp(target, long_target, length) == 0);
    uint32_t writes_before = context.writes;
    CHECK(create_link(&context, "target.txt", "/unsupported") == -95);
    CHECK(context.writes == writes_before);

    CHECK(unlink_link(&context, "/mnt/ext2/fast-link") == 0);
    CHECK(lstat_path(&context, "/mnt/ext2/fast-link", &info) == -2);
    CHECK(stat_path(&context, "/mnt/ext2/target.txt", &info) == 0);
    CHECK(unlink_link(&context, "/mnt/ext2/target.txt") == -95);
    CHECK(rename_entry(
        &context, "/mnt/ext2/absolute-link",
        "/mnt/ext2/renamed-link") == 0);
    CHECK(lstat_path(&context, "/mnt/ext2/absolute-link", &info) == -2);
    CHECK(readlink_path(
        &context, "/mnt/ext2/renamed-link", target, &length) == 0);
    CHECK(length == strlen("/mnt/ext2/target.txt"));
    CHECK(rename_entry(
        &context, "/mnt/ext2/renamed-link",
        "/mnt/ext2/dangling-link") == -17);
    CHECK(rename_entry(
        &context, "/mnt/ext2/renamed-link",
        "/mnt/ext2/dir/moved-link") == -18);
    CHECK(rename_link_only(
        &context, "/mnt/ext2/target.txt",
        "/mnt/ext2/target-new") == -95);
    CHECK(rename_entry(
        &context, "/mnt/ext2/dir",
        "/mnt/ext2/dir-new") == -95);
    initialize_without_source_sector_slack(&context);
    uint32_t rename_reject_writes = context.writes;
    uint32_t rename_reject_flushes = context.flushes;
    CHECK(rename_entry(
        &context, "/mnt/ext2/absolute-link",
        "/mnt/ext2/renamed-symbolic-link-long") == -28);
    CHECK(context.writes == rename_reject_writes);
    CHECK(context.flushes == rename_reject_flushes);
    CHECK(readlink_path(
        &context, "/mnt/ext2/absolute-link", target, &length) == 0);

    const char *failure_targets[2U] = {"target.txt", long_target};
    const char *failure_paths[2U] = {
        "/mnt/ext2/interrupted", "/mnt/ext2/interrupted-long"
    };
    for (uint32_t variant = 0U; variant < 2U; ++variant) {
        for (uint32_t failure = 0U; failure < 24U; ++failure) {
            initialize(&context);
            context.fail_write = failure;
            int created = create_link(
                &context, failure_targets[variant], failure_paths[variant]);
            CHECK(recover_link(
                &context, failure_paths[variant], failure_targets[variant],
                created) == 0);
        }
        for (uint32_t failure = 0U; failure < 8U; ++failure) {
            initialize(&context);
            context.fail_flush = failure;
            int created = create_link(
                &context, failure_targets[variant], failure_paths[variant]);
            CHECK(recover_link(
                &context, failure_paths[variant], failure_targets[variant],
                created) == 0);
        }
    }

    const char *unlink_paths[2U] = {
        "/mnt/ext2/fast-link", "/mnt/ext2/long-link"
    };
    const char *unlink_targets[2U] = {"target.txt", long_target};
    for (uint32_t variant = 0U; variant < 2U; ++variant) {
        initialize(&context);
        CHECK(unlink_link(&context, unlink_paths[variant]) == 0);
        uint32_t successful_writes = context.writes;
        uint32_t successful_flushes = context.flushes;
        for (uint32_t failure = 0U; failure < successful_writes; ++failure) {
            initialize(&context);
            context.fail_write = failure;
            int removed = unlink_link(&context, unlink_paths[variant]);
            CHECK(removed == 0 || removed == -5);
            CHECK(recover_unlink(
                &context, unlink_paths[variant],
                unlink_targets[variant], variant == 0U ? 14U : 20U,
                variant == 0U ? 0U : 39U) == 0);
        }
        for (uint32_t failure = 0U; failure < successful_flushes; ++failure) {
            initialize(&context);
            context.fail_flush = failure;
            int removed = unlink_link(&context, unlink_paths[variant]);
            CHECK(removed == 0 || removed == -5);
            CHECK(recover_unlink(
                &context, unlink_paths[variant],
                unlink_targets[variant], variant == 0U ? 14U : 20U,
                variant == 0U ? 0U : 39U) == 0);
        }
    }

    initialize(&context);
    CHECK(rename_entry(
        &context, "/mnt/ext2/absolute-link",
        "/mnt/ext2/renamed-symbolic-link-long") == 0);
    uint32_t rename_writes = context.writes;
    uint32_t rename_flushes = context.flushes;
    for (uint32_t failure = 0U; failure < rename_writes; ++failure) {
        initialize(&context);
        context.fail_write = failure;
        int renamed = rename_entry(
            &context, "/mnt/ext2/absolute-link",
            "/mnt/ext2/renamed-symbolic-link-long");
        CHECK(renamed == 0 || renamed == -5);
        CHECK(recover_rename(
            &context, "/mnt/ext2/absolute-link",
            "/mnt/ext2/renamed-symbolic-link-long",
            "/mnt/ext2/target.txt") == 0);
    }
    for (uint32_t failure = 0U; failure < rename_flushes; ++failure) {
        initialize(&context);
        context.fail_flush = failure;
        int renamed = rename_entry(
            &context, "/mnt/ext2/absolute-link",
            "/mnt/ext2/renamed-symbolic-link-long");
        CHECK(renamed == 0 || renamed == -5);
        CHECK(recover_rename(
            &context, "/mnt/ext2/absolute-link",
            "/mnt/ext2/renamed-symbolic-link-long",
            "/mnt/ext2/target.txt") == 0);
    }

    initialize(&context);
    uint8_t regular_inode[128U];
    uint8_t regular_data[BLOCK_SIZE];
    memcpy(regular_inode, inode_at(&context, 12U), sizeof(regular_inode));
    memcpy(regular_data, block_at(&context, 22U), sizeof(regular_data));
    uint32_t regular_reject_writes = context.writes;
    uint32_t regular_reject_flushes = context.flushes;
    CHECK(rename_entry(
        &context, "/mnt/ext2/target.txt",
        "/mnt/ext2/dangling-link") == -17);
    CHECK(rename_entry(
        &context, "/mnt/ext2/target.txt",
        "/mnt/ext2/dir/moved.txt") == -18);
    CHECK(context.writes == regular_reject_writes);
    CHECK(context.flushes == regular_reject_flushes);
    CHECK(memcmp(inode_at(&context, 12U), regular_inode, 128U) == 0);
    CHECK(memcmp(block_at(&context, 22U), regular_data, BLOCK_SIZE) == 0);
    initialize_without_source_sector_slack(&context);
    regular_reject_writes = context.writes;
    regular_reject_flushes = context.flushes;
    CHECK(rename_entry(
        &context, "/mnt/ext2/target.txt",
        "/mnt/ext2/regular-file-renamed-long.txt") == -28);
    CHECK(context.writes == regular_reject_writes);
    CHECK(context.flushes == regular_reject_flushes);
    CHECK(stat_path(&context, "/mnt/ext2/target.txt", &info) == 0);

    initialize(&context);
    memcpy(regular_inode, inode_at(&context, 12U), sizeof(regular_inode));
    memcpy(regular_data, block_at(&context, 22U), sizeof(regular_data));
    CHECK(rename_entry(
        &context, "/mnt/ext2/target.txt",
        "/mnt/ext2/regular-file-renamed-long.txt") == 0);
    uint32_t regular_rename_writes = context.writes;
    uint32_t regular_rename_flushes = context.flushes;
    CHECK(recover_regular_rename(
        &context, "/mnt/ext2/target.txt",
        "/mnt/ext2/regular-file-renamed-long.txt",
        regular_inode, regular_data) == 0);
    for (uint32_t failure = 0U;
         failure < regular_rename_writes; ++failure) {
        initialize(&context);
        memcpy(regular_inode, inode_at(&context, 12U),
               sizeof(regular_inode));
        memcpy(regular_data, block_at(&context, 22U),
               sizeof(regular_data));
        context.fail_write = failure;
        int renamed = rename_entry(
            &context, "/mnt/ext2/target.txt",
            "/mnt/ext2/regular-file-renamed-long.txt");
        CHECK(renamed == 0 || renamed == -5);
        CHECK(recover_regular_rename(
            &context, "/mnt/ext2/target.txt",
            "/mnt/ext2/regular-file-renamed-long.txt",
            regular_inode, regular_data) == 0);
    }
    for (uint32_t failure = 0U;
         failure < regular_rename_flushes; ++failure) {
        initialize(&context);
        memcpy(regular_inode, inode_at(&context, 12U),
               sizeof(regular_inode));
        memcpy(regular_data, block_at(&context, 22U),
               sizeof(regular_data));
        context.fail_flush = failure;
        int renamed = rename_entry(
            &context, "/mnt/ext2/target.txt",
            "/mnt/ext2/regular-file-renamed-long.txt");
        CHECK(renamed == 0 || renamed == -5);
        CHECK(recover_regular_rename(
            &context, "/mnt/ext2/target.txt",
            "/mnt/ext2/regular-file-renamed-long.txt",
            regular_inode, regular_data) == 0);
    }

    for (uint32_t variant = 0U; variant < 2U; ++variant) {
        if (variant == 0U) initialize(&context);
        else initialize_indirect_regular(&context);
        CHECK(unlink_entry(&context, "/mnt/ext2/target.txt") == 0);
        uint32_t regular_unlink_writes = context.writes;
        uint32_t regular_unlink_flushes = context.flushes;
        CHECK(recover_regular_unlink(
            &context, "/mnt/ext2/target.txt", (uint8_t)variant) == 0);
        for (uint32_t failure = 0U;
             failure < regular_unlink_writes; ++failure) {
            if (variant == 0U) initialize(&context);
            else initialize_indirect_regular(&context);
            context.fail_write = failure;
            int removed = unlink_entry(&context, "/mnt/ext2/target.txt");
            CHECK(removed == 0 || removed == -5);
            CHECK(recover_regular_unlink(
                &context, "/mnt/ext2/target.txt", (uint8_t)variant) == 0);
        }
        for (uint32_t failure = 0U;
             failure < regular_unlink_flushes; ++failure) {
            if (variant == 0U) initialize(&context);
            else initialize_indirect_regular(&context);
            context.fail_flush = failure;
            int removed = unlink_entry(&context, "/mnt/ext2/target.txt");
            CHECK(removed == 0 || removed == -5);
            CHECK(recover_regular_unlink(
                &context, "/mnt/ext2/target.txt", (uint8_t)variant) == 0);
        }
    }

    initialize(&context);
    uint32_t regular_unlink_reject_writes = context.writes;
    uint32_t regular_unlink_reject_flushes = context.flushes;
    CHECK(unlink_entry(&context, "/mnt/ext2/dir") == -95);
    CHECK(unlink_entry(
        &context, "/mnt/ext2/.reist-symlink-journal") == -16);
    put16(inode_at(&context, 12U) + 26U, 2U);
    CHECK(unlink_entry(&context, "/mnt/ext2/target.txt") == -31);
    put16(inode_at(&context, 12U) + 26U, 1U);
    put32(inode_at(&context, 12U) + 40U, 3U);
    CHECK(unlink_entry(&context, "/mnt/ext2/target.txt") == -5);
    put32(inode_at(&context, 12U) + 40U, 21U);
    CHECK(unlink_entry(&context, "/mnt/ext2/target.txt") == -5);
    put32(inode_at(&context, 12U) + 40U, 25U);
    CHECK(unlink_entry(&context, "/mnt/ext2/target.txt") == -5);
    put32(inode_at(&context, 12U) + 40U, 54U);
    CHECK(unlink_entry(&context, "/mnt/ext2/target.txt") == -5);
    CHECK(context.writes == regular_unlink_reject_writes);
    CHECK(context.flushes == regular_unlink_reject_flushes);

    initialize_indirect_regular(&context);
    put32(block_at(&context, 52U), 22U);
    CHECK(unlink_entry(&context, "/mnt/ext2/target.txt") == -5);
    CHECK(context.writes == 0U && context.flushes == 0U);
    initialize_indirect_regular(&context);
    put32(inode_at(&context, 12U) + 4U, 64U * BLOCK_SIZE);
    CHECK(unlink_entry(&context, "/mnt/ext2/target.txt") == -27);
    CHECK(context.writes == 0U && context.flushes == 0U);
    initialize(&context);
    put32(inode_at(&context, 12U) + 32U, 1U);
    CHECK(unlink_entry(&context, "/mnt/ext2/target.txt") == -95);
    CHECK(context.writes == 0U && context.flushes == 0U);
    return 0;
}
