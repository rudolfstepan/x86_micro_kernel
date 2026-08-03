#include "ext2.h"
#include "lib/libc/stdio.h"
#include "lib/libc/string.h"
#include "lib/libc/stdlib.h"
#include "drivers/block/ata.h"

typedef struct __attribute__((packed)) {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t name_len;
    uint8_t file_type;
    char name[];
} ext2_raw_dir_entry_t;

static void ext2_copy_bytes(void* destination, const void* source,
                            uint32_t length) {
    uint8_t* dst = (uint8_t*)destination;
    const uint8_t* src = (const uint8_t*)source;
    for (uint32_t i = 0; i < length; i++) dst[i] = src[i];
}

static void ext2_zero_bytes(void* destination, uint32_t length) {
    uint8_t* dst = (uint8_t*)destination;
    for (uint32_t i = 0; i < length; i++) dst[i] = 0;
}

static uint32_t ext2_inode_size(const ext2_fs_t* fs) {
    return fs->superblock.s_inode_size ? fs->superblock.s_inode_size : 128;
}

static bool ext2_valid_block(const ext2_fs_t* fs, uint32_t block) {
    return block != 0 && block < fs->superblock.s_blocks_count;
}

bool ext2_init(ext2_fs_t* fs, uint16_t base, bool is_master) {
    return ext2_init_volume(fs, base, is_master, 0, 0);
}

bool ext2_init_at(ext2_fs_t* fs, uint16_t base, bool is_master,
                  uint32_t partition_lba) {
    return ext2_init_volume(fs, base, is_master, partition_lba, 0);
}

bool ext2_init_volume(ext2_fs_t* fs, uint16_t base, bool is_master,
                      uint32_t partition_lba, uint32_t volume_sectors) {
    if (!fs) return false;
    memset(fs, 0, sizeof(*fs));
    fs->ata_base = base;
    fs->ata_is_master = is_master;
    fs->partition_lba = partition_lba;
    fs->volume_sectors = volume_sectors;

    uint8_t superblock_buffer[1024];
    if (partition_lba > UINT32_MAX - 3 ||
        !ata_read_sector(base, partition_lba + 2, superblock_buffer,
                         is_master) ||
        !ata_read_sector(base, partition_lba + 3,
                         superblock_buffer + 512, is_master)) {
        return false;
    }
    ext2_copy_bytes(&fs->superblock, superblock_buffer,
                    sizeof(fs->superblock));

    if (fs->superblock.s_magic != EXT2_SIGNATURE ||
        fs->superblock.s_log_block_size > 6 ||
        fs->superblock.s_blocks_count == 0 ||
        fs->superblock.s_inodes_count == 0 ||
        fs->superblock.s_blocks_per_group == 0 ||
        fs->superblock.s_inodes_per_group == 0 ||
        fs->superblock.s_first_data_block >=
            fs->superblock.s_blocks_count) {
        return false;
    }

    /* The directory file-type extension is the only incompat feature used
       by ordinary EXT2 images that this reader understands. */
    if (fs->superblock.s_feature_incompat & ~0x00000002u) return false;

    fs->block_size = 1024u << fs->superblock.s_log_block_size;
    if (fs->block_size < 1024 || (fs->block_size % 512) != 0) return false;
    if (fs->superblock.s_first_data_block !=
        (fs->block_size == 1024 ? 1u : 0u) ||
        fs->superblock.s_blocks_per_group > fs->block_size * 8u ||
        fs->superblock.s_inodes_per_group > fs->block_size * 8u) {
        return false;
    }
    uint64_t required_sectors = (uint64_t)fs->superblock.s_blocks_count *
                                (fs->block_size / 512u);
    if ((volume_sectors != 0 && required_sectors > volume_sectors) ||
        (uint64_t)partition_lba + required_sectors >
            (uint64_t)UINT32_MAX + 1u) {
        return false;
    }

    uint32_t inode_size = ext2_inode_size(fs);
    if (inode_size < sizeof(ext2_inode_t) || inode_size > fs->block_size ||
        (inode_size & (inode_size - 1)) != 0 ||
        (fs->block_size % inode_size) != 0) {
        return false;
    }
    fs->inodes_per_block = fs->block_size / inode_size;

    uint64_t data_blocks = (uint64_t)fs->superblock.s_blocks_count -
                           fs->superblock.s_first_data_block;
    uint32_t block_groups = (uint32_t)((data_blocks +
        fs->superblock.s_blocks_per_group - 1) /
        fs->superblock.s_blocks_per_group);
    uint32_t inode_groups = (uint32_t)(((uint64_t)
        fs->superblock.s_inodes_count + fs->superblock.s_inodes_per_group - 1) /
        fs->superblock.s_inodes_per_group);
    if (block_groups != inode_groups) return false;
    fs->num_block_groups = block_groups;
    if (fs->num_block_groups == 0 ||
        fs->num_block_groups > UINT32_MAX / sizeof(ext2_group_desc_t)) {
        return false;
    }

    uint32_t gdt_size = fs->num_block_groups * sizeof(ext2_group_desc_t);
    fs->group_desc_table = (ext2_group_desc_t*)malloc(gdt_size);
    fs->block_buffer = malloc(fs->block_size);
    if (!fs->group_desc_table || !fs->block_buffer) goto fail;

    uint32_t gdt_block = fs->superblock.s_first_data_block + 1;
    uint32_t copied = 0;
    while (copied < gdt_size) {
        uint32_t block_index = copied / fs->block_size;
        if (gdt_block > UINT32_MAX - block_index ||
            !ext2_read_block(fs, gdt_block + block_index,
                             fs->block_buffer)) {
            goto fail;
        }
        uint32_t amount = gdt_size - copied;
        if (amount > fs->block_size) amount = fs->block_size;
        ext2_copy_bytes((uint8_t*)fs->group_desc_table + copied,
                        fs->block_buffer, amount);
        copied += amount;
    }

    for (uint32_t i = 0; i < fs->num_block_groups; i++) {
        uint32_t table = fs->group_desc_table[i].bg_inode_table;
        if (!ext2_valid_block(fs, table)) goto fail;
    }

    fs->current_dir_inode = EXT2_ROOT_INO;
    return true;

fail:
    ext2_cleanup(fs);
    return false;
}

