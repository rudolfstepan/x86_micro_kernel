/**
 * @file userspace/storage/lib/vfs_shadow_ext2.c
 * @brief Independent bounded Linux EXT2 and symbolic-link engine for Ring 3.
 *
 * The supported subset is revision 0/1 EXT2 with 1, 2 or 4 KiB blocks,
 * linear directories and direct or single-indirect blocks.  Native fast and
 * block-backed symbolic links are resolved here, outside Ring 0.  Creation is
 * guarded by a fixed on-volume undo journal provisioned by the image builder.
 * Every read, write, flush, path component and link hop is explicitly bounded.
 */
#include "../include/reist/vfs_shadow_ext2.h"

#define EXT2_SIGNATURE 0xEF53U
#define EXT2_ROOT_INODE 2U
#define EXT2_SUPERBLOCK_SECTOR 2U
#define EXT2_SUPERBLOCK_SECTORS 2U
#define EXT2_GROUP_DESCRIPTOR_SIZE 32U
#define EXT2_INODE_CORE_SIZE 128U
#define EXT2_DIRECT_BLOCKS 12U
#define EXT2_SINGLE_INDIRECT_INDEX 12U
#define EXT2_INCOMPAT_FILETYPE 0x00000002U
#define EXT2_RO_COMPAT_SUPPORTED 0x00000003U
#define EXT2_INDEX_FL 0x00001000U
#define EXT2_S_IFMT 0xF000U
#define EXT2_S_IFREG 0x8000U
#define EXT2_S_IFDIR 0x4000U
#define EXT2_S_IFLNK 0xA000U
#define EXT2_FT_SYMLINK 7U
#define EXT2_FAST_SYMLINK_CAPACITY 60U
#define EXT2_DIRECTORY_HEADER_SIZE 8U
#define EXT2_JOURNAL_MAGIC 0x4B4E4C53U
#define EXT2_JOURNAL_VERSION 1U
#define EXT2_JOURNAL_STATE_CLEAN 0U
#define EXT2_JOURNAL_STATE_ACTIVE 1U
#define EXT2_JOURNAL_STATE_COMMITTED 2U
#define EXT2_JOURNAL_HEADER_FIXED 32U
#define EXT2_JOURNAL_ENTRY_SIZE 16U
#define EXT2_JOURNAL_NAME ".reist-symlink-journal"

typedef struct {
    void *context;
    reist_vfs_shadow_drive_info_fn drive_info;
    reist_vfs_shadow_read_sector_fn read_sector;
    reist_vfs_shadow_ext2_write_sector_fn write_sector;
    reist_vfs_shadow_ext2_flush_fn flush;
    reist_vfs_shadow_ext2_monotonic_fn monotonic_ms;
    uint64_t deadline_ms;
    uint64_t last_ms;
    uint32_t read_limit;
    uint32_t reads;
    uint32_t writes;
    uint32_t flushes;
    uint8_t clock_seen;
} ext2_request_t;

typedef struct {
    uint32_t resource;
    uint32_t sectors;
    uint32_t blocks_count;
    uint32_t inodes_count;
    uint32_t first_data_block;
    uint32_t block_size;
    uint32_t sectors_per_block;
    uint32_t blocks_per_group;
    uint32_t inodes_per_group;
    uint32_t inode_size;
    uint32_t first_inode;
    uint32_t group_count;
    uint32_t reads;
    uint32_t signature;
    uint8_t has_file_type;
    ext2_request_t *request;
} ext2_shadow_volume_t;

typedef struct {
    uint8_t bytes[EXT2_INODE_CORE_SIZE];
} ext2_shadow_inode_t;

typedef struct {
    uint32_t sector;
    uint32_t old_crc;
    uint32_t final_crc;
    uint8_t publish;
    uint8_t old_data[X86OS_STORAGE_BLOCK_SIZE];
    uint8_t final_data[X86OS_STORAGE_BLOCK_SIZE];
} ext2_transaction_entry_t;

typedef struct {
    uint32_t count;
    ext2_transaction_entry_t entries[
        REIST_VFS_SHADOW_EXT2_MAX_JOURNAL_ENTRIES];
} ext2_transaction_t;

typedef struct {
    uint32_t sectors[REIST_VFS_SHADOW_EXT2_JOURNAL_SECTORS];
    uint32_t indirect_block;
    uint32_t inode_number;
    uint32_t sequence;
    uint8_t present;
} ext2_journal_t;

typedef struct {
    uint32_t count;
    uint32_t blocks[REIST_VFS_SHADOW_EXT2_MAX_UNLINK_BLOCKS];
} ext2_regular_allocations_t;

/* The storage service is single-threaded; fixed storage avoids a 25 KiB stack
 * frame while preserving a heap-free, bounded transaction. */
static ext2_transaction_t ext2_transaction;

static void ext2_zero(void *target, uint32_t length);

static uint16_t ext2_get16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8U));
}

static uint32_t ext2_get32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static void ext2_put16(uint8_t *data, uint16_t value) {
    data[0U] = (uint8_t)value;
    data[1U] = (uint8_t)(value >> 8U);
}

static void ext2_put32(uint8_t *data, uint32_t value) {
    data[0U] = (uint8_t)value;
    data[1U] = (uint8_t)(value >> 8U);
    data[2U] = (uint8_t)(value >> 16U);
    data[3U] = (uint8_t)(value >> 24U);
}

static uint32_t ext2_crc32(const uint8_t *data, uint32_t length) {
    uint32_t crc = 0xFFFFFFFFU;
    for (uint32_t index = 0U; index < length; ++index) {
        crc ^= data[index];
        for (uint32_t bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1U) ^ (0xEDB88320U &
                (uint32_t)-(int32_t)(crc & 1U));
    }
    return ~crc;
}

static int ext2_request_from_legacy(ext2_request_t *request,
                                    const reist_vfs_shadow_io_t *io) {
    if (request == 0 || io == 0 || io->drive_info == 0 ||
        io->read_sector == 0) return -22;
    ext2_zero(request, sizeof(*request));
    request->context = io->context;
    request->drive_info = io->drive_info;
    request->read_sector = io->read_sector;
    request->deadline_ms = UINT64_MAX;
    request->read_limit = REIST_VFS_SHADOW_EXT2_MAX_SECTOR_READS;
    return 0;
}

static int ext2_request_from_extended(ext2_request_t *request,
        const reist_vfs_shadow_ext2_io_t *io, uint64_t deadline_ms,
        uint32_t read_limit) {
    if (request == 0 || io == 0 || io->drive_info == 0 ||
        io->read_sector == 0 || deadline_ms == 0U || read_limit == 0U ||
        read_limit > REIST_VFS_SHADOW_EXT2_MAX_TRANSACTION_READS) return -22;
    ext2_zero(request, sizeof(*request));
    request->context = io->context;
    request->drive_info = io->drive_info;
    request->read_sector = io->read_sector;
    request->write_sector = io->write_sector;
    request->flush = io->flush;
    request->monotonic_ms = io->monotonic_ms;
    request->deadline_ms = deadline_ms;
    request->read_limit = read_limit;
    return 0;
}

static int ext2_request_deadline(ext2_request_t *request) {
    if (request == 0) return -22;
    if (request->monotonic_ms == 0 || request->deadline_ms == UINT64_MAX)
        return 0;
    uint64_t now = 0U;
    if (request->monotonic_ms(request->context, &now) != 0 ||
        (request->clock_seen != 0U && now < request->last_ms)) return -5;
    request->last_ms = now;
    request->clock_seen = 1U;
    return now >= request->deadline_ms ? -110 : 0;
}

static int ext2_request_write(ext2_request_t *request, uint32_t resource,
        uint32_t sector, const uint8_t data[X86OS_STORAGE_BLOCK_SIZE]) {
    if (request == 0 || request->write_sector == 0 || data == 0) return -30;
    int status = ext2_request_deadline(request);
    if (status != 0) return status;
    if (request->writes >= REIST_VFS_SHADOW_EXT2_MAX_TRANSACTION_WRITES)
        return -110;
    ++request->writes;
    return request->write_sector(request->context, resource, sector, data) == 0
        ? 0 : -5;
}

static int ext2_request_flush(ext2_request_t *request, uint32_t resource) {
    if (request == 0 || request->flush == 0) return -30;
    int status = ext2_request_deadline(request);
    if (status != 0) return status;
    if (request->flushes >= REIST_VFS_SHADOW_EXT2_MAX_TRANSACTION_FLUSHES)
        return -110;
    ++request->flushes;
    return request->flush(request->context, resource) == 0 ? 0 : -5;
}

static uint32_t ext2_signature(const uint8_t *data, uint32_t length) {
    uint32_t signature = 2166136261U;
    for (uint32_t index = 0U; index < length; ++index)
        signature = (signature ^ data[index]) * 16777619U;
    return signature != 0U ? signature : 1U;
}

static uint32_t ext2_volume_signature(const uint8_t superblock[1024]) {
    uint8_t stable[1024];
    for (uint32_t index = 0U; index < sizeof(stable); ++index)
        stable[index] = superblock[index];
    /* Allocation counters and mount/check timestamps legitimately change
     * during a transaction and must not revoke unrelated open objects. */
    for (uint32_t index = 12U; index < 20U; ++index) stable[index] = 0U;
    for (uint32_t index = 44U; index < 56U; ++index) stable[index] = 0U;
    for (uint32_t index = 58U; index < 60U; ++index) stable[index] = 0U;
    for (uint32_t index = 64U; index < 68U; ++index) stable[index] = 0U;
    for (uint32_t index = 232U; index < 236U; ++index) stable[index] = 0U;
    return ext2_signature(stable, sizeof(stable));
}

static uint32_t ext2_div_ceil_u32(uint32_t value, uint32_t divisor) {
    return value / divisor + (value % divisor != 0U ? 1U : 0U);
}

static void ext2_zero(void *target, uint32_t length) {
    uint8_t *bytes = (uint8_t *)target;
    for (uint32_t index = 0U; index < length; ++index) bytes[index] = 0U;
}

static void ext2_copy(void *target, const void *source, uint32_t length) {
    uint8_t *out = (uint8_t *)target;
    const uint8_t *in = (const uint8_t *)source;
    for (uint32_t index = 0U; index < length; ++index) out[index] = in[index];
}

static uint32_t ext2_text_length(const char *text, uint32_t capacity) {
    uint32_t length = 0U;
    if (text == 0) return capacity;
    while (length < capacity && text[length] != '\0') ++length;
    return length;
}

static int ext2_path_prefix(const char *path, uint32_t path_length,
                            const char *mount, uint32_t mount_length) {
    if (mount_length == 0U || mount_length > path_length || mount[0] != '/')
        return 0;
    for (uint32_t index = 0U; index < mount_length; ++index)
        if (path[index] != mount[index]) return 0;
    if (mount_length == 1U) return 1;
    return mount_length == path_length || path[mount_length] == '/';
}

static int ext2_read_sector(ext2_shadow_volume_t *volume, uint32_t sector,
                            uint8_t data[X86OS_STORAGE_BLOCK_SIZE]) {
    if (volume == 0 || volume->request == 0 || data == 0 ||
        sector >= volume->sectors) return -5;
    int status = ext2_request_deadline(volume->request);
    if (status != 0) return status;
    volume->reads = volume->request->reads;
    if (volume->reads >= volume->request->read_limit) return -110;
    ++volume->request->reads;
    volume->reads = volume->request->reads;
    int read = volume->request->read_sector(
        volume->request->context, volume->resource, sector, data);
    return read == 0 ? 0 : read == -110 ? -110 : -5;
}

static int ext2_read_block(ext2_shadow_volume_t *volume, uint32_t block,
                           uint8_t data[REIST_VFS_SHADOW_EXT2_MAX_BLOCK_SIZE]) {
    if (block >= volume->blocks_count || volume->block_size == 0U ||
        volume->block_size > REIST_VFS_SHADOW_EXT2_MAX_BLOCK_SIZE)
        return -5;
    uint64_t first = (uint64_t)block * volume->sectors_per_block;
    if (first + volume->sectors_per_block > volume->sectors) return -5;
    for (uint32_t index = 0U; index < volume->sectors_per_block; ++index) {
        int status = ext2_read_sector(
            volume, (uint32_t)first + index,
            data + index * X86OS_STORAGE_BLOCK_SIZE);
        if (status != 0) return status;
    }
    return 0;
}

static int ext2_mount(ext2_shadow_volume_t *volume, ext2_request_t *request,
                      const char *path, uint32_t path_length,
                      uint32_t *relative_offset) {
    uint32_t best_resource = UINT32_MAX;
    uint32_t best_length = 0U;
    x86os_drive_info_t best;
    ext2_zero(&best, sizeof(best));
    for (uint32_t resource = 0U;
         resource < REIST_VFS_SHADOW_MAX_RESOURCES; ++resource) {
        x86os_drive_info_t candidate;
        ext2_zero(&candidate, sizeof(candidate));
        int status = ext2_request_deadline(request);
        if (status != 0) return status;
        status = request->drive_info(request->context, resource, &candidate);
        if (status == 0) break;
        if (status < 0) return status == -110 ? -110 : -5;
        uint32_t mount_length = ext2_text_length(
            candidate.mount_point, sizeof(candidate.mount_point));
        if (mount_length == 0U ||
            mount_length >= sizeof(candidate.mount_point) ||
            (mount_length > 1U &&
             candidate.mount_point[mount_length - 1U] == '/') ||
            !ext2_path_prefix(path, path_length, candidate.mount_point,
                              mount_length) ||
            mount_length <= best_length) continue;
        best_resource = resource;
        best_length = mount_length;
        best = candidate;
    }
    if (best_resource == UINT32_MAX || best.sectors == 0U) return -2;
    ext2_zero(volume, sizeof(*volume));
    volume->resource = best_resource;
    volume->sectors = best.sectors;
    volume->reads = request->reads;
    volume->request = request;
    *relative_offset = best_length == 1U ? 1U : best_length;
    if (*relative_offset < path_length && path[*relative_offset] == '/')
        ++*relative_offset;
    return 0;
}

static int ext2_parse_superblock(ext2_shadow_volume_t *volume) {
    uint8_t superblock[1024];
    for (uint32_t index = 0U; index < EXT2_SUPERBLOCK_SECTORS; ++index) {
        int status = ext2_read_sector(
            volume, EXT2_SUPERBLOCK_SECTOR + index,
            superblock + index * X86OS_STORAGE_BLOCK_SIZE);
        if (status != 0) return status;
    }
    volume->signature = ext2_volume_signature(superblock);
    uint32_t inodes_count = ext2_get32(superblock + 0U);
    uint32_t blocks_count = ext2_get32(superblock + 4U);
    uint32_t first_data_block = ext2_get32(superblock + 20U);
    uint32_t log_block_size = ext2_get32(superblock + 24U);
    uint32_t blocks_per_group = ext2_get32(superblock + 32U);
    uint32_t inodes_per_group = ext2_get32(superblock + 40U);
    uint32_t revision = ext2_get32(superblock + 76U);
    uint32_t incompat = ext2_get32(superblock + 96U);
    uint32_t ro_compat = ext2_get32(superblock + 100U);
    if (ext2_get16(superblock + 56U) != EXT2_SIGNATURE ||
        revision > 1U || log_block_size > 2U || blocks_count == 0U ||
        inodes_count < EXT2_ROOT_INODE || blocks_per_group == 0U ||
        inodes_per_group == 0U || first_data_block >= blocks_count ||
        (incompat & ~EXT2_INCOMPAT_FILETYPE) != 0U ||
        (ro_compat & ~EXT2_RO_COMPAT_SUPPORTED) != 0U) return -2;
    uint32_t block_size = 1024U << log_block_size;
    if (first_data_block != (block_size == 1024U ? 1U : 0U) ||
        blocks_per_group > block_size * 8U ||
        inodes_per_group > block_size * 8U) return -2;
    uint32_t inode_size = revision == 0U ? EXT2_INODE_CORE_SIZE
        : ext2_get16(superblock + 88U);
    uint32_t first_inode = revision == 0U ? 11U
        : ext2_get32(superblock + 84U);
    if (inode_size < EXT2_INODE_CORE_SIZE || inode_size > block_size ||
        (inode_size & (inode_size - 1U)) != 0U ||
        block_size % inode_size != 0U || first_inode < 11U ||
        first_inode > inodes_count) return -2;
    uint64_t required_sectors = (uint64_t)blocks_count *
        (block_size / X86OS_STORAGE_BLOCK_SIZE);
    if (required_sectors > volume->sectors) return -2;
    uint32_t data_blocks = blocks_count - first_data_block;
    uint32_t block_groups = ext2_div_ceil_u32(
        data_blocks, blocks_per_group);
    uint32_t inode_groups = ext2_div_ceil_u32(
        inodes_count, inodes_per_group);
    if (block_groups == 0U || block_groups != inode_groups) return -2;
    volume->blocks_count = blocks_count;
    volume->inodes_count = inodes_count;
    volume->first_data_block = first_data_block;
    volume->block_size = block_size;
    volume->sectors_per_block = block_size / X86OS_STORAGE_BLOCK_SIZE;
    volume->blocks_per_group = blocks_per_group;
    volume->inodes_per_group = inodes_per_group;
    volume->inode_size = inode_size;
    volume->first_inode = first_inode;
    volume->group_count = block_groups;
    volume->has_file_type = (incompat & EXT2_INCOMPAT_FILETYPE) != 0U;
    return 0;
}

