/**
 * @file userspace/storage/lib/vfs_shadow_ext2.c
 * @brief Independent bounded Linux EXT2 stat parser for Ring 3.
 *
 * The supported read-only subset is revision 0/1 EXT2 with 1, 2 or 4 KiB
 * blocks, linear directories and direct or single-indirect directory blocks.
 * Every read is mediated and charged to one fixed per-request budget.
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
    uint32_t group_count;
    uint32_t reads;
    uint8_t has_file_type;
    const reist_vfs_shadow_io_t *io;
} ext2_shadow_volume_t;

typedef struct {
    uint8_t bytes[EXT2_INODE_CORE_SIZE];
} ext2_shadow_inode_t;

static uint16_t ext2_get16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8U));
}

static uint32_t ext2_get32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
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
    if (volume == 0 || data == 0 || sector >= volume->sectors) return -5;
    if (volume->reads >= REIST_VFS_SHADOW_EXT2_MAX_SECTOR_READS) return -110;
    ++volume->reads;
    return volume->io->read_sector(volume->io->context, volume->resource,
                                   sector, data) == 0 ? 0 : -5;
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

static int ext2_mount(ext2_shadow_volume_t *volume,
                      const reist_vfs_shadow_io_t *io,
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
        int status = io->drive_info(io->context, resource, &candidate);
        if (status == 0) break;
        if (status < 0) return -5;
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
    volume->io = io;
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
    if (inode_size < EXT2_INODE_CORE_SIZE || inode_size > block_size ||
        (inode_size & (inode_size - 1U)) != 0U ||
        block_size % inode_size != 0U) return -2;
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
    uint32_t inode_table = ext2_get32(descriptor + 8U);
    uint32_t inode_table_bytes = volume->inodes_per_group *
        volume->inode_size;
    uint32_t inode_table_blocks = ext2_div_ceil_u32(
        inode_table_bytes, volume->block_size);
    if (inode_table < volume->first_data_block ||
        (uint64_t)inode_table + inode_table_blocks > volume->blocks_count)
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
    if ((mode != EXT2_S_IFREG && mode != EXT2_S_IFDIR) ||
        ext2_get32(inode->bytes + 20U) != 0U ||
        (mode == EXT2_S_IFREG && ext2_get32(inode->bytes + 108U) != 0U) ||
        (mode == EXT2_S_IFDIR &&
         (ext2_get32(inode->bytes + 32U) & EXT2_INDEX_FL) != 0U)) return -2;
    return 0;
}

static int ext2_data_block_valid(const ext2_shadow_volume_t *volume,
                                 uint32_t block) {
    return block >= volume->first_data_block && block < volume->blocks_count;
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
    info->type = (ext2_get16(inode->bytes + 0U) & EXT2_S_IFMT) == EXT2_S_IFDIR
        ? X86OS_DIRECTORY : X86OS_FILE;
    info->size = ext2_get32(inode->bytes + 4U);
    info->access_time = ext2_get32(inode->bytes + 8U);
    info->create_time = ext2_get32(inode->bytes + 12U);
    info->modify_time = ext2_get32(inode->bytes + 16U);
}

int reist_vfs_shadow_ext2_stat(const reist_vfs_shadow_io_t *io,
                               const char *absolute_path,
                               uint32_t path_length,
                               x86os_file_info_t *info) {
    if (io == 0 || io->drive_info == 0 || io->read_sector == 0 ||
        absolute_path == 0 || info == 0 || path_length == 0U ||
        path_length >= X86OS_VFS_SHADOW_PATH_CAPACITY ||
        absolute_path[0] != '/' || absolute_path[path_length] != '\0')
        return -22;
    for (uint32_t index = 0U; index < path_length; ++index)
        if (absolute_path[index] == '\0') return -22;
    ext2_zero(info, sizeof(*info));
    ext2_shadow_volume_t volume;
    uint32_t cursor = 0U;
    int status = ext2_mount(&volume, io, absolute_path, path_length, &cursor);
    if (status != 0) return status;
    status = ext2_parse_superblock(&volume);
    if (status != 0) return status;
    ext2_shadow_inode_t inode;
    status = ext2_read_inode(&volume, EXT2_ROOT_INODE, &inode);
    if (status != 0 ||
        (ext2_get16(inode.bytes + 0U) & EXT2_S_IFMT) != EXT2_S_IFDIR)
        return status != 0 ? status : -5;
    if (cursor >= path_length) {
        ext2_inode_info(&inode, "/", info);
        return 0;
    }
    for (uint32_t component = 0U;
         component < REIST_VFS_SHADOW_EXT2_MAX_COMPONENTS; ++component) {
        uint32_t start = cursor;
        while (cursor < path_length && absolute_path[cursor] != '/') ++cursor;
        uint32_t length = cursor - start;
        if (!ext2_component_valid(absolute_path + start, length)) return -22;
        char wanted[256];
        ext2_zero(wanted, sizeof(wanted));
        ext2_copy(wanted, absolute_path + start, length);
        uint32_t found_inode = 0U;
        char visible[256];
        status = ext2_find_entry(&volume, &inode, wanted, &found_inode,
                                 visible);
        if (status != 0) return status;
        status = ext2_read_inode(&volume, found_inode, &inode);
        if (status != 0) return status;
        while (cursor < path_length && absolute_path[cursor] == '/') ++cursor;
        if (cursor >= path_length) {
            ext2_inode_info(&inode, visible, info);
            return 0;
        }
        if ((ext2_get16(inode.bytes + 0U) & EXT2_S_IFMT) != EXT2_S_IFDIR)
            return -2;
    }
    return -110;
}