void ext2_cleanup(ext2_fs_t* fs) {
    if (!fs) return;
    if (fs->group_desc_table) free(fs->group_desc_table);
    if (fs->block_buffer) free(fs->block_buffer);
    fs->group_desc_table = NULL;
    fs->block_buffer = NULL;
}

uint32_t ext2_get_block_size(ext2_fs_t* fs) {
    return fs ? fs->block_size : 0;
}

bool ext2_read_block(ext2_fs_t* fs, uint32_t block_num, void* buffer) {
    if (!fs || !buffer || fs->block_size == 0 ||
        block_num >= fs->superblock.s_blocks_count) {
        return false;
    }
    uint32_t sectors_per_block = fs->block_size / 512;
    uint64_t first = (uint64_t)fs->partition_lba +
                     (uint64_t)block_num * sectors_per_block;
    uint64_t relative_end = (uint64_t)block_num * sectors_per_block +
                            sectors_per_block;
    if (first > UINT32_MAX ||
        first + sectors_per_block - 1 > UINT32_MAX ||
        (fs->volume_sectors != 0 && relative_end > fs->volume_sectors)) {
        return false;
    }
    for (uint32_t i = 0; i < sectors_per_block; i++) {
        if (!ata_read_sector(fs->ata_base, (uint32_t)first + i,
                             (uint8_t*)buffer + i * 512,
                             fs->ata_is_master)) {
            return false;
        }
    }
    return true;
}

bool ext2_write_block(ext2_fs_t* fs, uint32_t block_num,
                      const void* buffer) {
    if (!fs || !buffer || fs->block_size == 0 ||
        block_num >= fs->superblock.s_blocks_count) {
        return false;
    }
    uint32_t sectors_per_block = fs->block_size / 512;
    uint64_t first = (uint64_t)fs->partition_lba +
                     (uint64_t)block_num * sectors_per_block;
    uint64_t relative_end = (uint64_t)block_num * sectors_per_block +
                            sectors_per_block;
    if (first > UINT32_MAX ||
        first + sectors_per_block - 1 > UINT32_MAX ||
        (fs->volume_sectors != 0 && relative_end > fs->volume_sectors)) {
        return false;
    }
    for (uint32_t i = 0; i < sectors_per_block; i++) {
        if (!ata_write_sector(fs->ata_base, (uint32_t)first + i,
                              (void*)((const uint8_t*)buffer + i * 512),
                              fs->ata_is_master)) {
            return false;
        }
    }
    return true;
}