static int ext2_group_descriptor(ext2_shadow_volume_t *volume,
                                 uint32_t group,
                                 uint8_t descriptor[EXT2_GROUP_DESCRIPTOR_SIZE]) {
    if (group >= volume->group_count ||
        group > UINT32_MAX / EXT2_GROUP_DESCRIPTOR_SIZE) return -5;
    uint32_t table_block = volume->block_size == 1024U ? 2U : 1U;
    uint32_t descriptor_offset = group * EXT2_GROUP_DESCRIPTOR_SIZE;
    uint32_t sector_index = table_block * volume->sectors_per_block +
        descriptor_offset / X86OS_STORAGE_BLOCK_SIZE;
    uint32_t offset = descriptor_offset % X86OS_STORAGE_BLOCK_SIZE;
    if (sector_index >= volume->sectors ||
        offset + EXT2_GROUP_DESCRIPTOR_SIZE > X86OS_STORAGE_BLOCK_SIZE)
        return -5;
    uint8_t sector[X86OS_STORAGE_BLOCK_SIZE];
    int status = ext2_read_sector(volume, (uint32_t)sector_index, sector);
    if (status != 0) return status;
    ext2_copy(descriptor, sector + offset, EXT2_GROUP_DESCRIPTOR_SIZE);
    uint32_t block_bitmap = ext2_get32(descriptor + 0U);
    uint32_t inode_bitmap = ext2_get32(descriptor + 4U);
    uint32_t inode_table = ext2_get32(descriptor + 8U);
    uint32_t inode_table_bytes = volume->inodes_per_group *
        volume->inode_size;
    uint32_t inode_table_blocks = ext2_div_ceil_u32(
        inode_table_bytes, volume->block_size);
    if (block_bitmap == 0U || inode_bitmap == 0U || inode_table == 0U ||
        block_bitmap < volume->first_data_block ||
        inode_bitmap < volume->first_data_block ||
        inode_table < volume->first_data_block ||
        block_bitmap >= volume->blocks_count ||
        inode_bitmap >= volume->blocks_count ||
        block_bitmap == inode_bitmap ||
        (uint64_t)inode_table + inode_table_blocks > volume->blocks_count ||
        (block_bitmap >= inode_table &&
         block_bitmap < inode_table + inode_table_blocks) ||
        (inode_bitmap >= inode_table &&
         inode_bitmap < inode_table + inode_table_blocks) ||
        ext2_get16(descriptor + 12U) > volume->blocks_per_group ||
        ext2_get16(descriptor + 14U) > volume->inodes_per_group)
        return -5;
    return 0;
}

static int ext2_read_inode(ext2_shadow_volume_t *volume, uint32_t inode_number,
                           ext2_shadow_inode_t *inode) {
    if (inode_number == 0U || inode_number > volume->inodes_count || inode == 0)
        return -5;
    uint32_t zero_based = inode_number - 1U;
    uint32_t group = zero_based / volume->inodes_per_group;
    uint32_t index = zero_based % volume->inodes_per_group;
    uint8_t descriptor[EXT2_GROUP_DESCRIPTOR_SIZE];
    int status = ext2_group_descriptor(volume, group, descriptor);
    if (status != 0) return status;
    uint32_t inode_table = ext2_get32(descriptor + 8U);
    uint32_t inode_offset = index * volume->inode_size;
    uint32_t sector_index = inode_table * volume->sectors_per_block +
        inode_offset / X86OS_STORAGE_BLOCK_SIZE;
    uint32_t offset = inode_offset % X86OS_STORAGE_BLOCK_SIZE;
    if (sector_index >= volume->sectors ||
        offset + sizeof(inode->bytes) > X86OS_STORAGE_BLOCK_SIZE) return -5;
    uint8_t sector[X86OS_STORAGE_BLOCK_SIZE];
    status = ext2_read_sector(volume, (uint32_t)sector_index, sector);
    if (status != 0) return status;
    ext2_copy(inode->bytes, sector + offset, sizeof(inode->bytes));
    uint16_t mode = ext2_get16(inode->bytes + 0U) & EXT2_S_IFMT;
    if ((mode != EXT2_S_IFREG && mode != EXT2_S_IFDIR &&
         mode != EXT2_S_IFLNK) ||
        ext2_get32(inode->bytes + 20U) != 0U ||
        ((mode == EXT2_S_IFREG || mode == EXT2_S_IFLNK) &&
         ext2_get32(inode->bytes + 108U) != 0U) ||
        (mode == EXT2_S_IFDIR &&
         (ext2_get32(inode->bytes + 32U) & EXT2_INDEX_FL) != 0U)) return -2;
    return 0;
}

static int ext2_data_block_valid(const ext2_shadow_volume_t *volume,
                                 uint32_t block) {
    /* Zero is the standard EXT2 sparse/unassigned block pointer even on
     * filesystems whose first_data_block field is zero. */
    return block != 0U && block >= volume->first_data_block &&
           block < volume->blocks_count;
}

static int ext2_inode_block(ext2_shadow_volume_t *volume,
                            const ext2_shadow_inode_t *inode,
                            uint32_t logical, uint32_t *block) {
    if (logical < EXT2_DIRECT_BLOCKS) {
        *block = ext2_get32(inode->bytes + 40U + logical * 4U);
    } else {
        uint32_t indirect_index = logical - EXT2_DIRECT_BLOCKS;
        uint32_t entries = volume->block_size / sizeof(uint32_t);
        uint32_t indirect = ext2_get32(
            inode->bytes + 40U + EXT2_SINGLE_INDIRECT_INDEX * 4U);
        if (indirect_index >= entries) return -110;
        if (!ext2_data_block_valid(volume, indirect)) return -5;
        uint32_t indirect_offset = indirect_index * sizeof(uint32_t);
        uint32_t sector_index = indirect * volume->sectors_per_block +
            indirect_offset / X86OS_STORAGE_BLOCK_SIZE;
        uint32_t offset = indirect_offset % X86OS_STORAGE_BLOCK_SIZE;
        uint8_t sector[X86OS_STORAGE_BLOCK_SIZE];
        int status = ext2_read_sector(volume, sector_index, sector);
        if (status != 0) return status;
        *block = ext2_get32(sector + offset);
    }
    return ext2_data_block_valid(volume, *block) ? 0 : -5;
}

static int ext2_name_equal(const uint8_t *candidate, uint32_t length,
                           const char *wanted) {
    uint32_t wanted_length = ext2_text_length(wanted, 256U);
    if (wanted_length != length) return 0;
    for (uint32_t index = 0U; index < length; ++index)
        if (candidate[index] != (uint8_t)wanted[index]) return 0;
    return 1;
}

static int ext2_find_entry(ext2_shadow_volume_t *volume,
                           const ext2_shadow_inode_t *directory,
                           const char *wanted, uint32_t *inode_number,
                           char visible[256]) {
    uint32_t size = ext2_get32(directory->bytes + 4U);
    uint32_t blocks = ext2_div_ceil_u32(size, volume->block_size);
    if (blocks == 0U || blocks > REIST_VFS_SHADOW_EXT2_MAX_DIRECTORY_BLOCKS)
        return -110;
    uint8_t data[REIST_VFS_SHADOW_EXT2_MAX_BLOCK_SIZE];
    for (uint32_t logical = 0U; logical < blocks; ++logical) {
        uint32_t block = 0U;
        int status = ext2_inode_block(volume, directory, logical, &block);
        if (status != 0) return status;
        status = ext2_read_block(volume, block, data);
        if (status != 0) return status;
        uint32_t remaining_size = size - logical * volume->block_size;
        uint32_t limit = remaining_size < volume->block_size
            ? remaining_size : volume->block_size;
        uint32_t offset = 0U;
        while (offset < limit) {
            if (limit - offset < 8U) return -5;
            uint32_t entry_inode = ext2_get32(data + offset);
            uint32_t record_length = ext2_get16(data + offset + 4U);
            uint32_t name_length = volume->has_file_type != 0U
                ? data[offset + 6U] : ext2_get16(data + offset + 6U);
            if (record_length < 8U || (record_length & 3U) != 0U ||
                record_length > limit - offset || name_length > 255U ||
                name_length > record_length - 8U ||
                (entry_inode != 0U && entry_inode > volume->inodes_count))
                return -5;
            if (entry_inode != 0U && ext2_name_equal(
                    data + offset + 8U, name_length, wanted)) {
                *inode_number = entry_inode;
                ext2_zero(visible, 256U);
                ext2_copy(visible, data + offset + 8U, name_length);
                return 0;
            }
            offset += record_length;
        }
    }
    return -2;
}

static int ext2_component_valid(const char *name, uint32_t length) {
    if (length == 0U || length > 255U ||
        (length == 1U && name[0] == '.') ||
        (length == 2U && name[0] == '.' && name[1] == '.')) return 0;
    for (uint32_t index = 0U; index < length; ++index) {
        uint8_t value = (uint8_t)name[index];
        if (value < 0x20U || value > 0x7EU || value == '/') return 0;
    }
    return 1;
}

static void ext2_inode_info(const ext2_shadow_inode_t *inode,
                            const char *name, x86os_file_info_t *info) {
    ext2_zero(info, sizeof(*info));
    uint32_t length = ext2_text_length(name, sizeof(info->name));
    if (length < sizeof(info->name)) ext2_copy(info->name, name, length + 1U);
    uint16_t mode = ext2_get16(inode->bytes + 0U) & EXT2_S_IFMT;
    info->type = mode == EXT2_S_IFDIR ? X86OS_DIRECTORY
        : mode == EXT2_S_IFLNK ? X86OS_SYMLINK : X86OS_FILE;
    info->size = ext2_get32(inode->bytes + 4U);
    info->access_time = ext2_get32(inode->bytes + 8U);
    info->create_time = ext2_get32(inode->bytes + 12U);
    info->modify_time = ext2_get32(inode->bytes + 16U);
}

static int ext2_read_symlink_inode(ext2_shadow_volume_t *volume,
        const ext2_shadow_inode_t *inode,
        char target[X86OS_VFS_SYMLINK_TARGET_CAPACITY],
        uint32_t *target_length) {
    if (volume == 0 || inode == 0 || target == 0 || target_length == 0 ||
        (ext2_get16(inode->bytes + 0U) & EXT2_S_IFMT) != EXT2_S_IFLNK)
        return -22;
    ext2_zero(target, X86OS_VFS_SYMLINK_TARGET_CAPACITY);
    *target_length = 0U;
    uint32_t size = ext2_get32(inode->bytes + 4U);
    if (size == 0U || size >= X86OS_VFS_SYMLINK_TARGET_CAPACITY) return -36;
    if (size <= EXT2_FAST_SYMLINK_CAPACITY &&
        ext2_get32(inode->bytes + 28U) == 0U) {
        ext2_copy(target, inode->bytes + 40U, size);
    } else {
        uint32_t completed = 0U;
        uint8_t block_data[REIST_VFS_SHADOW_EXT2_MAX_BLOCK_SIZE];
        while (completed < size) {
            uint32_t logical = completed / volume->block_size;
            uint32_t in_block = completed % volume->block_size;
            uint32_t block = 0U;
            int status = ext2_inode_block(volume, inode, logical, &block);
            if (status != 0) return status;
            status = ext2_read_block(volume, block, block_data);
            if (status != 0) return status;
            uint32_t amount = volume->block_size - in_block;
            if (amount > size - completed) amount = size - completed;
            ext2_copy(target + completed, block_data + in_block, amount);
            completed += amount;
        }
    }
    for (uint32_t index = 0U; index < size; ++index) {
        uint8_t value = (uint8_t)target[index];
        if (value < 0x20U || value > 0x7EU || value == '\0') {
            ext2_zero(target, X86OS_VFS_SYMLINK_TARGET_CAPACITY);
            return -5;
        }
    }
    target[size] = '\0';
    *target_length = size;
    return 0;
}

static int ext2_path_component_append(
        char output[X86OS_VFS_SHADOW_PATH_CAPACITY], uint32_t *length,
        const char *component, uint32_t component_length) {
    if (component_length == 1U && component[0U] == '.') return 0;
    if (component_length == 2U && component[0U] == '.' &&
        component[1U] == '.') {
        if (*length <= 1U) return -13;
        while (*length > 1U && output[*length - 1U] != '/') --*length;
        if (*length > 1U) --*length;
        output[*length] = '\0';
        return 0;
    }
    if (!ext2_component_valid(component, component_length)) return -22;
    if (*length > 1U) {
        if (*length + 1U >= X86OS_VFS_SHADOW_PATH_CAPACITY) return -36;
        output[(*length)++] = '/';
    }
    if (component_length >= X86OS_VFS_SHADOW_PATH_CAPACITY - *length)
        return -36;
    ext2_copy(output + *length, component, component_length);
    *length += component_length;
    output[*length] = '\0';
    return 0;
}

static int ext2_path_segment_append(
        char output[X86OS_VFS_SHADOW_PATH_CAPACITY], uint32_t *length,
        const char *segment, uint32_t segment_length) {
    uint32_t cursor = 0U;
    while (cursor < segment_length) {
        while (cursor < segment_length && segment[cursor] == '/') ++cursor;
        if (cursor >= segment_length) break;
        uint32_t start = cursor;
        while (cursor < segment_length && segment[cursor] != '/') ++cursor;
        int status = ext2_path_component_append(
            output, length, segment + start, cursor - start);
        if (status != 0) return status;
    }
    return 0;
}

static int ext2_follow_path(const char *current, uint32_t component_start,
        const char *target, uint32_t target_length,
        const char *remaining, uint32_t remaining_length,
        char output[X86OS_VFS_SHADOW_PATH_CAPACITY], uint32_t *output_length) {
    if (current == 0 || target == 0 || output == 0 || output_length == 0 ||
        target_length == 0U) return -22;
    ext2_zero(output, X86OS_VFS_SHADOW_PATH_CAPACITY);
    output[0U] = '/';
    output[1U] = '\0';
    *output_length = 1U;
    int status = 0;
    if (target[0U] != '/') {
        uint32_t parent_length = component_start;
        while (parent_length > 1U && current[parent_length - 1U] == '/')
            --parent_length;
        status = ext2_path_segment_append(
            output, output_length, current, parent_length);
    }
    if (status == 0)
        status = ext2_path_segment_append(
            output, output_length, target, target_length);
    if (status == 0 && remaining_length != 0U)
        status = ext2_path_segment_append(
            output, output_length, remaining, remaining_length);
    return status;
}

static int ext2_resolve(ext2_request_t *request,
                        const char *absolute_path, uint32_t path_length,
                        uint32_t resolve_flags,
                        ext2_shadow_volume_t *volume,
                        ext2_shadow_inode_t *inode, char visible[256],
                        uint32_t *inode_number_out) {
    if (request == 0 || request->drive_info == 0 ||
        request->read_sector == 0 || absolute_path == 0 || volume == 0 ||
        inode == 0 || visible == 0 || path_length == 0U ||
        path_length >= X86OS_VFS_SHADOW_PATH_CAPACITY ||
        (resolve_flags & ~REIST_VFS_SHADOW_EXT2_NOFOLLOW_FINAL) != 0U ||
        absolute_path[0U] != '/' || absolute_path[path_length] != '\0')
        return -22;
    for (uint32_t index = 0U; index < path_length; ++index)
        if (absolute_path[index] == '\0') return -22;
    char pending[X86OS_VFS_SHADOW_PATH_CAPACITY];
    ext2_zero(pending, sizeof(pending));
    ext2_copy(pending, absolute_path, path_length + 1U);
    uint32_t pending_length = path_length;
    uint32_t seen_resources[REIST_VFS_SHADOW_EXT2_MAX_LINK_DEPTH];
    uint32_t seen_inodes[REIST_VFS_SHADOW_EXT2_MAX_LINK_DEPTH];
    uint32_t link_depth = 0U;
    uint32_t walked = 0U;
    for (;;) {
        ext2_zero(visible, 256U);
        uint32_t cursor = 0U;
        int status = ext2_mount(
            volume, request, pending, pending_length, &cursor);
        if (status != 0) return status;
        status = ext2_parse_superblock(volume);
        if (status != 0) return status;
        status = ext2_read_inode(volume, EXT2_ROOT_INODE, inode);
        if (status != 0 ||
            (ext2_get16(inode->bytes + 0U) & EXT2_S_IFMT) != EXT2_S_IFDIR)
            return status != 0 ? status : -5;
        uint32_t inode_number = EXT2_ROOT_INODE;
        if (cursor >= pending_length) {
            visible[0U] = '/';
            if (inode_number_out != 0) *inode_number_out = inode_number;
            return 0;
        }
        uint8_t restart = 0U;
        uint32_t components = 0U;
        for (;;) {
            if (components >= REIST_VFS_SHADOW_EXT2_MAX_COMPONENTS)
                return -110;
            if (walked >= REIST_VFS_SHADOW_EXT2_MAX_WALK_COMPONENTS)
                return -110;
            ++components;
            ++walked;
            uint32_t start = cursor;
            while (cursor < pending_length && pending[cursor] != '/')
                ++cursor;
            uint32_t length = cursor - start;
            if (!ext2_component_valid(pending + start, length)) return -22;
            char wanted[256];
            ext2_zero(wanted, sizeof(wanted));
            ext2_copy(wanted, pending + start, length);
            uint32_t found_inode = 0U;
            status = ext2_find_entry(
                volume, inode, wanted, &found_inode, visible);
            if (status != 0) return status;
            inode_number = found_inode;
            status = ext2_read_inode(volume, found_inode, inode);
            if (status != 0) return status;
            while (cursor < pending_length && pending[cursor] == '/')
                ++cursor;
            uint8_t final = cursor >= pending_length;
            uint16_t mode = ext2_get16(inode->bytes + 0U) & EXT2_S_IFMT;
            if (mode == EXT2_S_IFLNK &&
                !(final != 0U &&
                  (resolve_flags & REIST_VFS_SHADOW_EXT2_NOFOLLOW_FINAL) !=
                    0U)) {
                if (link_depth >= REIST_VFS_SHADOW_EXT2_MAX_LINK_DEPTH)
                    return -40;
                for (uint32_t index = 0U; index < link_depth; ++index)
                    if (seen_resources[index] == volume->resource &&
                        seen_inodes[index] == inode_number) return -40;
                seen_resources[link_depth] = volume->resource;
                seen_inodes[link_depth] = inode_number;
                ++link_depth;
                char target[X86OS_VFS_SYMLINK_TARGET_CAPACITY];
                uint32_t target_length = 0U;
                status = ext2_read_symlink_inode(
                    volume, inode, target, &target_length);
                if (status != 0) return status;
                char followed[X86OS_VFS_SHADOW_PATH_CAPACITY];
                uint32_t followed_length = 0U;
                status = ext2_follow_path(
                    pending, start, target, target_length,
                    pending + cursor, pending_length - cursor,
                    followed, &followed_length);
                if (status != 0) return status;
                ext2_zero(pending, sizeof(pending));
                ext2_copy(pending, followed, followed_length + 1U);
                pending_length = followed_length;
                restart = 1U;
                break;
            }
            if (final != 0U) {
                if (inode_number_out != 0) *inode_number_out = inode_number;
                return 0;
            }
            if (mode != EXT2_S_IFDIR) return -20;
        }
        if (restart == 0U) return -5;
    }
}