uint32_t ext2_get_inode_block_group(ext2_fs_t* fs, uint32_t inode_num) {
    if (!fs || inode_num == 0 || fs->superblock.s_inodes_per_group == 0)
        return UINT32_MAX;
    return (inode_num - 1) / fs->superblock.s_inodes_per_group;
}

uint32_t ext2_get_inode_table_index(ext2_fs_t* fs, uint32_t inode_num) {
    if (!fs || inode_num == 0 || fs->superblock.s_inodes_per_group == 0)
        return UINT32_MAX;
    return (inode_num - 1) % fs->superblock.s_inodes_per_group;
}

bool ext2_read_inode(ext2_fs_t* fs, uint32_t inode_num,
                     ext2_inode_t* inode) {
    if (!fs || !inode || inode_num == 0 ||
        inode_num > fs->superblock.s_inodes_count ||
        !fs->group_desc_table || !fs->block_buffer) {
        return false;
    }
    uint32_t group = ext2_get_inode_block_group(fs, inode_num);
    uint32_t index = ext2_get_inode_table_index(fs, inode_num);
    if (group >= fs->num_block_groups) return false;

    uint32_t inode_size = ext2_inode_size(fs);
    uint64_t byte_offset = (uint64_t)index * inode_size;
    uint32_t block_offset = (uint32_t)(byte_offset / fs->block_size);
    uint32_t offset_in_block = (uint32_t)(byte_offset % fs->block_size);
    uint32_t table = fs->group_desc_table[group].bg_inode_table;
    if (table > UINT32_MAX - block_offset ||
        offset_in_block + sizeof(*inode) > fs->block_size ||
        !ext2_read_block(fs, table + block_offset, fs->block_buffer)) {
        return false;
    }
    ext2_copy_bytes(inode, (uint8_t*)fs->block_buffer + offset_in_block,
                    sizeof(*inode));
    if ((inode->i_mode & EXT2_S_IFMT) == EXT2_S_IFREG &&
        inode->i_dir_acl != 0) {
        return false;
    }
    return true;
}

bool ext2_write_inode(ext2_fs_t* fs, uint32_t inode_num,
                      const ext2_inode_t* inode) {
    if (!fs || !inode || inode_num == 0 ||
        inode_num > fs->superblock.s_inodes_count ||
        !fs->group_desc_table || !fs->block_buffer) {
        return false;
    }
    uint32_t group = ext2_get_inode_block_group(fs, inode_num);
    uint32_t index = ext2_get_inode_table_index(fs, inode_num);
    if (group >= fs->num_block_groups) return false;

    uint32_t inode_size = ext2_inode_size(fs);
    uint64_t byte_offset = (uint64_t)index * inode_size;
    uint32_t block_offset = (uint32_t)(byte_offset / fs->block_size);
    uint32_t offset_in_block = (uint32_t)(byte_offset % fs->block_size);
    uint32_t table = fs->group_desc_table[group].bg_inode_table;
    if (table > UINT32_MAX - block_offset ||
        offset_in_block + sizeof(*inode) > fs->block_size ||
        !ext2_read_block(fs, table + block_offset, fs->block_buffer)) {
        return false;
    }
    ext2_copy_bytes((uint8_t*)fs->block_buffer + offset_in_block, inode,
                    sizeof(*inode));
    return ext2_write_block(fs, table + block_offset, fs->block_buffer);
}

static bool ext2_read_pointer(ext2_fs_t* fs, uint32_t pointer_block,
                              uint32_t index, uint32_t* result) {
    uint32_t pointers_per_block = fs->block_size / sizeof(uint32_t);
    if (!result || index >= pointers_per_block) return false;
    if (pointer_block == 0) {
        *result = 0;
        return true;
    }
    if (!ext2_valid_block(fs, pointer_block) ||
        !ext2_read_block(fs, pointer_block, fs->block_buffer)) {
        return false;
    }
    *result = ((uint32_t*)fs->block_buffer)[index];
    return *result == 0 || ext2_valid_block(fs, *result);
}

static bool ext2_inode_data_block(ext2_fs_t* fs, const ext2_inode_t* inode,
                                  uint32_t logical, uint32_t* result) {
    if (!fs || !inode || !result) return false;
    if (logical < EXT2_NDIR_BLOCKS) {
        *result = inode->i_block[logical];
        return *result == 0 || ext2_valid_block(fs, *result);
    }

    uint64_t per_block = fs->block_size / sizeof(uint32_t);
    uint64_t index = logical - EXT2_NDIR_BLOCKS;
    if (index < per_block) {
        return ext2_read_pointer(fs, inode->i_block[EXT2_IND_BLOCK],
                                 (uint32_t)index, result);
    }

    index -= per_block;
    uint64_t double_capacity = per_block * per_block;
    if (index < double_capacity) {
        uint32_t indirect;
        if (!ext2_read_pointer(fs, inode->i_block[EXT2_DIND_BLOCK],
                               (uint32_t)(index / per_block), &indirect)) {
            return false;
        }
        return ext2_read_pointer(fs, indirect,
                                 (uint32_t)(index % per_block), result);
    }

    index -= double_capacity;
    uint64_t triple_capacity = double_capacity * per_block;
    if (index >= triple_capacity) return false;
    uint32_t double_indirect;
    uint32_t indirect;
    if (!ext2_read_pointer(fs, inode->i_block[EXT2_TIND_BLOCK],
                           (uint32_t)(index / double_capacity),
                           &double_indirect)) {
        return false;
    }
    uint64_t within_double = index % double_capacity;
    if (!ext2_read_pointer(fs, double_indirect,
                           (uint32_t)(within_double / per_block),
                           &indirect)) {
        return false;
    }
    return ext2_read_pointer(fs, indirect,
                             (uint32_t)(within_double % per_block), result);
}

int ext2_read_file(ext2_fs_t* fs, ext2_inode_t* inode, uint32_t offset,
                   uint32_t size, void* buffer) {
    if (!fs || !inode || (!buffer && size != 0) || !fs->block_buffer)
        return -1;
    if (offset >= inode->i_size || size == 0) return 0;
    uint32_t remaining = inode->i_size - offset;
    if (size > remaining) size = remaining;

    if ((inode->i_mode & EXT2_S_IFMT) == EXT2_S_IFLNK &&
        inode->i_size <= sizeof(inode->i_block)) {
        ext2_copy_bytes(buffer, (const uint8_t*)inode->i_block + offset, size);
        return (int)size;
    }

    uint32_t bytes_read = 0;
    while (bytes_read < size) {
        uint32_t current = offset + bytes_read;
        uint32_t logical = current / fs->block_size;
        uint32_t in_block = current % fs->block_size;
        uint32_t amount = fs->block_size - in_block;
        if (amount > size - bytes_read) amount = size - bytes_read;

        uint32_t physical;
        if (!ext2_inode_data_block(fs, inode, logical, &physical)) return -1;
        if (physical == 0) {
            ext2_zero_bytes((uint8_t*)buffer + bytes_read, amount);
        } else {
            if (!ext2_read_block(fs, physical, fs->block_buffer)) return -1;
            ext2_copy_bytes((uint8_t*)buffer + bytes_read,
                            (uint8_t*)fs->block_buffer + in_block, amount);
        }
        bytes_read += amount;
    }
    return (int)bytes_read;
}

typedef bool (*ext2_dir_visitor_t)(const ext2_dir_entry_t* entry,
                                   void* context);