int reist_vfs_shadow_ext2_stat(const reist_vfs_shadow_io_t *io,
                               const char *absolute_path,
                               uint32_t path_length,
                               x86os_file_info_t *info) {
    if (info == 0) return -22;
    ext2_zero(info, sizeof(*info));
    ext2_shadow_volume_t volume;
    ext2_shadow_inode_t inode;
    char visible[256];
    ext2_request_t request;
    int status = ext2_request_from_legacy(&request, io);
    if (status == 0)
        status = ext2_resolve(&request, absolute_path, path_length, 0U,
                              &volume, &inode, visible, 0);
    if (status == 0) ext2_inode_info(&inode, visible, info);
    return status;
}

static int ext2_read_file(ext2_shadow_volume_t *volume,
                          const ext2_shadow_inode_t *inode, uint32_t offset,
                          uint8_t *data, uint32_t capacity,
                          uint32_t *transferred) {
    if ((ext2_get16(inode->bytes + 0U) & EXT2_S_IFMT) != EXT2_S_IFREG)
        return -21;
    uint32_t size = ext2_get32(inode->bytes + 4U);
    if (offset >= size) return 0;
    uint32_t amount = size - offset < capacity ? size - offset : capacity;
    uint32_t completed = 0U;
    uint8_t block_data[REIST_VFS_SHADOW_EXT2_MAX_BLOCK_SIZE];
    while (completed < amount) {
        uint32_t position = offset + completed;
        uint32_t logical = position / volume->block_size;
        uint32_t in_block = position % volume->block_size;
        uint32_t block = 0U;
        int status = ext2_inode_block(volume, inode, logical, &block);
        if (status != 0) return status;
        status = ext2_read_block(volume, block, block_data);
        if (status != 0) return status;
        uint32_t chunk = volume->block_size - in_block;
        if (chunk > amount - completed) chunk = amount - completed;
        ext2_copy(data + completed, block_data + in_block, chunk);
        completed += chunk;
    }
    *transferred = completed;
    return 0;
}

int reist_vfs_shadow_ext2_read(const reist_vfs_shadow_io_t *io,
                               const char *absolute_path,
                               uint32_t path_length, uint32_t offset,
                               uint8_t *data, uint32_t capacity,
                               uint32_t *transferred) {
    if (data == 0 || transferred == 0 || capacity == 0U ||
        capacity > X86OS_VFS_SHADOW_READ_CAPACITY) return -22;
    ext2_zero(data, capacity);
    *transferred = 0U;
    ext2_shadow_volume_t volume;
    ext2_shadow_inode_t inode;
    char visible[256];
    ext2_request_t request;
    int status = ext2_request_from_legacy(&request, io);
    if (status == 0)
        status = ext2_resolve(&request, absolute_path, path_length, 0U,
                              &volume, &inode, visible, 0);
    if (status != 0) return status;
    status = ext2_read_file(&volume, &inode, offset, data, capacity,
                            transferred);
    if (status != 0) {
        ext2_zero(data, capacity);
        *transferred = 0U;
    }
    return status;
}

static void ext2_readdir_cursor_reset(
        reist_vfs_shadow_ext2_readdir_cursor_t *cursor) {
    if (cursor != 0) ext2_zero(cursor, sizeof(*cursor));
}

static int ext2_readdir_resume_valid(
        const ext2_shadow_volume_t *volume,
        const ext2_shadow_inode_t *directory, uint32_t directory_inode,
        uint32_t blocks, uint32_t index,
        const reist_vfs_shadow_ext2_readdir_cursor_t *cursor) {
    if (volume == 0 || directory == 0 || cursor == 0 ||
        cursor->version != REIST_VFS_SHADOW_READDIR_CURSOR_VERSION ||
        cursor->struct_size != sizeof(*cursor) || cursor->active != 1U ||
        cursor->next_index != index || cursor->resource != volume->resource ||
        cursor->volume_signature != volume->signature ||
        cursor->directory_inode != directory_inode ||
        cursor->directory_generation != ext2_get32(directory->bytes + 100U) ||
        cursor->directory_signature !=
            ext2_signature(directory->bytes, sizeof(directory->bytes)) ||
        cursor->directory_size != ext2_get32(directory->bytes + 4U) ||
        cursor->logical_block > blocks ||
        (cursor->logical_block == blocks && cursor->entry_offset != 0U) ||
        cursor->entry_offset >= volume->block_size ||
        (cursor->entry_offset & 3U) != 0U) return 0;
    return 1;
}

static void ext2_readdir_cursor_publish(
        reist_vfs_shadow_ext2_readdir_cursor_t *cursor,
        const ext2_shadow_volume_t *volume,
        const ext2_shadow_inode_t *directory, uint32_t directory_inode,
        uint32_t index, uint32_t logical_block, uint32_t entry_offset) {
    ext2_readdir_cursor_reset(cursor);
    if (index == UINT32_MAX) return;
    cursor->version = REIST_VFS_SHADOW_READDIR_CURSOR_VERSION;
    cursor->struct_size = sizeof(*cursor);
    cursor->next_index = index + 1U;
    cursor->resource = volume->resource;
    cursor->volume_signature = volume->signature;
    cursor->directory_inode = directory_inode;
    cursor->directory_generation = ext2_get32(directory->bytes + 100U);
    cursor->directory_signature =
        ext2_signature(directory->bytes, sizeof(directory->bytes));
    cursor->directory_size = ext2_get32(directory->bytes + 4U);
    cursor->logical_block = logical_block;
    cursor->entry_offset = entry_offset;
    cursor->active = 1U;
}

int reist_vfs_shadow_ext2_readdir_continue(
        const reist_vfs_shadow_io_t *io, const char *absolute_path,
        uint32_t path_length, uint32_t index,
        reist_vfs_shadow_ext2_readdir_cursor_t *cursor,
        x86os_file_info_t *info) {
    if (cursor == 0 || info == 0) return -22;
    ext2_zero(info, sizeof(*info));
    ext2_shadow_volume_t volume;
    ext2_shadow_inode_t directory;
    char visible[256];
    uint32_t directory_inode = 0U;
    ext2_request_t request;
    int status = ext2_request_from_legacy(&request, io);
    if (status == 0)
        status = ext2_resolve(&request, absolute_path, path_length, 0U,
                              &volume, &directory, visible,
                              &directory_inode);
    if (status != 0) {
        ext2_readdir_cursor_reset(cursor);
        return status;
    }
    if ((ext2_get16(directory.bytes + 0U) & EXT2_S_IFMT) != EXT2_S_IFDIR) {
        ext2_readdir_cursor_reset(cursor);
        return -20;
    }
    uint32_t size = ext2_get32(directory.bytes + 4U);
    uint32_t blocks = ext2_div_ceil_u32(size, volume.block_size);
    if (blocks == 0U || blocks > REIST_VFS_SHADOW_EXT2_MAX_DIRECTORY_BLOCKS) {
        ext2_readdir_cursor_reset(cursor);
        return -110;
    }
    uint32_t current = 0U;
    uint32_t first_logical = 0U;
    uint32_t first_offset = 0U;
    if (ext2_readdir_resume_valid(
            &volume, &directory, directory_inode, blocks, index, cursor)) {
        current = index;
        first_logical = cursor->logical_block;
        first_offset = cursor->entry_offset;
    } else {
        ext2_readdir_cursor_reset(cursor);
    }
    uint8_t block_data[REIST_VFS_SHADOW_EXT2_MAX_BLOCK_SIZE];
    for (uint32_t logical = first_logical; logical < blocks; ++logical) {
        uint32_t block = 0U;
        status = ext2_inode_block(&volume, &directory, logical, &block);
        if (status != 0) {
            ext2_readdir_cursor_reset(cursor);
            return status;
        }
        status = ext2_read_block(&volume, block, block_data);
        if (status != 0) {
            ext2_readdir_cursor_reset(cursor);
            return status;
        }
        uint32_t remaining = size - logical * volume.block_size;
        uint32_t limit = remaining < volume.block_size
            ? remaining : volume.block_size;
        uint32_t block_cursor = logical == first_logical ? first_offset : 0U;
        if (block_cursor >= limit || (block_cursor & 3U) != 0U) {
            ext2_readdir_cursor_reset(cursor);
            return -5;
        }
        for (; block_cursor < limit;) {
            if (limit - block_cursor < 8U) {
                ext2_readdir_cursor_reset(cursor);
                return -5;
            }
            uint32_t child_number = ext2_get32(block_data + block_cursor);
            uint32_t record_length =
                ext2_get16(block_data + block_cursor + 4U);
            uint32_t name_length = volume.has_file_type != 0U
                ? block_data[block_cursor + 6U]
                : ext2_get16(block_data + block_cursor + 6U);
            if (record_length < 8U || (record_length & 3U) != 0U ||
                record_length > limit - block_cursor || name_length > 255U ||
                name_length > record_length - 8U ||
                (child_number != 0U && child_number > volume.inodes_count)) {
                ext2_readdir_cursor_reset(cursor);
                return -5;
            }
            const char *name =
                (const char *)(block_data + block_cursor + 8U);
            int dot = name_length == 1U && name[0] == '.';
            int dotdot = name_length == 2U && name[0] == '.' && name[1] == '.';
            if (child_number != 0U && !dot && !dotdot) {
                if (!ext2_component_valid(name, name_length)) {
                    ext2_readdir_cursor_reset(cursor);
                    return -5;
                }
                if (current++ == index) {
                    ext2_shadow_inode_t child;
                    status = ext2_read_inode(&volume, child_number, &child);
                    if (status != 0) {
                        ext2_readdir_cursor_reset(cursor);
                        return status;
                    }
                    char child_name[256];
                    ext2_zero(child_name, sizeof(child_name));
                    ext2_copy(child_name, name, name_length);
                    ext2_inode_info(&child, child_name, info);
                    uint32_t next_logical = logical;
                    uint32_t next_offset = block_cursor + record_length;
                    if (next_offset == limit) {
                        ++next_logical;
                        next_offset = 0U;
                    }
                    ext2_readdir_cursor_publish(
                        cursor, &volume, &directory, directory_inode, index,
                        next_logical, next_offset);
                    return 0;
                }
            }
            block_cursor += record_length;
        }
    }
    ext2_readdir_cursor_reset(cursor);
    return 1;
}

int reist_vfs_shadow_ext2_readdir(const reist_vfs_shadow_io_t *io,
                                  const char *absolute_path,
                                  uint32_t path_length, uint32_t index,
                                  x86os_file_info_t *info) {
    reist_vfs_shadow_ext2_readdir_cursor_t cursor;
    ext2_zero(&cursor, sizeof(cursor));
    return reist_vfs_shadow_ext2_readdir_continue(
        io, absolute_path, path_length, index, &cursor, info);
}

static int ext2_object_inode(ext2_request_t *request,
        const reist_vfs_shadow_object_t *object,
        ext2_shadow_volume_t *volume, ext2_shadow_inode_t *inode) {
    if (request == 0 || request->drive_info == 0 ||
        request->read_sector == 0 ||
        object == 0 || volume == 0 || inode == 0 ||
        object->version != REIST_VFS_SHADOW_OBJECT_VERSION ||
        object->struct_size != sizeof(*object) ||
        object->filesystem != REIST_VFS_SHADOW_OBJECT_EXT2 ||
        object->resource >= REIST_VFS_SHADOW_MAX_RESOURCES ||
        object->locator_b != 0U || object->locator_c != 0U) return -22;
    x86os_drive_info_t drive;
    ext2_zero(&drive, sizeof(drive));
    int status = ext2_request_deadline(request);
    if (status != 0) return status;
    int available = request->drive_info(
        request->context, object->resource, &drive);
    if (available <= 0 || drive.sectors == 0U) return -116;
    ext2_zero(volume, sizeof(*volume));
    volume->resource = object->resource;
    volume->sectors = drive.sectors;
    volume->reads = request->reads;
    volume->request = request;
    status = ext2_parse_superblock(volume);
    if (status != 0) return status;
    if (volume->signature != object->volume_signature) return -116;
    status = ext2_read_inode(volume, object->locator_a, inode);
    if (status != 0) return status;
    if (ext2_get32(inode->bytes + 100U) != object->object_generation)
        return -116;
    return 0;
}

int reist_vfs_shadow_ext2_object_open(
        const reist_vfs_shadow_io_t *io, const char *absolute_path,
        uint32_t path_length, reist_vfs_shadow_object_t *object,
        x86os_file_info_t *info) {
    if (object == 0 || info == 0) return -22;
    ext2_zero(object, sizeof(*object));
    ext2_zero(info, sizeof(*info));
    ext2_shadow_volume_t volume;
    ext2_shadow_inode_t inode;
    char visible[256];
    uint32_t inode_number = 0U;
    ext2_request_t request;
    int status = ext2_request_from_legacy(&request, io);
    if (status == 0)
        status = ext2_resolve(&request, absolute_path, path_length, 0U,
                              &volume, &inode, visible, &inode_number);
    if (status != 0) return status;
    if ((ext2_get16(inode.bytes + 0U) & EXT2_S_IFMT) != EXT2_S_IFREG)
        return -21;
    *object = (reist_vfs_shadow_object_t){
        .version = REIST_VFS_SHADOW_OBJECT_VERSION,
        .struct_size = sizeof(*object),
        .filesystem = REIST_VFS_SHADOW_OBJECT_EXT2,
        .resource = volume.resource,
        .volume_signature = volume.signature,
        .locator_a = inode_number,
        .locator_b = 0U,
        .locator_c = 0U,
        .object_generation = ext2_get32(inode.bytes + 100U),
    };
    ext2_inode_info(&inode, visible, info);
    return 0;
}

int reist_vfs_shadow_ext2_object_stat(
        const reist_vfs_shadow_io_t *io,
        const reist_vfs_shadow_object_t *object, x86os_file_info_t *info) {
    if (info == 0) return -22;
    ext2_zero(info, sizeof(*info));
    ext2_shadow_volume_t volume;
    ext2_shadow_inode_t inode;
    ext2_request_t request;
    int status = ext2_request_from_legacy(&request, io);
    if (status == 0)
        status = ext2_object_inode(&request, object, &volume, &inode);
    if (status == 0) ext2_inode_info(&inode, "", info);
    return status;
}

int reist_vfs_shadow_ext2_object_read(
        const reist_vfs_shadow_io_t *io,
        const reist_vfs_shadow_object_t *object, uint32_t offset,
        uint8_t *data, uint32_t capacity, uint32_t *transferred) {
    if (data == 0 || transferred == 0 || capacity == 0U ||
        capacity > X86OS_STORAGE_BULK_MAX_BYTES) return -22;
    ext2_zero(data, capacity);
    *transferred = 0U;
    ext2_shadow_volume_t volume;
    ext2_shadow_inode_t inode;
    ext2_request_t request;
    int status = ext2_request_from_legacy(&request, io);
    if (status == 0)
        status = ext2_object_inode(&request, object, &volume, &inode);
    if (status != 0) return status;
    status = ext2_read_file(&volume, &inode, offset, data, capacity,
                            transferred);
    if (status != 0) {
        ext2_zero(data, capacity);
        *transferred = 0U;
    }
    return status;
}

typedef struct {
    uint32_t state;
    uint32_t sequence;
    uint32_t count;
    uint32_t sectors[REIST_VFS_SHADOW_EXT2_MAX_JOURNAL_ENTRIES];
    uint32_t old_crc[REIST_VFS_SHADOW_EXT2_MAX_JOURNAL_ENTRIES];
    uint32_t final_crc[REIST_VFS_SHADOW_EXT2_MAX_JOURNAL_ENTRIES];
    uint8_t publish[REIST_VFS_SHADOW_EXT2_MAX_JOURNAL_ENTRIES];
} ext2_journal_header_t;

static int ext2_volume_for_resource(ext2_request_t *request,
        uint32_t resource, ext2_shadow_volume_t *volume) {
    if (request == 0 || volume == 0 ||
        resource >= REIST_VFS_SHADOW_MAX_RESOURCES) return -22;
    int status = ext2_request_deadline(request);
    if (status != 0) return status;
    x86os_drive_info_t drive;
    ext2_zero(&drive, sizeof(drive));
    int available = request->drive_info(request->context, resource, &drive);
    if (available <= 0 || drive.sectors == 0U) return -116;
    ext2_zero(volume, sizeof(*volume));
    volume->resource = resource;
    volume->sectors = drive.sectors;
    volume->reads = request->reads;
    volume->request = request;
    return ext2_parse_superblock(volume);
}

static int ext2_journal_locate(ext2_shadow_volume_t *volume,
                               ext2_journal_t *journal) {
    if (volume == 0 || journal == 0) return -22;
    ext2_zero(journal, sizeof(*journal));
    ext2_shadow_inode_t root;
    int status = ext2_read_inode(volume, EXT2_ROOT_INODE, &root);
    if (status != 0) return status;
    if ((ext2_get16(root.bytes + 0U) & EXT2_S_IFMT) != EXT2_S_IFDIR)
        return -5;
    uint32_t journal_inode = 0U;
    char visible[256];
    status = ext2_find_entry(volume, &root, EXT2_JOURNAL_NAME,
                             &journal_inode, visible);
    if (status == -2) return 0;
    if (status != 0) return status;
    ext2_shadow_inode_t inode;
    status = ext2_read_inode(volume, journal_inode, &inode);
    if (status != 0) return status;
    if ((ext2_get16(inode.bytes + 0U) & EXT2_S_IFMT) != EXT2_S_IFREG ||
        ext2_get32(inode.bytes + 4U) <
            REIST_VFS_SHADOW_EXT2_JOURNAL_SECTORS *
                X86OS_STORAGE_BLOCK_SIZE) return -5;
    journal->inode_number = journal_inode;
    for (uint32_t index = 0U;
         index < REIST_VFS_SHADOW_EXT2_JOURNAL_SECTORS; ++index) {
        uint32_t byte_offset = index * X86OS_STORAGE_BLOCK_SIZE;
        uint32_t logical = byte_offset / volume->block_size;
        uint32_t in_block = byte_offset % volume->block_size;
        uint32_t block = 0U;
        status = ext2_inode_block(volume, &inode, logical, &block);
        if (status != 0) return status;
        uint64_t sector = (uint64_t)block * volume->sectors_per_block +
            in_block / X86OS_STORAGE_BLOCK_SIZE;
        if (sector >= volume->sectors) return -5;
        journal->sectors[index] = (uint32_t)sector;
        for (uint32_t prior = 0U; prior < index; ++prior)
            if (journal->sectors[prior] == journal->sectors[index]) return -5;
    }
    uint32_t indirect = ext2_get32(
        inode.bytes + 40U + EXT2_SINGLE_INDIRECT_INDEX * 4U);
    if (indirect != 0U && !ext2_data_block_valid(volume, indirect))
        return -5;
    journal->indirect_block = indirect;
    journal->present = 1U;
    return 0;
}

static int ext2_bytes_zero(const uint8_t *data, uint32_t length) {
    for (uint32_t index = 0U; index < length; ++index)
        if (data[index] != 0U) return 0;
    return 1;
}

static void ext2_journal_publish(ext2_journal_t *destination,
                                 const ext2_journal_t *source) {
    volatile uint32_t *sectors = destination->sectors;
    for (uint32_t index = 0U;
         index < REIST_VFS_SHADOW_EXT2_JOURNAL_SECTORS; ++index)
        sectors[index] = source->sectors[index];
    destination->indirect_block = source->indirect_block;
    destination->inode_number = source->inode_number;
    destination->sequence = source->sequence;
    destination->present = source->present;
}

static int ext2_directory_allocation_blocks(
        ext2_shadow_volume_t *volume, const ext2_shadow_inode_t *directory,
        uint32_t *blocks,
        uint32_t *count) {
    if (volume == 0 || directory == 0 || blocks == 0 || count == 0)
        return -22;
    uint32_t logical_count = ext2_div_ceil_u32(
        ext2_get32(directory->bytes + 4U), volume->block_size);
    if (logical_count == 0U ||
        logical_count > REIST_VFS_SHADOW_EXT2_MAX_DIRECTORY_BLOCKS)
        return -110;
    *count = logical_count;
    for (uint32_t index = 0U; index < logical_count; ++index) {
        int status = ext2_inode_block(
            volume, directory, index, &blocks[index]);
        if (status != 0) return status;
    }
    if (logical_count > EXT2_DIRECT_BLOCKS) {
        uint32_t indirect = ext2_get32(
            directory->bytes + 40U + EXT2_SINGLE_INDIRECT_INDEX * 4U);
        if (!ext2_data_block_valid(volume, indirect)) return -5;
        blocks[(*count)++] = indirect;
    }
    return 0;
}

static int ext2_regular_block_safe(
        ext2_shadow_volume_t *volume, const ext2_journal_t *journal,
        const uint32_t *directory_blocks, uint32_t directory_count,
        uint32_t block) {
    if (!ext2_data_block_valid(volume, block)) return -5;
    for (uint32_t index = 0U;
         index < REIST_VFS_SHADOW_EXT2_JOURNAL_SECTORS; ++index)
        if (journal->sectors[index] / volume->sectors_per_block == block)
            return -5;
    if (journal->indirect_block == block) return -5;
    for (uint32_t index = 0U; index < directory_count; ++index)
        if (directory_blocks[index] == block) return -5;
    uint32_t zero_based = block - volume->first_data_block;
    uint32_t group = zero_based / volume->blocks_per_group;
    uint8_t descriptor[EXT2_GROUP_DESCRIPTOR_SIZE];
    int status = ext2_group_descriptor(volume, group, descriptor);
    if (status != 0) return status;
    uint32_t block_bitmap = ext2_get32(descriptor + 0U);
    uint32_t inode_bitmap = ext2_get32(descriptor + 4U);
    uint32_t inode_table = ext2_get32(descriptor + 8U);
    uint32_t inode_table_blocks = ext2_div_ceil_u32(
        volume->inodes_per_group * volume->inode_size,
        volume->block_size);
    if (block == block_bitmap || block == inode_bitmap ||
        (block >= inode_table && block < inode_table + inode_table_blocks))
        return -5;
    if (volume->group_count >
        UINT32_MAX / EXT2_GROUP_DESCRIPTOR_SIZE) return -5;
    uint32_t descriptor_blocks = ext2_div_ceil_u32(
        volume->group_count * EXT2_GROUP_DESCRIPTOR_SIZE,
        volume->block_size);
    if (group >
        (UINT32_MAX - volume->first_data_block) /
            volume->blocks_per_group) return -5;
    uint32_t group_first = volume->first_data_block +
        group * volume->blocks_per_group;
    if (descriptor_blocks > UINT32_MAX - group_first) return -5;
    if (block >= group_first &&
        block <= group_first + descriptor_blocks) return -5;
    return 0;
}

static int ext2_regular_allocation_append(
        ext2_shadow_volume_t *volume, const ext2_journal_t *journal,
        const uint32_t *directory_blocks, uint32_t directory_count,
        ext2_regular_allocations_t *allocations, uint32_t block) {
    if (allocations->count >= REIST_VFS_SHADOW_EXT2_MAX_UNLINK_BLOCKS)
        return -27;
    int status = ext2_regular_block_safe(
        volume, journal, directory_blocks, directory_count, block);
    if (status != 0) return status;
    for (uint32_t index = 0U; index < allocations->count; ++index)
        if (allocations->blocks[index] == block) return -5;
    allocations->blocks[allocations->count++] = block;
    return 0;
}

static int ext2_regular_allocations(
        ext2_shadow_volume_t *volume, const ext2_shadow_inode_t *inode,
        const ext2_shadow_inode_t *directory, const ext2_journal_t *journal,
        ext2_regular_allocations_t *allocations) {
    if (volume == 0 || inode == 0 || directory == 0 || journal == 0 ||
        allocations == 0) return -22;
    ext2_zero(allocations, sizeof(*allocations));
    if (ext2_get32(inode->bytes + 32U) != 0U ||
        ext2_get32(inode->bytes + 104U) != 0U ||
        ext2_get32(inode->bytes + 112U) != 0U ||
        !ext2_bytes_zero(inode->bytes + 116U, 12U)) return -95;
    uint32_t data_blocks = ext2_div_ceil_u32(
        ext2_get32(inode->bytes + 4U), volume->block_size);
    uint32_t has_indirect = data_blocks > EXT2_DIRECT_BLOCKS ? 1U : 0U;
    if (data_blocks + has_indirect >
        REIST_VFS_SHADOW_EXT2_MAX_UNLINK_BLOCKS) return -27;
    if (ext2_get32(inode->bytes + 40U + 13U * 4U) != 0U ||
        ext2_get32(inode->bytes + 40U + 14U * 4U) != 0U) return -95;
    uint32_t directory_blocks[
        REIST_VFS_SHADOW_EXT2_MAX_DIRECTORY_BLOCKS + 1U];
    uint32_t directory_count = 0U;
    int status = ext2_directory_allocation_blocks(
        volume, directory, directory_blocks, &directory_count);
    if (status != 0) return status;
    for (uint32_t index = 0U; index < EXT2_DIRECT_BLOCKS; ++index) {
        uint32_t block = ext2_get32(inode->bytes + 40U + index * 4U);
        if (index >= data_blocks) {
            if (block != 0U) return -5;
            continue;
        }
        status = ext2_regular_allocation_append(
            volume, journal, directory_blocks, directory_count,
            allocations, block);
        if (status != 0) return status;
    }
    uint32_t indirect = ext2_get32(
        inode->bytes + 40U + EXT2_SINGLE_INDIRECT_INDEX * 4U);
    if (has_indirect == 0U) {
        if (indirect != 0U) return -5;
    } else {
        status = ext2_regular_allocation_append(
            volume, journal, directory_blocks, directory_count,
            allocations, indirect);
        if (status != 0) return status;
        uint8_t indirect_data[REIST_VFS_SHADOW_EXT2_MAX_BLOCK_SIZE];
        status = ext2_read_block(volume, indirect, indirect_data);
        if (status != 0) return status;
        uint32_t required = data_blocks - EXT2_DIRECT_BLOCKS;
        uint32_t entries = volume->block_size / sizeof(uint32_t);
        if (required > entries) return -27;
        for (uint32_t index = 0U; index < entries; ++index) {
            uint32_t block = ext2_get32(indirect_data + index * 4U);
            if (index >= required) {
                if (block != 0U) return -5;
                continue;
            }
            status = ext2_regular_allocation_append(
                volume, journal, directory_blocks, directory_count,
                allocations, block);
            if (status != 0) return status;
        }
    }
    uint32_t expected_sectors = allocations->count *
        volume->sectors_per_block;
    return ext2_get32(inode->bytes + 28U) == expected_sectors ? 0 : -5;
}

static int ext2_journal_header_decode(
        const uint8_t data[X86OS_STORAGE_BLOCK_SIZE],
        uint32_t volume_signature, ext2_journal_header_t *header) {
    if (data == 0 || header == 0) return 0;
    ext2_zero(header, sizeof(*header));
    if (ext2_bytes_zero(data, X86OS_STORAGE_BLOCK_SIZE)) return 1;
    uint32_t state = ext2_get32(data + 8U);
    uint32_t count = ext2_get32(data + 16U);
    if (ext2_get32(data + 0U) != EXT2_JOURNAL_MAGIC ||
        ext2_get32(data + 4U) != EXT2_JOURNAL_VERSION ||
        state > EXT2_JOURNAL_STATE_COMMITTED ||
        count > REIST_VFS_SHADOW_EXT2_MAX_JOURNAL_ENTRIES ||
        ext2_get32(data + 20U) != volume_signature ||
        ext2_get32(data + 28U) != 0U ||
        (state == EXT2_JOURNAL_STATE_CLEAN && count != 0U) ||
        (state != EXT2_JOURNAL_STATE_CLEAN && count == 0U)) return 0;
    uint8_t checked[X86OS_STORAGE_BLOCK_SIZE];
    ext2_copy(checked, data, sizeof(checked));
    uint32_t recorded_crc = ext2_get32(checked + 24U);
    ext2_put32(checked + 24U, 0U);
    if (ext2_crc32(checked, sizeof(checked)) != recorded_crc) return 0;
    for (uint32_t index = 0U; index < count; ++index) {
        uint32_t offset = EXT2_JOURNAL_HEADER_FIXED +
            index * EXT2_JOURNAL_ENTRY_SIZE;
        uint32_t publish = ext2_get32(data + offset + 12U);
        if (publish > 1U) return 0;
        header->sectors[index] = ext2_get32(data + offset + 0U);
        header->old_crc[index] = ext2_get32(data + offset + 4U);
        header->final_crc[index] = ext2_get32(data + offset + 8U);
        header->publish[index] = (uint8_t)publish;
    }
    uint32_t used = EXT2_JOURNAL_HEADER_FIXED +
        count * EXT2_JOURNAL_ENTRY_SIZE;
    if (!ext2_bytes_zero(data + used, X86OS_STORAGE_BLOCK_SIZE - used))
        return 0;
    header->state = state;
    header->sequence = ext2_get32(data + 12U);
    header->count = count;
    return 1;
}

static void ext2_journal_header_encode(
        uint8_t data[X86OS_STORAGE_BLOCK_SIZE], uint32_t state,
        uint32_t sequence, uint32_t volume_signature,
        const ext2_transaction_t *transaction) {
    ext2_zero(data, X86OS_STORAGE_BLOCK_SIZE);
    ext2_put32(data + 0U, EXT2_JOURNAL_MAGIC);
    ext2_put32(data + 4U, EXT2_JOURNAL_VERSION);
    ext2_put32(data + 8U, state);
    ext2_put32(data + 12U, sequence);
    uint32_t count = state == EXT2_JOURNAL_STATE_CLEAN || transaction == 0
        ? 0U : transaction->count;
    ext2_put32(data + 16U, count);
    ext2_put32(data + 20U, volume_signature);
    for (uint32_t index = 0U; index < count; ++index) {
        const ext2_transaction_entry_t *entry = &transaction->entries[index];
        uint32_t offset = EXT2_JOURNAL_HEADER_FIXED +
            index * EXT2_JOURNAL_ENTRY_SIZE;
        ext2_put32(data + offset + 0U, entry->sector);
        ext2_put32(data + offset + 4U, entry->old_crc);
        ext2_put32(data + offset + 8U, entry->final_crc);
        ext2_put32(data + offset + 12U, entry->publish != 0U ? 1U : 0U);
    }
    ext2_put32(data + 24U, 0U);
    ext2_put32(data + 24U, ext2_crc32(data, X86OS_STORAGE_BLOCK_SIZE));
}

static int ext2_journal_write_headers(ext2_request_t *request,
        const ext2_shadow_volume_t *volume, const ext2_journal_t *journal,
        uint32_t state, uint32_t sequence,
        const ext2_transaction_t *transaction) {
    uint8_t header[X86OS_STORAGE_BLOCK_SIZE];
    ext2_journal_header_encode(
        header, state, sequence, volume->signature, transaction);
    int status = ext2_request_write(
        request, volume->resource, journal->sectors[0U], header);
    if (status == 0)
        status = ext2_request_write(
            request, volume->resource, journal->sectors[1U], header);
    return status;
}

static int ext2_journal_clean(ext2_request_t *request,
        const ext2_shadow_volume_t *volume, const ext2_journal_t *journal,
        uint32_t sequence) {
    int status = ext2_journal_write_headers(
        request, volume, journal, EXT2_JOURNAL_STATE_CLEAN, sequence, 0);
    if (status == 0) status = ext2_request_flush(request, volume->resource);
    return status;
}

static int ext2_journal_restore(ext2_request_t *request,
        const ext2_shadow_volume_t *volume, const ext2_journal_t *journal,
        const ext2_journal_header_t *header,
        uint8_t before[REIST_VFS_SHADOW_EXT2_MAX_JOURNAL_ENTRIES]
                      [X86OS_STORAGE_BLOCK_SIZE]) {
    int status = 0;
    for (uint32_t pass = 0U; pass < 2U && status == 0; ++pass) {
        uint32_t writes = 0U;
        for (uint32_t index = 0U; index < header->count; ++index) {
            uint8_t publish = header->publish[index];
            if ((pass == 0U && publish == 0U) ||
                (pass == 1U && publish != 0U)) continue;
            status = ext2_request_write(
                request, volume->resource, header->sectors[index],
                before[index]);
            if (status != 0) break;
            ++writes;
        }
        if (status == 0 && writes != 0U)
            status = ext2_request_flush(request, volume->resource);
    }
    if (status == 0)
        status = ext2_journal_clean(
            request, volume, journal, header->sequence);
    return status;
}