static int ext2_walk_dir(ext2_fs_t* fs, uint32_t inode_num,
                         ext2_dir_visitor_t visitor, void* context) {
    ext2_inode_t inode;
    if (!fs || !visitor || !ext2_read_inode(fs, inode_num, &inode) ||
        (inode.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR) {
        return -1;
    }

    uint32_t logical_blocks = inode.i_size == 0 ? 0 :
        ((inode.i_size - 1) / fs->block_size) + 1;
    for (uint32_t logical = 0; logical < logical_blocks; logical++) {
        uint32_t physical;
        if (!ext2_inode_data_block(fs, &inode, logical, &physical)) return -1;
        if (physical == 0) continue;
        if (!ext2_read_block(fs, physical, fs->block_buffer)) return -1;

        uint32_t valid = inode.i_size - logical * fs->block_size;
        if (valid > fs->block_size) valid = fs->block_size;
        uint32_t position = 0;
        while (position < valid) {
            if (valid - position < 8) return -1;
            ext2_raw_dir_entry_t* raw = (ext2_raw_dir_entry_t*)
                ((uint8_t*)fs->block_buffer + position);
            if (raw->rec_len < 8 || (raw->rec_len & 3) != 0 ||
                raw->rec_len > valid - position ||
                raw->name_len > raw->rec_len - 8) {
                return -1;
            }

            if (raw->inode != 0) {
                if (raw->inode > fs->superblock.s_inodes_count) return -1;
                ext2_dir_entry_t entry;
                memset(&entry, 0, sizeof(entry));
                entry.inode = raw->inode;
                entry.rec_len = raw->rec_len;
                entry.name_len = raw->name_len;
                entry.file_type = raw->file_type;
                ext2_copy_bytes(entry.name, raw->name, raw->name_len);
                entry.name[raw->name_len] = '\0';
                if (visitor(&entry, context)) return 1;
            }
            position += raw->rec_len;
        }
    }
    return 0;
}

typedef struct {
    ext2_dir_entry_t* entries;
    uint32_t maximum;
    uint32_t count;
} ext2_collect_context_t;

static bool ext2_collect_entry(const ext2_dir_entry_t* entry, void* opaque) {
    ext2_collect_context_t* context = (ext2_collect_context_t*)opaque;
    if (context->count >= context->maximum) return true;
    context->entries[context->count++] = *entry;
    return context->count >= context->maximum;
}

bool ext2_read_dir(ext2_fs_t* fs, uint32_t inode_num,
                   ext2_dir_entry_t* entries, uint32_t max_entries,
                   uint32_t* count) {
    if (!fs || !count || (!entries && max_entries != 0)) return false;
    *count = 0;
    if (max_entries == 0) return true;
    ext2_collect_context_t context = { entries, max_entries, 0 };
    int result = ext2_walk_dir(fs, inode_num, ext2_collect_entry, &context);
    *count = context.count;
    return result >= 0;
}

typedef struct {
    const char* name;
    uint32_t length;
    ext2_dir_entry_t* result;
} ext2_find_context_t;

static bool ext2_find_visitor(const ext2_dir_entry_t* entry, void* opaque) {
    ext2_find_context_t* context = (ext2_find_context_t*)opaque;
    if (entry->name_len != context->length ||
        memcmp(entry->name, context->name, context->length) != 0) {
        return false;
    }
    *context->result = *entry;
    return true;
}

int ext2_find_entry_status(ext2_fs_t* fs, uint32_t dir_inode,
                           const char* name, ext2_dir_entry_t* entry) {
    if (!fs || !name || !entry) return -1;
    uint32_t length = strlen(name);
    if (length == 0 || length > EXT2_NAME_LEN) return 0;
    ext2_find_context_t context = { name, length, entry };
    return ext2_walk_dir(fs, dir_inode, ext2_find_visitor, &context);
}

bool ext2_find_entry(ext2_fs_t* fs, uint32_t dir_inode, const char* name,
                     ext2_dir_entry_t* entry) {
    return ext2_find_entry_status(fs, dir_inode, name, entry) == 1;
}

typedef struct {
    uint32_t target;
    uint32_t current;
    ext2_dir_entry_t* result;
} ext2_index_context_t;

static bool ext2_index_visitor(const ext2_dir_entry_t* entry, void* opaque) {
    ext2_index_context_t* context = (ext2_index_context_t*)opaque;
    if (context->current++ != context->target) return false;
    *context->result = *entry;
    return true;
}

int ext2_get_dir_entry(ext2_fs_t* fs, uint32_t dir_inode, uint32_t index,
                       ext2_dir_entry_t* entry) {
    if (!fs || !entry) return -1;
    ext2_index_context_t context = { index, 0, entry };
    return ext2_walk_dir(fs, dir_inode, ext2_index_visitor, &context);
}