static int ext2_journal_recover(ext2_request_t *request,
        ext2_shadow_volume_t *volume, uint8_t require_journal,
        ext2_journal_t *journal_out) {
    ext2_journal_t journal;
    int status = ext2_journal_locate(volume, &journal);
    if (status != 0) return status;
    if (journal.present == 0U) return require_journal != 0U ? -30 : 0;
    uint8_t raw[2U][X86OS_STORAGE_BLOCK_SIZE];
    ext2_journal_header_t decoded[2U];
    uint8_t valid[2U];
    for (uint32_t copy = 0U; copy < 2U; ++copy) {
        status = ext2_read_sector(volume, journal.sectors[copy], raw[copy]);
        if (status != 0) return status;
        valid[copy] = (uint8_t)ext2_journal_header_decode(
            raw[copy], volume->signature, &decoded[copy]);
    }
    if (valid[0U] == 0U && valid[1U] == 0U) return -5;
    if (valid[0U] != 0U && valid[1U] != 0U &&
        decoded[0U].sequence == decoded[1U].sequence &&
        decoded[0U].state == decoded[1U].state) {
        for (uint32_t index = 0U; index < X86OS_STORAGE_BLOCK_SIZE; ++index)
            if (raw[0U][index] != raw[1U][index]) return -5;
    }
    uint32_t selected = valid[0U] != 0U ? 0U : 1U;
    if (valid[0U] != 0U && valid[1U] != 0U &&
        (decoded[1U].sequence > decoded[0U].sequence ||
         (decoded[1U].sequence == decoded[0U].sequence &&
          decoded[1U].state > decoded[0U].state))) selected = 1U;
    ext2_journal_header_t *header = &decoded[selected];
    uint32_t other = selected ^ 1U;
    if (header->state == EXT2_JOURNAL_STATE_CLEAN && valid[other] == 0U &&
        !ext2_bytes_zero(raw[other], X86OS_STORAGE_BLOCK_SIZE)) return -5;
    journal.sequence = header->sequence;
    if (header->state == EXT2_JOURNAL_STATE_CLEAN) {
        if (journal_out != 0) ext2_journal_publish(journal_out, &journal);
        return 0;
    }
    if (request->write_sector == 0 || request->flush == 0) return -30;
    uint32_t publications = 0U;
    for (uint32_t index = 0U; index < header->count; ++index) {
        if (header->sectors[index] == 0U ||
            header->sectors[index] >= volume->sectors) return -5;
        if (header->publish[index] != 0U) ++publications;
        for (uint32_t prior = 0U; prior < index; ++prior)
            if (header->sectors[prior] == header->sectors[index]) return -5;
        for (uint32_t reserved = 0U;
             reserved < REIST_VFS_SHADOW_EXT2_JOURNAL_SECTORS; ++reserved)
            if (header->sectors[index] == journal.sectors[reserved]) return -5;
    }
    if (publications != 1U) return -5;
    static uint8_t before[REIST_VFS_SHADOW_EXT2_MAX_JOURNAL_ENTRIES]
                         [X86OS_STORAGE_BLOCK_SIZE];
    for (uint32_t index = 0U; index < header->count; ++index) {
        status = ext2_read_sector(
            volume, journal.sectors[index + 2U], before[index]);
        if (status != 0 ||
            ext2_crc32(before[index], X86OS_STORAGE_BLOCK_SIZE) !=
                header->old_crc[index]) return status != 0 ? status : -5;
    }
    uint8_t current[X86OS_STORAGE_BLOCK_SIZE];
    uint8_t complete = 1U;
    for (uint32_t index = 0U; index < header->count; ++index) {
        status = ext2_read_sector(volume, header->sectors[index], current);
        if (status != 0) return status;
        uint32_t current_crc = ext2_crc32(current, sizeof(current));
        if (current_crc != header->old_crc[index] &&
            current_crc != header->final_crc[index]) return -5;
        if (current_crc != header->final_crc[index]) complete = 0U;
    }
    if (header->state == EXT2_JOURNAL_STATE_COMMITTED && complete != 0U) {
            status = ext2_journal_clean(
                request, volume, &journal, header->sequence);
            if (status == 0 && journal_out != 0)
                ext2_journal_publish(journal_out, &journal);
            return status;
    }
    status = ext2_journal_restore(
        request, volume, &journal, header, before);
    if (status == 0 && journal_out != 0)
        ext2_journal_publish(journal_out, &journal);
    return status;
}

static void ext2_transaction_reset(void) {
    ext2_zero(&ext2_transaction, sizeof(ext2_transaction));
}

static int ext2_transaction_sector(ext2_shadow_volume_t *volume,
        uint32_t sector, uint8_t publish, uint8_t **data) {
    if (volume == 0 || data == 0 || sector >= volume->sectors) return -5;
    for (uint32_t index = 0U; index < ext2_transaction.count; ++index) {
        ext2_transaction_entry_t *entry = &ext2_transaction.entries[index];
        if (entry->sector != sector) continue;
        if (publish != 0U) entry->publish = 1U;
        *data = entry->final_data;
        return 0;
    }
    if (ext2_transaction.count >=
        REIST_VFS_SHADOW_EXT2_MAX_JOURNAL_ENTRIES) return -28;
    ext2_transaction_entry_t *entry =
        &ext2_transaction.entries[ext2_transaction.count];
    int status = ext2_read_sector(volume, sector, entry->old_data);
    if (status != 0) return status;
    entry->sector = sector;
    entry->publish = publish != 0U ? 1U : 0U;
    ext2_copy(entry->final_data, entry->old_data, X86OS_STORAGE_BLOCK_SIZE);
    *data = entry->final_data;
    ++ext2_transaction.count;
    return 0;
}

static int ext2_group_descriptor_location(
        const ext2_shadow_volume_t *volume, uint32_t group,
        uint32_t *sector, uint32_t *offset) {
    if (volume == 0 || sector == 0 || offset == 0 ||
        group >= volume->group_count ||
        group > UINT32_MAX / EXT2_GROUP_DESCRIPTOR_SIZE) return -5;
    uint32_t table_block = volume->block_size == 1024U ? 2U : 1U;
    uint32_t byte_offset = group * EXT2_GROUP_DESCRIPTOR_SIZE;
    uint64_t descriptor_sector =
        (uint64_t)table_block * volume->sectors_per_block +
        byte_offset / X86OS_STORAGE_BLOCK_SIZE;
    *offset = byte_offset % X86OS_STORAGE_BLOCK_SIZE;
    if (descriptor_sector >= volume->sectors ||
        *offset + EXT2_GROUP_DESCRIPTOR_SIZE > X86OS_STORAGE_BLOCK_SIZE)
        return -5;
    *sector = (uint32_t)descriptor_sector;
    return 0;
}

static int ext2_transaction_decrement_counters(
        ext2_shadow_volume_t *volume, uint32_t group, uint8_t inode) {
    uint8_t *superblock = 0;
    int status = ext2_transaction_sector(
        volume, EXT2_SUPERBLOCK_SECTOR, 0U, &superblock);
    if (status != 0) return status;
    uint32_t super_offset = inode != 0U ? 16U : 12U;
    uint32_t super_count = ext2_get32(superblock + super_offset);
    if (super_count == 0U) return -28;
    ext2_put32(superblock + super_offset, super_count - 1U);
    uint32_t descriptor_sector = 0U;
    uint32_t descriptor_offset = 0U;
    status = ext2_group_descriptor_location(
        volume, group, &descriptor_sector, &descriptor_offset);
    if (status != 0) return status;
    uint8_t *descriptor_data = 0;
    status = ext2_transaction_sector(
        volume, descriptor_sector, 0U, &descriptor_data);
    if (status != 0) return status;
    uint32_t count_offset = descriptor_offset + (inode != 0U ? 14U : 12U);
    uint16_t group_count = ext2_get16(descriptor_data + count_offset);
    if (group_count == 0U) return -28;
    ext2_put16(descriptor_data + count_offset,
               (uint16_t)(group_count - 1U));
    return 0;
}

static int ext2_allocate_inode(ext2_shadow_volume_t *volume,
                               uint32_t *inode_number) {
    if (volume == 0 || inode_number == 0) return -22;
    uint32_t groups = volume->group_count <
        REIST_VFS_SHADOW_EXT2_MAX_ALLOCATION_GROUPS
        ? volume->group_count : REIST_VFS_SHADOW_EXT2_MAX_ALLOCATION_GROUPS;
    uint8_t bitmap_sector[X86OS_STORAGE_BLOCK_SIZE];
    for (uint32_t group = 0U; group < groups; ++group) {
        uint8_t descriptor[EXT2_GROUP_DESCRIPTOR_SIZE];
        int status = ext2_group_descriptor(volume, group, descriptor);
        if (status != 0) return status;
        if (ext2_get16(descriptor + 14U) == 0U) continue;
        uint32_t bitmap_block = ext2_get32(descriptor + 4U);
        if (!ext2_data_block_valid(volume, bitmap_block)) return -5;
        uint32_t first = group * volume->inodes_per_group + 1U;
        uint32_t count = volume->inodes_per_group;
        if (first > volume->inodes_count) break;
        if (count > volume->inodes_count - first + 1U)
            count = volume->inodes_count - first + 1U;
        uint32_t loaded_sector = UINT32_MAX;
        for (uint32_t local = 0U; local < count; ++local) {
            uint32_t candidate = first + local;
            if (candidate < volume->first_inode) continue;
            uint32_t byte = local / 8U;
            uint32_t sector_in_block = byte / X86OS_STORAGE_BLOCK_SIZE;
            uint32_t physical = bitmap_block * volume->sectors_per_block +
                sector_in_block;
            if (physical != loaded_sector) {
                status = ext2_read_sector(volume, physical, bitmap_sector);
                if (status != 0) return status;
                loaded_sector = physical;
            }
            uint32_t byte_in_sector = byte % X86OS_STORAGE_BLOCK_SIZE;
            uint8_t mask = (uint8_t)(1U << (local & 7U));
            if ((bitmap_sector[byte_in_sector] & mask) != 0U) continue;
            uint8_t *planned = 0;
            status = ext2_transaction_sector(
                volume, physical, 0U, &planned);
            if (status != 0) return status;
            if ((planned[byte_in_sector] & mask) != 0U) continue;
            planned[byte_in_sector] |= mask;
            status = ext2_transaction_decrement_counters(volume, group, 1U);
            if (status != 0) return status;
            *inode_number = candidate;
            return 0;
        }
    }
    return -28;
}

static int ext2_allocate_block(ext2_shadow_volume_t *volume,
                               uint32_t *block_number) {
    if (volume == 0 || block_number == 0) return -22;
    uint32_t groups = volume->group_count <
        REIST_VFS_SHADOW_EXT2_MAX_ALLOCATION_GROUPS
        ? volume->group_count : REIST_VFS_SHADOW_EXT2_MAX_ALLOCATION_GROUPS;
    uint8_t bitmap_sector[X86OS_STORAGE_BLOCK_SIZE];
    for (uint32_t group = 0U; group < groups; ++group) {
        uint8_t descriptor[EXT2_GROUP_DESCRIPTOR_SIZE];
        int status = ext2_group_descriptor(volume, group, descriptor);
        if (status != 0) return status;
        if (ext2_get16(descriptor + 12U) == 0U) continue;
        uint32_t bitmap_block = ext2_get32(descriptor + 0U);
        if (!ext2_data_block_valid(volume, bitmap_block)) return -5;
        uint32_t first = volume->first_data_block +
            group * volume->blocks_per_group;
        uint32_t count = volume->blocks_per_group;
        if (first >= volume->blocks_count) break;
        if (count > volume->blocks_count - first)
            count = volume->blocks_count - first;
        uint32_t loaded_sector = UINT32_MAX;
        for (uint32_t local = 0U; local < count; ++local) {
            uint32_t byte = local / 8U;
            uint32_t sector_in_block = byte / X86OS_STORAGE_BLOCK_SIZE;
            uint32_t physical = bitmap_block * volume->sectors_per_block +
                sector_in_block;
            if (physical != loaded_sector) {
                status = ext2_read_sector(volume, physical, bitmap_sector);
                if (status != 0) return status;
                loaded_sector = physical;
            }
            uint32_t byte_in_sector = byte % X86OS_STORAGE_BLOCK_SIZE;
            uint8_t mask = (uint8_t)(1U << (local & 7U));
            if ((bitmap_sector[byte_in_sector] & mask) != 0U) continue;
            uint8_t *planned = 0;
            status = ext2_transaction_sector(
                volume, physical, 0U, &planned);
            if (status != 0) return status;
            if ((planned[byte_in_sector] & mask) != 0U) continue;
            planned[byte_in_sector] |= mask;
            status = ext2_transaction_decrement_counters(volume, group, 0U);
            if (status != 0) return status;
            *block_number = first + local;
            return ext2_data_block_valid(volume, *block_number) ? 0 : -5;
        }
    }
    return -28;
}

static int ext2_plan_inode(ext2_shadow_volume_t *volume,
        uint32_t inode_number, const char *target, uint32_t target_length,
        uint32_t target_block, uint32_t generation) {
    uint32_t zero_based = inode_number - 1U;
    uint32_t group = zero_based / volume->inodes_per_group;
    uint32_t index = zero_based % volume->inodes_per_group;
    uint8_t descriptor[EXT2_GROUP_DESCRIPTOR_SIZE];
    int status = ext2_group_descriptor(volume, group, descriptor);
    if (status != 0) return status;
    uint32_t inode_table = ext2_get32(descriptor + 8U);
    uint64_t absolute = (uint64_t)inode_table * volume->block_size +
        (uint64_t)index * volume->inode_size;
    uint32_t remaining = volume->inode_size;
    uint32_t copied = 0U;
    while (remaining != 0U) {
        uint32_t sector = (uint32_t)(absolute / X86OS_STORAGE_BLOCK_SIZE);
        uint32_t offset = (uint32_t)(absolute % X86OS_STORAGE_BLOCK_SIZE);
        uint32_t amount = X86OS_STORAGE_BLOCK_SIZE - offset;
        if (amount > remaining) amount = remaining;
        uint8_t *data = 0;
        status = ext2_transaction_sector(volume, sector, 0U, &data);
        if (status != 0) return status;
        ext2_zero(data + offset, amount);
        absolute += amount;
        remaining -= amount;
        copied += amount;
    }
    (void)copied;
    absolute = (uint64_t)inode_table * volume->block_size +
        (uint64_t)index * volume->inode_size;
    uint32_t sector = (uint32_t)(absolute / X86OS_STORAGE_BLOCK_SIZE);
    uint32_t offset = (uint32_t)(absolute % X86OS_STORAGE_BLOCK_SIZE);
    uint8_t *inode_sector = 0;
    status = ext2_transaction_sector(volume, sector, 0U, &inode_sector);
    if (status != 0 || offset + EXT2_INODE_CORE_SIZE >
        X86OS_STORAGE_BLOCK_SIZE) return status != 0 ? status : -5;
    uint8_t *inode = inode_sector + offset;
    ext2_put16(inode + 0U, (uint16_t)(EXT2_S_IFLNK | 0777U));
    ext2_put32(inode + 4U, target_length);
    ext2_put16(inode + 26U, 1U);
    ext2_put32(inode + 28U, target_block == 0U
        ? 0U : volume->sectors_per_block);
    ext2_put32(inode + 100U, generation);
    if (target_block == 0U) {
        ext2_copy(inode + 40U, target, target_length);
    } else {
        ext2_put32(inode + 40U, target_block);
    }
    return 0;
}

static int ext2_plan_target_block(ext2_shadow_volume_t *volume,
        uint32_t block, const char *target, uint32_t target_length) {
    uint32_t copied = 0U;
    for (uint32_t index = 0U; index < volume->sectors_per_block; ++index) {
        uint8_t *data = 0;
        int status = ext2_transaction_sector(
            volume, block * volume->sectors_per_block + index, 0U, &data);
        if (status != 0) return status;
        ext2_zero(data, X86OS_STORAGE_BLOCK_SIZE);
        uint32_t amount = target_length - copied;
        if (amount > X86OS_STORAGE_BLOCK_SIZE)
            amount = X86OS_STORAGE_BLOCK_SIZE;
        if (amount != 0U) ext2_copy(data, target + copied, amount);
        copied += amount;
    }
    return copied >= target_length ? 0 : -5;
}

static uint32_t ext2_directory_record_size(uint32_t name_length) {
    return (EXT2_DIRECTORY_HEADER_SIZE + name_length + 3U) & ~3U;
}

static int ext2_plan_directory_entry(ext2_shadow_volume_t *volume,
        const ext2_shadow_inode_t *directory, uint32_t inode_number,
        const char *name, uint32_t name_length) {
    uint32_t required = ext2_directory_record_size(name_length);
    uint32_t size = ext2_get32(directory->bytes + 4U);
    uint32_t blocks = ext2_div_ceil_u32(size, volume->block_size);
    if (blocks == 0U || blocks > REIST_VFS_SHADOW_EXT2_MAX_DIRECTORY_BLOCKS)
        return -110;
    uint8_t block_data[REIST_VFS_SHADOW_EXT2_MAX_BLOCK_SIZE];
    for (uint32_t logical = 0U; logical < blocks; ++logical) {
        uint32_t block = 0U;
        int status = ext2_inode_block(volume, directory, logical, &block);
        if (status != 0) return status;
        status = ext2_read_block(volume, block, block_data);
        if (status != 0) return status;
        uint32_t remaining = size - logical * volume->block_size;
        uint32_t limit = remaining < volume->block_size
            ? remaining : volume->block_size;
        for (uint32_t cursor = 0U; cursor < limit;) {
            if (limit - cursor < EXT2_DIRECTORY_HEADER_SIZE) return -5;
            uint32_t entry_inode = ext2_get32(block_data + cursor);
            uint32_t record = ext2_get16(block_data + cursor + 4U);
            uint32_t old_name_length = volume->has_file_type != 0U
                ? block_data[cursor + 6U]
                : ext2_get16(block_data + cursor + 6U);
            if (record < EXT2_DIRECTORY_HEADER_SIZE ||
                (record & 3U) != 0U || record > limit - cursor ||
                old_name_length > 255U ||
                old_name_length > record - EXT2_DIRECTORY_HEADER_SIZE)
                return -5;
            uint32_t used = entry_inode == 0U ? 0U
                : ext2_directory_record_size(old_name_length);
            uint32_t new_offset = entry_inode == 0U ? cursor : cursor + used;
            uint32_t available = entry_inode == 0U ? record : record - used;
            if (available >= required &&
                cursor / X86OS_STORAGE_BLOCK_SIZE ==
                    new_offset / X86OS_STORAGE_BLOCK_SIZE &&
                new_offset % X86OS_STORAGE_BLOCK_SIZE + required <=
                    X86OS_STORAGE_BLOCK_SIZE) {
                uint32_t sector = block * volume->sectors_per_block +
                    new_offset / X86OS_STORAGE_BLOCK_SIZE;
                uint8_t *planned = 0;
                status = ext2_transaction_sector(
                    volume, sector, 1U, &planned);
                if (status != 0) return status;
                uint32_t current_offset = cursor % X86OS_STORAGE_BLOCK_SIZE;
                uint32_t local = new_offset % X86OS_STORAGE_BLOCK_SIZE;
                if (entry_inode != 0U)
                    ext2_put16(planned + current_offset + 4U,
                               (uint16_t)used);
                ext2_zero(planned + local, required);
                ext2_put32(planned + local + 0U, inode_number);
                ext2_put16(planned + local + 4U, (uint16_t)available);
                if (volume->has_file_type != 0U) {
                    planned[local + 6U] = (uint8_t)name_length;
                    planned[local + 7U] = EXT2_FT_SYMLINK;
                } else {
                    ext2_put16(planned + local + 6U,
                               (uint16_t)name_length);
                }
                ext2_copy(planned + local + 8U, name, name_length);
                return 0;
            }
            cursor += record;
        }
    }
    return -28;
}

static int ext2_recover_path_request(ext2_request_t *request,
        const char *absolute_path, uint32_t path_length,
        uint8_t require_journal, ext2_shadow_volume_t *volume,
        ext2_journal_t *journal) {
    uint32_t relative = 0U;
    int status = ext2_mount(
        volume, request, absolute_path, path_length, &relative);
    if (status != 0) return status;
    status = ext2_parse_superblock(volume);
    if (status != 0) return status;
    return ext2_journal_recover(
        request, volume, require_journal, journal);
}

static int ext2_transaction_journal(ext2_request_t *request,
        const ext2_shadow_volume_t *volume, const ext2_journal_t *journal,
        uint32_t sequence) {
    for (uint32_t index = 0U; index < ext2_transaction.count; ++index) {
        ext2_transaction_entry_t *entry = &ext2_transaction.entries[index];
        entry->old_crc = ext2_crc32(
            entry->old_data, X86OS_STORAGE_BLOCK_SIZE);
        entry->final_crc = ext2_crc32(
            entry->final_data, X86OS_STORAGE_BLOCK_SIZE);
        for (uint32_t reserved = 0U;
             reserved < REIST_VFS_SHADOW_EXT2_JOURNAL_SECTORS; ++reserved)
            if (entry->sector == journal->sectors[reserved]) return -5;
    }
    for (uint32_t index = 0U; index < ext2_transaction.count; ++index) {
        ext2_transaction_entry_t *entry = &ext2_transaction.entries[index];
        int status = ext2_request_write(
            request, volume->resource, journal->sectors[index + 2U],
            entry->old_data);
        if (status != 0) return status;
    }
    int status = ext2_request_flush(request, volume->resource);
    if (status != 0) return status;
    status = ext2_journal_write_headers(
        request, volume, journal, EXT2_JOURNAL_STATE_ACTIVE, sequence,
        &ext2_transaction);
    if (status == 0) status = ext2_request_flush(request, volume->resource);
    return status;
}

static int ext2_transaction_apply(ext2_request_t *request,
        const ext2_shadow_volume_t *volume) {
    for (uint32_t pass = 0U; pass < 2U; ++pass) {
        uint32_t writes = 0U;
        for (uint32_t index = 0U; index < ext2_transaction.count; ++index) {
            ext2_transaction_entry_t *entry = &ext2_transaction.entries[index];
            if ((pass == 0U && entry->publish != 0U) ||
                (pass == 1U && entry->publish == 0U)) continue;
            int status = ext2_request_write(
                request, volume->resource, entry->sector, entry->final_data);
            if (status != 0) return status;
            ++writes;
        }
        if (writes != 0U) {
            int status = ext2_request_flush(request, volume->resource);
            if (status != 0) return status;
        }
    }
    return 0;
}

static int ext2_transaction_verify(ext2_shadow_volume_t *volume) {
    uint8_t sector[X86OS_STORAGE_BLOCK_SIZE];
    for (uint32_t index = 0U; index < ext2_transaction.count; ++index) {
        int status = ext2_read_sector(
            volume, ext2_transaction.entries[index].sector, sector);
        if (status != 0) return status;
        if (ext2_crc32(sector, sizeof(sector)) !=
            ext2_transaction.entries[index].final_crc) return -5;
    }
    return 0;
}

static int ext2_symlink_inputs(const char *target, uint32_t target_length,
        const char *link_path, uint32_t link_path_length) {
    if (target == 0 || link_path == 0 || target_length == 0U ||
        target_length >= X86OS_VFS_SYMLINK_TARGET_CAPACITY ||
        link_path_length <= 1U ||
        link_path_length >= X86OS_VFS_SHADOW_PATH_CAPACITY ||
        target[target_length] != '\0' || link_path[0U] != '/' ||
        link_path[link_path_length] != '\0') return -22;
    for (uint32_t index = 0U; index < target_length; ++index) {
        uint8_t value = (uint8_t)target[index];
        if (value < 0x20U || value > 0x7EU || value == '\0') return -22;
    }
    for (uint32_t index = 0U; index < link_path_length; ++index)
        if (link_path[index] == '\0') return -22;
    return 0;
}

static int ext2_symlink_request(ext2_request_t *request,
        const char *target, uint32_t target_length,
        const char *absolute_link_path, uint32_t link_path_length) {
    int status = ext2_symlink_inputs(
        target, target_length, absolute_link_path, link_path_length);
    if (status != 0 || request->write_sector == 0 || request->flush == 0)
        return status != 0 ? status : -30;
    ext2_shadow_volume_t journal_volume;
    ext2_journal_t journal;
    status = ext2_recover_path_request(
        request, absolute_link_path, link_path_length, 1U,
        &journal_volume, &journal);
    if (status == -2) return -95;
    if (status != 0) return status;
    /* Recovery is a complete bounded operation of its own.  Do not append a
     * fresh namespace mutation to the same request after repair writes. */
    if (request->writes != 0U || request->flushes != 0U) return -11;
    uint32_t name_start = link_path_length;
    while (name_start > 0U && absolute_link_path[name_start - 1U] != '/')
        --name_start;
    if (name_start == 0U || name_start >= link_path_length) return -22;
    uint32_t name_length = link_path_length - name_start;
    if (!ext2_component_valid(
            absolute_link_path + name_start, name_length)) return -22;
    uint32_t parent_length = name_start - 1U;
    if (parent_length == 0U) parent_length = 1U;
    char parent_path[X86OS_VFS_SHADOW_PATH_CAPACITY];
    ext2_zero(parent_path, sizeof(parent_path));
    ext2_copy(parent_path, absolute_link_path, parent_length);
    parent_path[parent_length] = '\0';
    ext2_shadow_volume_t volume;
    ext2_shadow_inode_t directory;
    char visible[256];
    uint32_t directory_inode = 0U;
    status = ext2_resolve(
        request, parent_path, parent_length, 0U, &volume, &directory,
        visible, &directory_inode);
    if (status != 0) return status;
    if (volume.resource != journal_volume.resource ||
        volume.signature != journal_volume.signature) return -18;
    if ((ext2_get16(directory.bytes + 0U) & EXT2_S_IFMT) != EXT2_S_IFDIR)
        return -20;
    char name[256];
    ext2_zero(name, sizeof(name));
    ext2_copy(name, absolute_link_path + name_start, name_length);
    uint32_t existing = 0U;
    status = ext2_find_entry(&volume, &directory, name, &existing, visible);
    if (status == 0) return -17;
    if (status != -2) return status;
    if (journal.sequence == UINT32_MAX) return -75;
    uint32_t sequence = journal.sequence + 1U;
    if (sequence == 0U) return -75;
    ext2_transaction_reset();
    uint32_t inode_number = 0U;
    status = ext2_allocate_inode(&volume, &inode_number);
    uint32_t target_block = 0U;
    if (status == 0 && target_length > EXT2_FAST_SYMLINK_CAPACITY)
        status = ext2_allocate_block(&volume, &target_block);
    if (status == 0 && target_block != 0U)
        status = ext2_plan_target_block(
            &volume, target_block, target, target_length);
    if (status == 0)
        status = ext2_plan_inode(
            &volume, inode_number, target, target_length, target_block,
            sequence);
    if (status == 0)
        status = ext2_plan_directory_entry(
            &volume, &directory, inode_number, name, name_length);
    if (status != 0 || ext2_transaction.count == 0U)
        return status != 0 ? status : -5;
    status = ext2_transaction_journal(
        request, &volume, &journal, sequence);
    if (status != 0) return status;
    status = ext2_transaction_apply(request, &volume);
    if (status != 0) return status;
    ext2_shadow_volume_t verify_volume;
    status = ext2_volume_for_resource(
        request, volume.resource, &verify_volume);
    if (status == 0) status = ext2_transaction_verify(&verify_volume);
    if (status == 0) {
        ext2_shadow_inode_t verify_inode;
        uint32_t verify_number = 0U;
        status = ext2_resolve(
            request, absolute_link_path, link_path_length,
            REIST_VFS_SHADOW_EXT2_NOFOLLOW_FINAL, &verify_volume,
            &verify_inode, visible, &verify_number);
        if (status == 0 && verify_number != inode_number) status = -5;
        char verify_target[X86OS_VFS_SYMLINK_TARGET_CAPACITY];
        uint32_t verify_length = 0U;
        if (status == 0)
            status = ext2_read_symlink_inode(
                &verify_volume, &verify_inode, verify_target, &verify_length);
        if (status == 0 && verify_length != target_length) status = -5;
        for (uint32_t index = 0U;
             status == 0 && index < target_length; ++index)
            if (verify_target[index] != target[index]) status = -5;
    }
    if (status != 0) return status;
    status = ext2_journal_write_headers(
        request, &volume, &journal, EXT2_JOURNAL_STATE_COMMITTED,
        sequence, &ext2_transaction);
    if (status == 0) status = ext2_request_flush(request, volume.resource);
    if (status == 0)
        status = ext2_journal_clean(
            request, &volume, &journal, sequence);
    if (status == 0) ext2_transaction_reset();
    return status;
}

typedef struct {
    uint32_t sector;
    uint32_t sector_first_offset;
    uint32_t sector_block_remaining;
    uint32_t local_offset;
    uint32_t record_length;
    uint32_t name_length;
    uint32_t inode_number;
} ext2_directory_location_t;

static int ext2_namespace_split(
        const char *path, uint32_t path_length,
        char parent[X86OS_VFS_SHADOW_PATH_CAPACITY],
        uint32_t *parent_length, char name[256], uint32_t *name_length) {
    if (path == 0 || parent == 0 || parent_length == 0 || name == 0 ||
        name_length == 0 || path_length <= 1U ||
        path_length >= X86OS_VFS_SHADOW_PATH_CAPACITY || path[0U] != '/' ||
        path[path_length] != '\0') return -22;
    uint32_t start = path_length;
    while (start > 0U && path[start - 1U] != '/') --start;
    if (start == 0U || start >= path_length) return -22;
    *name_length = path_length - start;
    if (!ext2_component_valid(path + start, *name_length)) return -22;
    *parent_length = start - 1U;
    if (*parent_length == 0U) *parent_length = 1U;
    ext2_zero(parent, X86OS_VFS_SHADOW_PATH_CAPACITY);
    ext2_zero(name, 256U);
    ext2_copy(parent, path, *parent_length);
    parent[*parent_length] = '\0';
    ext2_copy(name, path + start, *name_length);
    name[*name_length] = '\0';
    return 0;
}

static int ext2_directory_locate(
        ext2_shadow_volume_t *volume, const ext2_shadow_inode_t *directory,
        const char *name, ext2_directory_location_t *location) {
    if (volume == 0 || directory == 0 || name == 0 || location == 0)
        return -22;
    uint32_t size = ext2_get32(directory->bytes + 4U);
    uint32_t blocks = ext2_div_ceil_u32(size, volume->block_size);
    if (blocks == 0U || blocks > REIST_VFS_SHADOW_EXT2_MAX_DIRECTORY_BLOCKS)
        return -110;
    uint8_t data[REIST_VFS_SHADOW_EXT2_MAX_BLOCK_SIZE];
    for (uint32_t logical = 0U; logical < blocks; ++logical) {
        uint32_t block = 0U;
        int status = ext2_inode_block(volume, directory, logical, &block);
        if (status != 0) return status;
        status = ext2_read_block(volume, block, data);
        if (status != 0) return status;
        uint32_t remaining = size - logical * volume->block_size;
        uint32_t limit = remaining < volume->block_size
            ? remaining : volume->block_size;
        uint32_t sector_index = UINT32_MAX;
        uint32_t sector_first_offset = 0U;
        for (uint32_t offset = 0U; offset < limit;) {
            uint32_t entry_sector = offset / X86OS_STORAGE_BLOCK_SIZE;
            if (entry_sector != sector_index) {
                sector_index = entry_sector;
                sector_first_offset = offset % X86OS_STORAGE_BLOCK_SIZE;
            }
            if (limit - offset < EXT2_DIRECTORY_HEADER_SIZE) return -5;
            uint32_t inode_number = ext2_get32(data + offset);
            uint32_t record = ext2_get16(data + offset + 4U);
            uint32_t found_length = volume->has_file_type != 0U
                ? data[offset + 6U] : ext2_get16(data + offset + 6U);
            if (record < EXT2_DIRECTORY_HEADER_SIZE || (record & 3U) != 0U ||
                record > limit - offset || found_length > 255U ||
                found_length > record - EXT2_DIRECTORY_HEADER_SIZE ||
                (inode_number != 0U && inode_number > volume->inodes_count))
                return -5;
            if (inode_number != 0U && ext2_name_equal(
                    data + offset + 8U, found_length, name)) {
                uint32_t local = offset % X86OS_STORAGE_BLOCK_SIZE;
                if (local + EXT2_DIRECTORY_HEADER_SIZE + found_length >
                    X86OS_STORAGE_BLOCK_SIZE) return -28;
                location->sector = block * volume->sectors_per_block +
                    offset / X86OS_STORAGE_BLOCK_SIZE;
                location->sector_first_offset = sector_first_offset;
                location->sector_block_remaining = limit -
                    entry_sector * X86OS_STORAGE_BLOCK_SIZE;
                location->local_offset = local;
                location->record_length = record;
                location->name_length = found_length;
                location->inode_number = inode_number;
                return 0;
            }
            offset += record;
        }
    }
    return -2;
}

static int ext2_transaction_increment_counters(
        ext2_shadow_volume_t *volume, uint32_t group, uint8_t inode) {
    uint8_t *superblock = 0;
    int status = ext2_transaction_sector(
        volume, EXT2_SUPERBLOCK_SECTOR, 0U, &superblock);
    if (status != 0) return status;
    uint32_t super_offset = inode != 0U ? 16U : 12U;
    uint32_t super_count = ext2_get32(superblock + super_offset);
    uint32_t super_limit = inode != 0U
        ? volume->inodes_count : volume->blocks_count;
    if (super_count >= super_limit) return -5;
    ext2_put32(superblock + super_offset, super_count + 1U);
    uint32_t descriptor_sector = 0U;
    uint32_t descriptor_offset = 0U;
    status = ext2_group_descriptor_location(
        volume, group, &descriptor_sector, &descriptor_offset);
    if (status != 0) return status;
    uint8_t *descriptor = 0;
    status = ext2_transaction_sector(
        volume, descriptor_sector, 0U, &descriptor);
    if (status != 0) return status;
    uint32_t count_offset = descriptor_offset + (inode != 0U ? 14U : 12U);
    uint16_t count = ext2_get16(descriptor + count_offset);
    uint32_t group_limit = inode != 0U
        ? volume->inodes_per_group : volume->blocks_per_group;
    if ((uint32_t)count >= group_limit) return -5;
    ext2_put16(descriptor + count_offset, (uint16_t)(count + 1U));
    return 0;
}

static int ext2_release_allocation(ext2_shadow_volume_t *volume,
                                   uint32_t number, uint8_t inode) {
    if (volume == 0 || number == 0U) return -22;
    uint32_t group = 0U;
    uint32_t local = 0U;
    uint32_t bitmap_offset = inode != 0U ? 4U : 0U;
    if (inode != 0U) {
        if (number > volume->inodes_count) return -5;
        uint32_t zero_based = number - 1U;
        group = zero_based / volume->inodes_per_group;
        local = zero_based % volume->inodes_per_group;
    } else {
        if (!ext2_data_block_valid(volume, number) ||
            number < volume->first_data_block) return -5;
        uint32_t zero_based = number - volume->first_data_block;
        group = zero_based / volume->blocks_per_group;
        local = zero_based % volume->blocks_per_group;
    }
    uint8_t descriptor[EXT2_GROUP_DESCRIPTOR_SIZE];
    int status = ext2_group_descriptor(volume, group, descriptor);
    if (status != 0) return status;
    uint32_t bitmap_block = ext2_get32(descriptor + bitmap_offset);
    if (!ext2_data_block_valid(volume, bitmap_block)) return -5;
    uint32_t byte = local / 8U;
    uint32_t sector = bitmap_block * volume->sectors_per_block +
        byte / X86OS_STORAGE_BLOCK_SIZE;
    uint32_t local_byte = byte % X86OS_STORAGE_BLOCK_SIZE;
    uint8_t mask = (uint8_t)(1U << (local & 7U));
    uint8_t *bitmap = 0;
    status = ext2_transaction_sector(volume, sector, 0U, &bitmap);
    if (status != 0) return status;
    if ((bitmap[local_byte] & mask) == 0U) return -5;
    bitmap[local_byte] &= (uint8_t)~mask;
    return ext2_transaction_increment_counters(volume, group, inode);
}

static int ext2_clear_inode(ext2_shadow_volume_t *volume,
                            uint32_t inode_number) {
    if (volume == 0 || inode_number == 0U ||
        inode_number > volume->inodes_count) return -22;
    uint32_t zero_based = inode_number - 1U;
    uint32_t group = zero_based / volume->inodes_per_group;
    uint32_t index = zero_based % volume->inodes_per_group;
    uint8_t descriptor[EXT2_GROUP_DESCRIPTOR_SIZE];
    int status = ext2_group_descriptor(volume, group, descriptor);
    if (status != 0) return status;
    uint32_t inode_table = ext2_get32(descriptor + 8U);
    uint64_t absolute = (uint64_t)inode_table * volume->block_size +
        (uint64_t)index * volume->inode_size;
    uint32_t remaining = volume->inode_size;
    while (remaining != 0U) {
        uint32_t sector = (uint32_t)(absolute / X86OS_STORAGE_BLOCK_SIZE);
        uint32_t offset = (uint32_t)(absolute % X86OS_STORAGE_BLOCK_SIZE);
        uint32_t amount = X86OS_STORAGE_BLOCK_SIZE - offset;
        if (amount > remaining) amount = remaining;
        uint8_t *data = 0;
        status = ext2_transaction_sector(volume, sector, 0U, &data);
        if (status != 0) return status;
        ext2_zero(data + offset, amount);
        absolute += amount;
        remaining -= amount;
    }
    return 0;
}

static int ext2_plan_directory_remove(
        ext2_shadow_volume_t *volume,
        const ext2_directory_location_t *location, const char *name) {
    if (volume == 0 || location == 0 || name == 0) return -22;
    uint8_t *data = 0;
    int status = ext2_transaction_sector(
        volume, location->sector, 1U, &data);
    if (status != 0) return status;
    uint32_t local = location->local_offset;
    if (ext2_get32(data + local) != location->inode_number ||
        ext2_get16(data + local + 4U) != location->record_length ||
        !ext2_name_equal(data + local + 8U, location->name_length, name))
        return -5;
    ext2_put32(data + local, 0U);
    if (volume->has_file_type != 0U) {
        data[local + 6U] = 0U;
        data[local + 7U] = 0U;
    } else {
        ext2_put16(data + local + 6U, 0U);
    }
    ext2_zero(data + local + 8U, location->name_length);
    return 0;
}

static int ext2_plan_directory_rename(
        ext2_shadow_volume_t *volume,
        const ext2_directory_location_t *location, const char *old_name,
        const char *new_name, uint32_t new_length) {
    if (volume == 0 || location == 0 || old_name == 0 || new_name == 0 ||
        !ext2_component_valid(new_name, new_length) ||
        new_length > location->record_length - EXT2_DIRECTORY_HEADER_SIZE ||
        location->local_offset + EXT2_DIRECTORY_HEADER_SIZE + new_length >
            X86OS_STORAGE_BLOCK_SIZE)
        return -28;
    uint8_t *data = 0;
    int status = ext2_transaction_sector(
        volume, location->sector, 1U, &data);
    if (status != 0) return status;
    uint32_t local = location->local_offset;
    if (ext2_get32(data + local) != location->inode_number ||
        ext2_get16(data + local + 4U) != location->record_length ||
        !ext2_name_equal(data + local + 8U, location->name_length, old_name))
        return -5;
    uint32_t clear = location->name_length > new_length
        ? location->name_length : new_length;
    ext2_zero(data + local + 8U, clear);
    if (volume->has_file_type != 0U)
        data[local + 6U] = (uint8_t)new_length;
    else
        ext2_put16(data + local + 6U, (uint16_t)new_length);
    ext2_copy(data + local + 8U, new_name, new_length);
    return 0;
}

static int ext2_plan_directory_relocate(
        ext2_shadow_volume_t *volume,
        const ext2_directory_location_t *location, const char *old_name,
        const char *new_name, uint32_t new_length) {
    if (volume == 0 || location == 0 || old_name == 0 || new_name == 0 ||
        !ext2_component_valid(new_name, new_length) ||
        location->sector_first_offset >= X86OS_STORAGE_BLOCK_SIZE ||
        location->sector_block_remaining < EXT2_DIRECTORY_HEADER_SIZE ||
        location->local_offset + EXT2_DIRECTORY_HEADER_SIZE +
            location->name_length > X86OS_STORAGE_BLOCK_SIZE)
        return -28;
    uint32_t required = ext2_directory_record_size(new_length);
    uint8_t current[X86OS_STORAGE_BLOCK_SIZE];
    int status = ext2_read_sector(volume, location->sector, current);
    if (status != 0) return status;
    uint32_t source = location->local_offset;
    if (ext2_get32(current + source) != location->inode_number ||
        ext2_get16(current + source + 4U) != location->record_length ||
        !ext2_name_equal(
            current + source + 8U, location->name_length, old_name))
        return -5;

    uint32_t donor = UINT32_MAX;
    uint32_t destination = 0U;
    uint32_t donor_used = 0U;
    uint32_t destination_record = 0U;
    for (uint32_t cursor = location->sector_first_offset;
         cursor < X86OS_STORAGE_BLOCK_SIZE;) {
        if (cursor >= location->sector_block_remaining ||
            X86OS_STORAGE_BLOCK_SIZE - cursor <
                EXT2_DIRECTORY_HEADER_SIZE ||
            location->sector_block_remaining - cursor <
                EXT2_DIRECTORY_HEADER_SIZE)
            break;
        uint32_t inode_number = ext2_get32(current + cursor);
        uint32_t record = ext2_get16(current + cursor + 4U);
        uint32_t name_length = volume->has_file_type != 0U
            ? current[cursor + 6U]
            : ext2_get16(current + cursor + 6U);
        if (record < EXT2_DIRECTORY_HEADER_SIZE || (record & 3U) != 0U ||
            record > location->sector_block_remaining - cursor ||
            name_length > 255U ||
            name_length > record - EXT2_DIRECTORY_HEADER_SIZE ||
            (inode_number != 0U && inode_number > volume->inodes_count))
            return -5;
        uint32_t used = inode_number == 0U ? 0U
            : ext2_directory_record_size(name_length);
        uint32_t new_offset = inode_number == 0U ? cursor : cursor + used;
        uint32_t available = inode_number == 0U ? record : record - used;
        if (cursor != source && available >= required &&
            new_offset <= X86OS_STORAGE_BLOCK_SIZE - required) {
            donor = cursor;
            destination = new_offset;
            donor_used = used;
            destination_record = available;
            break;
        }
        if (record > UINT32_MAX - cursor) return -5;
        cursor += record;
    }
    if (donor == UINT32_MAX) return -28;

    uint8_t *planned = 0;
    status = ext2_transaction_sector(
        volume, location->sector, 1U, &planned);
    if (status != 0) return status;
    for (uint32_t index = 0U; index < X86OS_STORAGE_BLOCK_SIZE; ++index)
        if (planned[index] != current[index]) return -11;
    uint8_t file_type = volume->has_file_type != 0U
        ? planned[source + 7U] : 0U;
    if (donor_used != 0U)
        ext2_put16(planned + donor + 4U, (uint16_t)donor_used);
    ext2_zero(planned + destination, required);
    ext2_put32(planned + destination, location->inode_number);
    ext2_put16(planned + destination + 4U,
               (uint16_t)destination_record);
    if (volume->has_file_type != 0U) {
        planned[destination + 6U] = (uint8_t)new_length;
        planned[destination + 7U] = file_type;
    } else {
        ext2_put16(planned + destination + 6U, (uint16_t)new_length);
    }
    ext2_copy(planned + destination + 8U, new_name, new_length);

    ext2_put32(planned + source, 0U);
    if (volume->has_file_type != 0U) {
        planned[source + 6U] = 0U;
        planned[source + 7U] = 0U;
    } else {
        ext2_put16(planned + source + 6U, 0U);
    }
    ext2_zero(planned + source + 8U, location->name_length);
    return 0;
}

static int ext2_namespace_finish(ext2_request_t *request,
        ext2_shadow_volume_t *volume, const ext2_journal_t *journal,
        uint32_t sequence) {
    int status = ext2_transaction_journal(
        request, volume, journal, sequence);
    if (status == 0) status = ext2_transaction_apply(request, volume);
    ext2_shadow_volume_t verify_volume;
    if (status == 0)
        status = ext2_volume_for_resource(
            request, volume->resource, &verify_volume);
    if (status == 0) status = ext2_transaction_verify(&verify_volume);
    return status;
}

static int ext2_namespace_commit(ext2_request_t *request,
        ext2_shadow_volume_t *volume, const ext2_journal_t *journal,
        uint32_t sequence) {
    int status = ext2_journal_write_headers(
        request, volume, journal, EXT2_JOURNAL_STATE_COMMITTED,
        sequence, &ext2_transaction);
    if (status == 0) status = ext2_request_flush(request, volume->resource);
    if (status == 0)
        status = ext2_journal_clean(request, volume, journal, sequence);
    if (status == 0) ext2_transaction_reset();
    return status;
}

static int ext2_unlink_request(ext2_request_t *request,
        const char *absolute_path, uint32_t path_length,
        uint8_t symlink_only) {
    char parent[X86OS_VFS_SHADOW_PATH_CAPACITY];
    char name[256];
    uint32_t parent_length = 0U;
    uint32_t name_length = 0U;
    int status = ext2_namespace_split(
        absolute_path, path_length, parent, &parent_length,
        name, &name_length);
    if (status != 0 || request == 0 || request->write_sector == 0 ||
        request->flush == 0) return status != 0 ? status : -30;
    ext2_shadow_volume_t volume;
    ext2_journal_t journal;
    status = ext2_recover_path_request(
        request, absolute_path, path_length, 1U, &volume, &journal);
    if (status == -2) return -95;
    if (status != 0) return status;
    if (request->writes != 0U || request->flushes != 0U) return -11;
    ext2_shadow_volume_t parent_volume;
    ext2_shadow_inode_t directory;
    char visible[256];
    uint32_t directory_inode = 0U;
    status = ext2_resolve(
        request, parent, parent_length, 0U, &parent_volume, &directory,
        visible, &directory_inode);
    if (status != 0) return status;
    if (parent_volume.resource != volume.resource ||
        parent_volume.signature != volume.signature) return -18;
    if ((ext2_get16(directory.bytes) & EXT2_S_IFMT) != EXT2_S_IFDIR)
        return -20;
    ext2_directory_location_t location;
    status = ext2_directory_locate(
        &parent_volume, &directory, name, &location);
    if (status != 0) return status;
    ext2_shadow_inode_t inode;
    status = ext2_read_inode(&parent_volume, location.inode_number, &inode);
    if (status != 0) return status;
    if (location.inode_number == journal.inode_number) return -16;
    uint16_t mode = ext2_get16(inode.bytes) & EXT2_S_IFMT;
    if (mode != EXT2_S_IFLNK &&
        (symlink_only != 0U || mode != EXT2_S_IFREG))
        return -95;
    if (ext2_get16(inode.bytes + 26U) != 1U) return -31;
    char target[X86OS_VFS_SYMLINK_TARGET_CAPACITY];
    uint32_t target_length = 0U;
    if (mode == EXT2_S_IFLNK)
        status = ext2_read_symlink_inode(
            &parent_volume, &inode, target, &target_length);
    if (status != 0) return status;
    uint32_t target_block = mode == EXT2_S_IFLNK &&
        target_length > EXT2_FAST_SYMLINK_CAPACITY
        ? ext2_get32(inode.bytes + 40U) : 0U;
    ext2_regular_allocations_t regular_allocations;
    ext2_zero(&regular_allocations, sizeof(regular_allocations));
    if (mode == EXT2_S_IFREG) {
        status = ext2_regular_allocations(
            &parent_volume, &inode, &directory, &journal,
            &regular_allocations);
        if (status != 0) return status;
    } else if (target_block != 0U) {
        if (!ext2_data_block_valid(&parent_volume, target_block) ||
            ext2_get32(inode.bytes + 28U) !=
                parent_volume.sectors_per_block) return -5;
        for (uint32_t index = 1U; index < 15U; ++index)
            if (ext2_get32(inode.bytes + 40U + index * 4U) != 0U)
                return -5;
    } else if (ext2_get32(inode.bytes + 28U) != 0U) {
        return -5;
    }
    if (journal.sequence == UINT32_MAX) return -75;
    uint32_t sequence = journal.sequence + 1U;
    ext2_transaction_reset();
    status = ext2_plan_directory_remove(
        &parent_volume, &location, name);
    if (status == 0 && target_block != 0U)
        status = ext2_release_allocation(
            &parent_volume, target_block, 0U);
    for (uint32_t index = 0U;
         status == 0 && index < regular_allocations.count; ++index)
        status = ext2_release_allocation(
            &parent_volume, regular_allocations.blocks[index], 0U);
    if (status == 0)
        status = ext2_release_allocation(
            &parent_volume, location.inode_number, 1U);
    if (status == 0)
        status = ext2_clear_inode(&parent_volume, location.inode_number);
    if (status != 0 || ext2_transaction.count == 0U)
        return status != 0 ? status : -5;
    status = ext2_namespace_finish(
        request, &parent_volume, &journal, sequence);
    if (status == 0) {
        status = ext2_resolve(
            request, absolute_path, path_length,
            REIST_VFS_SHADOW_EXT2_NOFOLLOW_FINAL, &volume, &inode,
            visible, &directory_inode);
        if (status == -2) status = 0;
        else status = status == 0 ? -5 : status;
    }
    if (status != 0) return status;
    return ext2_namespace_commit(
        request, &parent_volume, &journal, sequence);
}

static int ext2_rename_request(ext2_request_t *request,
        const char *source_path, uint32_t source_length,
        const char *destination_path, uint32_t destination_length,
        uint8_t symlink_only) {
    char source_parent[X86OS_VFS_SHADOW_PATH_CAPACITY];
    char destination_parent[X86OS_VFS_SHADOW_PATH_CAPACITY];
    char source_name[256];
    char destination_name[256];
    uint32_t source_parent_length = 0U;
    uint32_t destination_parent_length = 0U;
    uint32_t source_name_length = 0U;
    uint32_t destination_name_length = 0U;
    int status = ext2_namespace_split(
        source_path, source_length, source_parent, &source_parent_length,
        source_name, &source_name_length);
    if (status == 0)
        status = ext2_namespace_split(
            destination_path, destination_length, destination_parent,
            &destination_parent_length, destination_name,
            &destination_name_length);
    if (status != 0 || request == 0 || request->write_sector == 0 ||
        request->flush == 0) return status != 0 ? status : -30;
    if (source_parent_length != destination_parent_length)
        return -18;
    for (uint32_t index = 0U; index <= source_parent_length; ++index)
        if (source_parent[index] != destination_parent[index]) return -18;
    ext2_shadow_volume_t volume;
    ext2_journal_t journal;
    status = ext2_recover_path_request(
        request, source_path, source_length, 1U, &volume, &journal);
    if (status == -2) return -95;
    if (status != 0) return status;
    if (request->writes != 0U || request->flushes != 0U) return -11;
    ext2_shadow_inode_t directory;
    char visible[256];
    uint32_t directory_inode = 0U;
    status = ext2_resolve(
        request, source_parent, source_parent_length, 0U, &volume,
        &directory, visible, &directory_inode);
    if (status != 0) return status;
    ext2_directory_location_t location;
    status = ext2_directory_locate(&volume, &directory, source_name,
                                   &location);
    if (status != 0) return status;
    uint32_t existing = 0U;
    status = ext2_find_entry(
        &volume, &directory, destination_name, &existing, visible);
    if (status == 0) return -17;
    if (status != -2) return status;
    ext2_shadow_inode_t inode;
    status = ext2_read_inode(&volume, location.inode_number, &inode);
    if (status != 0) return status;
    uint16_t mode = ext2_get16(inode.bytes) & EXT2_S_IFMT;
    if (mode != EXT2_S_IFLNK &&
        (symlink_only != 0U || mode != EXT2_S_IFREG))
        return -95;
    char target[X86OS_VFS_SYMLINK_TARGET_CAPACITY];
    uint32_t target_length = 0U;
    if (mode == EXT2_S_IFLNK)
        status = ext2_read_symlink_inode(
            &volume, &inode, target, &target_length);
    if (status != 0) return status;
    if (journal.sequence == UINT32_MAX) return -75;
    uint32_t sequence = journal.sequence + 1U;
    ext2_transaction_reset();
    status = ext2_plan_directory_rename(
        &volume, &location, source_name, destination_name,
        destination_name_length);
    if (status == -28) {
        ext2_transaction_reset();
        status = ext2_plan_directory_relocate(
            &volume, &location, source_name, destination_name,
            destination_name_length);
    }
    if (status != 0 || ext2_transaction.count != 1U)
        return status != 0 ? status : -5;
    status = ext2_namespace_finish(request, &volume, &journal, sequence);
    if (status == 0) {
        ext2_shadow_inode_t verify_inode;
        uint32_t verify_number = 0U;
        status = ext2_resolve(
            request, source_path, source_length,
            REIST_VFS_SHADOW_EXT2_NOFOLLOW_FINAL, &volume, &verify_inode,
            visible, &verify_number);
        if (status == -2) status = 0;
        else status = status == 0 ? -5 : status;
        if (status == 0)
            status = ext2_resolve(
                request, destination_path, destination_length,
                REIST_VFS_SHADOW_EXT2_NOFOLLOW_FINAL, &volume,
                &verify_inode, visible, &verify_number);
        if (status == 0 && verify_number != location.inode_number)
            status = -5;
        for (uint32_t index = 0U;
             status == 0 && index < sizeof(inode.bytes); ++index)
            if (verify_inode.bytes[index] != inode.bytes[index]) status = -5;
        char verify_target[X86OS_VFS_SYMLINK_TARGET_CAPACITY];
        uint32_t verify_length = 0U;
        if (status == 0 && mode == EXT2_S_IFLNK)
            status = ext2_read_symlink_inode(
                &volume, &verify_inode, verify_target, &verify_length);
        if (status == 0 && mode == EXT2_S_IFLNK &&
            verify_length != target_length) status = -5;
        for (uint32_t index = 0U;
             status == 0 && mode == EXT2_S_IFLNK &&
             index < target_length; ++index)
            if (verify_target[index] != target[index]) status = -5;
    }
    if (status != 0) return status;
    return ext2_namespace_commit(request, &volume, &journal, sequence);
}

static int ext2_bridge_drive_info(void *opaque, uint32_t resource,
                                  x86os_drive_info_t *info) {
    ext2_request_t *request = (ext2_request_t *)opaque;
    int status = ext2_request_deadline(request);
    if (status != 0) return status;
    return request->drive_info(request->context, resource, info);
}

static int ext2_bridge_read_sector(void *opaque, uint32_t resource,
        uint32_t sector, uint8_t data[X86OS_STORAGE_BLOCK_SIZE]) {
    ext2_request_t *request = (ext2_request_t *)opaque;
    int status = ext2_request_deadline(request);
    if (status != 0) return status;
    if (request->reads >= request->read_limit) return -110;
    ++request->reads;
    return request->read_sector(request->context, resource, sector, data);
}

int reist_vfs_shadow_ext2_recover_path(
        const reist_vfs_shadow_ext2_io_t *io, const char *absolute_path,
        uint32_t path_length, uint64_t deadline_ms) {
    ext2_request_t request;
    int status = ext2_request_from_extended(
        &request, io, deadline_ms,
        REIST_VFS_SHADOW_EXT2_MAX_TRANSACTION_READS);
    if (status != 0) return status;
    ext2_shadow_volume_t volume;
    return ext2_recover_path_request(
        &request, absolute_path, path_length, 0U, &volume, 0);
}

int reist_vfs_shadow_ext2_recover_object(
        const reist_vfs_shadow_ext2_io_t *io, uint32_t resource,
        uint64_t deadline_ms) {
    ext2_request_t request;
    int status = ext2_request_from_extended(
        &request, io, deadline_ms,
        REIST_VFS_SHADOW_EXT2_MAX_TRANSACTION_READS);
    if (status != 0) return status;
    ext2_shadow_volume_t volume;
    status = ext2_volume_for_resource(&request, resource, &volume);
    return status == 0
        ? ext2_journal_recover(&request, &volume, 0U, 0) : status;
}

int reist_vfs_shadow_ext2_stat_bounded(
        const reist_vfs_shadow_ext2_io_t *io, const char *absolute_path,
        uint32_t path_length, uint32_t resolve_flags, uint64_t deadline_ms,
        x86os_file_info_t *info) {
    if (info == 0) return -22;
    ext2_zero(info, sizeof(*info));
    ext2_request_t request;
    int status = ext2_request_from_extended(
        &request, io, deadline_ms,
        REIST_VFS_SHADOW_EXT2_MAX_TRANSACTION_READS);
    ext2_shadow_volume_t volume;
    if (status == 0)
        status = ext2_recover_path_request(
            &request, absolute_path, path_length, 0U, &volume, 0);
    ext2_shadow_inode_t inode;
    char visible[256];
    if (status == 0)
        status = ext2_resolve(
            &request, absolute_path, path_length, resolve_flags,
            &volume, &inode, visible, 0);
    if (status == 0) ext2_inode_info(&inode, visible, info);
    return status;
}

int reist_vfs_shadow_ext2_read_bounded(
        const reist_vfs_shadow_ext2_io_t *io, const char *absolute_path,
        uint32_t path_length, uint32_t offset, uint8_t *data,
        uint32_t capacity, uint64_t deadline_ms, uint32_t *transferred) {
    if (data == 0 || transferred == 0 || capacity == 0U ||
        capacity > X86OS_VFS_SHADOW_READ_CAPACITY) return -22;
    ext2_zero(data, capacity);
    *transferred = 0U;
    ext2_request_t request;
    int status = ext2_request_from_extended(
        &request, io, deadline_ms,
        REIST_VFS_SHADOW_EXT2_MAX_TRANSACTION_READS);
    ext2_shadow_volume_t volume;
    if (status == 0)
        status = ext2_recover_path_request(
            &request, absolute_path, path_length, 0U, &volume, 0);
    ext2_shadow_inode_t inode;
    char visible[256];
    if (status == 0)
        status = ext2_resolve(
            &request, absolute_path, path_length, 0U,
            &volume, &inode, visible, 0);
    if (status == 0)
        status = ext2_read_file(
            &volume, &inode, offset, data, capacity, transferred);
    if (status != 0) {
        ext2_zero(data, capacity);
        *transferred = 0U;
    }
    return status;
}

int reist_vfs_shadow_ext2_readdir_bounded(
        const reist_vfs_shadow_ext2_io_t *io, const char *absolute_path,
        uint32_t path_length, uint32_t index,
        reist_vfs_shadow_ext2_readdir_cursor_t *cursor,
        uint64_t deadline_ms, x86os_file_info_t *info) {
    if (cursor == 0 || info == 0) return -22;
    ext2_request_t request;
    int status = ext2_request_from_extended(
        &request, io, deadline_ms,
        REIST_VFS_SHADOW_EXT2_MAX_TRANSACTION_READS);
    ext2_shadow_volume_t volume;
    if (status == 0)
        status = ext2_recover_path_request(
            &request, absolute_path, path_length, 0U, &volume, 0);
    if (status != 0) {
        ext2_readdir_cursor_reset(cursor);
        ext2_zero(info, sizeof(*info));
        return status;
    }
    const reist_vfs_shadow_io_t bridge = {
        .context = &request,
        .drive_info = ext2_bridge_drive_info,
        .read_sector = ext2_bridge_read_sector,
    };
    return reist_vfs_shadow_ext2_readdir_continue(
        &bridge, absolute_path, path_length, index, cursor, info);
}

int reist_vfs_shadow_ext2_object_open_bounded(
        const reist_vfs_shadow_ext2_io_t *io, const char *absolute_path,
        uint32_t path_length, uint32_t open_flags, uint64_t deadline_ms,
        reist_vfs_shadow_object_t *object, x86os_file_info_t *info) {
    if (object == 0 || info == 0 ||
        (open_flags & ~X86OS_O_NOFOLLOW) != 0U) return -22;
    ext2_zero(object, sizeof(*object));
    ext2_zero(info, sizeof(*info));
    ext2_request_t request;
    int status = ext2_request_from_extended(
        &request, io, deadline_ms,
        REIST_VFS_SHADOW_EXT2_MAX_TRANSACTION_READS);
    ext2_shadow_volume_t volume;
    if (status == 0)
        status = ext2_recover_path_request(
            &request, absolute_path, path_length, 0U, &volume, 0);
    ext2_shadow_inode_t inode;
    char visible[256];
    uint32_t inode_number = 0U;
    uint32_t resolve_flags = (open_flags & X86OS_O_NOFOLLOW) != 0U
        ? REIST_VFS_SHADOW_EXT2_NOFOLLOW_FINAL : 0U;
    if (status == 0)
        status = ext2_resolve(
            &request, absolute_path, path_length, resolve_flags,
            &volume, &inode, visible, &inode_number);
    uint16_t mode = status == 0
        ? ext2_get16(inode.bytes + 0U) & EXT2_S_IFMT : 0U;
    if (status == 0 && mode == EXT2_S_IFLNK &&
        (open_flags & X86OS_O_NOFOLLOW) != 0U) status = -40;
    if (status == 0 && mode != EXT2_S_IFREG) status = -21;
    if (status != 0) return status;
    *object = (reist_vfs_shadow_object_t){
        .version = REIST_VFS_SHADOW_OBJECT_VERSION,
        .struct_size = sizeof(*object),
        .filesystem = REIST_VFS_SHADOW_OBJECT_EXT2,
        .resource = volume.resource,
        .volume_signature = volume.signature,
        .locator_a = inode_number,
        .locator_b = 0U,
        .locator_c = 0U,
        .object_generation = ext2_get32(inode.bytes + 100U),
    };
    ext2_inode_info(&inode, visible, info);
    return 0;
}

int reist_vfs_shadow_ext2_object_stat_bounded(
        const reist_vfs_shadow_ext2_io_t *io,
        const reist_vfs_shadow_object_t *object, uint64_t deadline_ms,
        x86os_file_info_t *info) {
    if (object == 0 || info == 0) return -22;
    ext2_zero(info, sizeof(*info));
    ext2_request_t request;
    int status = ext2_request_from_extended(
        &request, io, deadline_ms,
        REIST_VFS_SHADOW_EXT2_MAX_TRANSACTION_READS);
    ext2_shadow_volume_t volume;
    if (status == 0)
        status = ext2_volume_for_resource(
            &request, object->resource, &volume);
    if (status == 0)
        status = ext2_journal_recover(&request, &volume, 0U, 0);
    ext2_shadow_inode_t inode;
    if (status == 0)
        status = ext2_object_inode(&request, object, &volume, &inode);
    if (status == 0) ext2_inode_info(&inode, "", info);
    return status;
}

int reist_vfs_shadow_ext2_object_read_bounded(
        const reist_vfs_shadow_ext2_io_t *io,
        const reist_vfs_shadow_object_t *object, uint32_t offset,
        uint8_t *data, uint32_t capacity, uint64_t deadline_ms,
        uint32_t *transferred) {
    if (object == 0 || data == 0 || transferred == 0 || capacity == 0U ||
        capacity > X86OS_STORAGE_BULK_MAX_BYTES) return -22;
    ext2_zero(data, capacity);
    *transferred = 0U;
    ext2_request_t request;
    int status = ext2_request_from_extended(
        &request, io, deadline_ms,
        REIST_VFS_SHADOW_EXT2_MAX_TRANSACTION_READS);
    ext2_shadow_volume_t volume;
    if (status == 0)
        status = ext2_volume_for_resource(
            &request, object->resource, &volume);
    if (status == 0)
        status = ext2_journal_recover(&request, &volume, 0U, 0);
    ext2_shadow_inode_t inode;
    if (status == 0)
        status = ext2_object_inode(&request, object, &volume, &inode);
    if (status == 0)
        status = ext2_read_file(
            &volume, &inode, offset, data, capacity, transferred);
    if (status != 0) {
        ext2_zero(data, capacity);
        *transferred = 0U;
    }
    return status;
}

int reist_vfs_shadow_ext2_readlink(
        const reist_vfs_shadow_ext2_io_t *io, const char *absolute_path,
        uint32_t path_length,
        char target[X86OS_VFS_SYMLINK_TARGET_CAPACITY],
        uint32_t *target_length, uint64_t deadline_ms) {
    if (target == 0 || target_length == 0) return -22;
    ext2_zero(target, X86OS_VFS_SYMLINK_TARGET_CAPACITY);
    *target_length = 0U;
    ext2_request_t request;
    int status = ext2_request_from_extended(
        &request, io, deadline_ms,
        REIST_VFS_SHADOW_EXT2_MAX_TRANSACTION_READS);
    ext2_shadow_volume_t volume;
    if (status == 0)
        status = ext2_recover_path_request(
            &request, absolute_path, path_length, 0U, &volume, 0);
    ext2_shadow_inode_t inode;
    char visible[256];
    if (status == 0)
        status = ext2_resolve(
            &request, absolute_path, path_length,
            REIST_VFS_SHADOW_EXT2_NOFOLLOW_FINAL,
            &volume, &inode, visible, 0);
    if (status == 0 &&
        (ext2_get16(inode.bytes + 0U) & EXT2_S_IFMT) != EXT2_S_IFLNK)
        status = -22;
    if (status == 0)
        status = ext2_read_symlink_inode(
            &volume, &inode, target, target_length);
    if (status != 0) {
        ext2_zero(target, X86OS_VFS_SYMLINK_TARGET_CAPACITY);
        *target_length = 0U;
    }
    return status;
}

int reist_vfs_shadow_ext2_symlink(
        const reist_vfs_shadow_ext2_io_t *io, const char *target,
        uint32_t target_length, const char *absolute_link_path,
        uint32_t link_path_length, uint64_t deadline_ms) {
    ext2_request_t request;
    int status = ext2_request_from_extended(
        &request, io, deadline_ms,
        REIST_VFS_SHADOW_EXT2_MAX_TRANSACTION_READS);
    return status == 0
        ? ext2_symlink_request(
            &request, target, target_length,
            absolute_link_path, link_path_length)
        : status;
}

int reist_vfs_shadow_ext2_unlink_symlink(
        const reist_vfs_shadow_ext2_io_t *io, const char *absolute_path,
        uint32_t path_length, uint64_t deadline_ms) {
    ext2_request_t request;
    int status = ext2_request_from_extended(
        &request, io, deadline_ms,
        REIST_VFS_SHADOW_EXT2_MAX_TRANSACTION_READS);
    return status == 0
        ? ext2_unlink_request(
            &request, absolute_path, path_length, 1U)
        : status;
}

int reist_vfs_shadow_ext2_unlink(
        const reist_vfs_shadow_ext2_io_t *io, const char *absolute_path,
        uint32_t path_length, uint64_t deadline_ms) {
    ext2_request_t request;
    int status = ext2_request_from_extended(
        &request, io, deadline_ms,
        REIST_VFS_SHADOW_EXT2_MAX_TRANSACTION_READS);
    return status == 0
        ? ext2_unlink_request(
            &request, absolute_path, path_length, 0U)
        : status;
}

int reist_vfs_shadow_ext2_rename_symlink(
        const reist_vfs_shadow_ext2_io_t *io, const char *source_path,
        uint32_t source_length, const char *destination_path,
        uint32_t destination_length, uint64_t deadline_ms) {
    ext2_request_t request;
    int status = ext2_request_from_extended(
        &request, io, deadline_ms,
        REIST_VFS_SHADOW_EXT2_MAX_TRANSACTION_READS);
    return status == 0
        ? ext2_rename_request(
            &request, source_path, source_length,
            destination_path, destination_length, 1U)
        : status;
}

int reist_vfs_shadow_ext2_rename(
        const reist_vfs_shadow_ext2_io_t *io, const char *source_path,
        uint32_t source_length, const char *destination_path,
        uint32_t destination_length, uint64_t deadline_ms) {
    ext2_request_t request;
    int status = ext2_request_from_extended(
        &request, io, deadline_ms,
        REIST_VFS_SHADOW_EXT2_MAX_TRANSACTION_READS);
    return status == 0
        ? ext2_rename_request(
            &request, source_path, source_length,
            destination_path, destination_length, 0U)
        : status;
}
