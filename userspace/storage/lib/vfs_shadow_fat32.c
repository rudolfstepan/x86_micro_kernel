/**
 * @file userspace/storage/lib/vfs_shadow_fat32.c
 * @brief Independent bounded FAT12/FAT32/VFAT stat parser for Ring 3.
 *
 * The parser follows the Microsoft FAT on-disk terminology and determines FAT
 * type from the BPB layout and data-cluster count. It accepts the current
 * REIST ASCII pathname subset and consumes only fixed stack storage and a
 * fixed number of mediated sector reads.
 */
#include "../include/reist/vfs_shadow_fat32.h"
#include "../../../include/reist/utf.h"
#include "../../../include/reist/unicode_norm.h"

#define FAT32_ATTR_DIRECTORY 0x10U
#define FAT32_ATTR_VOLUME_ID 0x08U
#define FAT32_ATTR_LONG_NAME 0x0FU
#define FAT32_EOC_MIN 0x0FFFFFF8U
#define FAT32_BAD_CLUSTER 0x0FFFFFF7U
#define FAT32_RESERVED_MIN 0x0FFFFFF0U
#define FAT32_LFN_CHARS_PER_ENTRY 13U
#define FAT32_MAX_LFN_ENTRIES 20U
#define FAT_TYPE_ANY 0U
#define FAT_TYPE_12 12U
#define FAT_TYPE_32 32U
#define FAT12_CLUSTER_LIMIT 4085U
#define FAT32_CLUSTER_MINIMUM 65525U
#define FAT12_EOC_MIN 0x0FF8U
#define FAT12_BAD_CLUSTER 0x0FF7U
#define FAT12_RESERVED_MIN 0x0FF0U

typedef struct {
    uint32_t resource;
    uint32_t sectors;
    uint32_t reserved_sectors;
    uint32_t fat_sectors;
    uint32_t data_start;
    uint32_t cluster_count;
    uint32_t root_cluster;
    uint32_t root_dir_start;
    uint32_t root_dir_sectors;
    uint32_t active_fat;
    uint32_t reads;
    uint32_t signature;
    uint32_t fat_cache_lba;
    uint8_t sectors_per_cluster;
    uint8_t fat_count;
    uint8_t fat_type;
    uint8_t fat_cache_valid;
    uint8_t fat_cache[X86OS_STORAGE_BLOCK_SIZE];
    const reist_vfs_shadow_io_t *io;
} shadow_volume_t;

typedef struct {
    uint8_t bytes[32];
} shadow_dir_entry_t;

typedef struct {
    uint32_t anchor;
    uint32_t power;
    uint32_t distance;
    uint32_t visited;
} shadow_file_chain_guard_t;

typedef struct {
    char name[256];
    uint16_t units[FAT32_MAX_LFN_ENTRIES * FAT32_LFN_CHARS_PER_ENTRY];
    uint8_t checksum;
    uint8_t expected_order;
    uint8_t slot_count;
    uint8_t active;
} shadow_lfn_t;

static uint16_t shadow_get16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8U));
}

static uint32_t shadow_get32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static uint32_t shadow_signature(const uint8_t *data, uint32_t length) {
    uint32_t signature = 2166136261U;
    for (uint32_t index = 0U; index < length; ++index)
        signature = (signature ^ data[index]) * 16777619U;
    return signature != 0U ? signature : 1U;
}

static uint32_t shadow_entry_generation(const uint8_t bytes[32]) {
    uint8_t identity[12];
    for (uint32_t index = 0U; index < 8U; ++index)
        identity[index] = bytes[13U + index];
    identity[8U] = bytes[20U];
    identity[9U] = bytes[21U];
    identity[10U] = bytes[26U];
    identity[11U] = bytes[27U];
    return shadow_signature(identity, sizeof(identity));
}

static void shadow_zero(void *target, uint32_t length) {
    uint8_t *bytes = (uint8_t *)target;
    for (uint32_t index = 0U; index < length; ++index) bytes[index] = 0U;
}

static void shadow_copy(void *target, const void *source, uint32_t length) {
    uint8_t *out = (uint8_t *)target;
    const uint8_t *in = (const uint8_t *)source;
    for (uint32_t index = 0U; index < length; ++index) out[index] = in[index];
}

static uint32_t shadow_text_length(const char *text, uint32_t capacity) {
    uint32_t length = 0U;
    if (text == 0) return capacity;
    while (length < capacity && text[length] != '\0') ++length;
    return length;
}

static char shadow_lower(char value) {
    return value >= 'A' && value <= 'Z'
        ? (char)(value + ('a' - 'A')) : value;
}

static int shadow_name_equal(const char *left, const char *right) {
    return reist_unicode_caseless_nfc_equal(left, right);
}

static int shadow_path_prefix(const char *path, uint32_t path_length,
                              const char *mount, uint32_t mount_length) {
    if (mount_length == 0U || mount_length > path_length || mount[0] != '/')
        return 0;
    for (uint32_t index = 0U; index < mount_length; ++index)
        if (path[index] != mount[index]) return 0;
    if (mount_length == 1U) return 1;
    return mount_length == path_length || path[mount_length] == '/';
}

static int shadow_read(shadow_volume_t *volume, uint32_t sector,
                       uint8_t data[X86OS_STORAGE_BLOCK_SIZE]) {
    if (volume == 0 || data == 0 || sector >= volume->sectors) return -5;
    if (volume->reads >= REIST_VFS_SHADOW_MAX_SECTOR_READS) return -110;
    ++volume->reads;
    return volume->io->read_sector(volume->io->context, volume->resource,
                                   sector, data) == 0 ? 0 : -5;
}

static int shadow_valid_component(const char *name, uint32_t length) {
    if (length == 0U || length > 255U ||
        (length == 1U && name[0] == '.') ||
        (length == 2U && name[0] == '.' && name[1] == '.')) return 0;
    uint16_t units[255];
    size_t unit_count = 0U;
    if (!reist_utf8_to_utf16(name, length, units, 255U, &unit_count) ||
        unit_count == 0U || units[unit_count - 1U] == (uint16_t)' ' ||
        units[unit_count - 1U] == (uint16_t)'.') return 0;
    for (size_t index = 0U; index < unit_count; ++index) {
        uint16_t value = units[index];
        if (value < 0x20U || value == (uint16_t)'"' ||
            value == (uint16_t)'*' || value == (uint16_t)'/' ||
            value == (uint16_t)':' || value == (uint16_t)'<' ||
            value == (uint16_t)'>' || value == (uint16_t)'?' ||
            value == (uint16_t)'\\' || value == (uint16_t)'|') return 0;
    }
    return 1;
}

static uint32_t shadow_days_before_year(int year) {
    uint32_t days = 0U;
    for (int cursor = 1970; cursor < year; ++cursor)
        days += ((cursor % 4 == 0 && cursor % 100 != 0) ||
                 cursor % 400 == 0) ? 366U : 365U;
    return days;
}

static uint32_t shadow_time_from_fat(uint16_t date, uint16_t time) {
    static const uint8_t month_days[12] =
        {31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U};
    int day = (int)(date & 0x1FU);
    int month = (int)((date >> 5U) & 0x0FU);
    int year = (int)((date >> 9U) & 0x7FU) + 1980;
    int second = (int)(time & 0x1FU) * 2;
    int minute = (int)((time >> 5U) & 0x3FU);
    int hour = (int)((time >> 11U) & 0x1FU);
    int leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    if (year < 1970 || year > 2107 || month < 1 || month > 12 ||
        day < 1 || day > (int)month_days[month - 1] +
            (month == 2 && leap ? 1 : 0) ||
        hour > 23 || minute > 59 || second > 59) return 0U;
    uint32_t days = shadow_days_before_year(year);
    for (int cursor = 1; cursor < month; ++cursor) {
        days += month_days[cursor - 1];
        if (cursor == 2 && leap) ++days;
    }
    days += (uint32_t)day - 1U;
    uint64_t seconds = (uint64_t)days * 86400ULL +
        (uint64_t)hour * 3600ULL + (uint64_t)minute * 60ULL +
        (uint32_t)second;
    return seconds > UINT32_MAX ? UINT32_MAX : (uint32_t)seconds;
}

static int shadow_mount(shadow_volume_t *volume, const reist_vfs_shadow_io_t *io,
                        const char *path, uint32_t path_length,
                        uint32_t *relative_offset) {
    uint32_t best_resource = UINT32_MAX;
    uint32_t best_length = 0U;
    x86os_drive_info_t best;
    shadow_zero(&best, sizeof(best));
    for (uint32_t resource = 0U;
         resource < REIST_VFS_SHADOW_MAX_RESOURCES; ++resource) {
        x86os_drive_info_t candidate;
        shadow_zero(&candidate, sizeof(candidate));
        int status = io->drive_info(io->context, resource, &candidate);
        if (status == 0) break;
        if (status < 0) return -5;
        uint32_t mount_length = shadow_text_length(
            candidate.mount_point, sizeof(candidate.mount_point));
        if (mount_length == 0U ||
            mount_length >= sizeof(candidate.mount_point) ||
            (mount_length > 1U &&
             candidate.mount_point[mount_length - 1U] == '/') ||
            !shadow_path_prefix(path, path_length, candidate.mount_point,
                                mount_length) ||
            mount_length <= best_length) continue;
        best_resource = resource;
        best_length = mount_length;
        best = candidate;
    }
    if (best_resource == UINT32_MAX || best.sectors == 0U) return -2;
    shadow_zero(volume, sizeof(*volume));
    volume->resource = best_resource;
    volume->sectors = best.sectors;
    volume->io = io;
    *relative_offset = best_length == 1U ? 1U : best_length;
    if (*relative_offset < path_length && path[*relative_offset] == '/')
        ++*relative_offset;
    return 0;
}

static int shadow_parse_bpb(shadow_volume_t *volume, uint8_t required_type) {
    uint8_t sector[X86OS_STORAGE_BLOCK_SIZE];
    int status = shadow_read(volume, 0U, sector);
    if (status != 0) return status;
    volume->signature = shadow_signature(sector, sizeof(sector));
    uint32_t total16 = shadow_get16(sector + 19U);
    uint32_t total32 = shadow_get32(sector + 32U);
    uint32_t total = total16 != 0U ? total16 : total32;
    uint32_t fat16_sectors = shadow_get16(sector + 22U);
    uint32_t fat32_sectors = shadow_get32(sector + 36U);
    uint32_t reserved = shadow_get16(sector + 14U);
    uint32_t fat_count = sector[16U];
    uint32_t sectors_per_cluster = sector[13U];
    uint32_t root_entries = shadow_get16(sector + 17U);
    if (sector[510U] != 0x55U || sector[511U] != 0xAAU ||
        shadow_get16(sector + 11U) != X86OS_STORAGE_BLOCK_SIZE ||
        sectors_per_cluster == 0U || sectors_per_cluster > 128U ||
        (sectors_per_cluster & (sectors_per_cluster - 1U)) != 0U ||
        reserved == 0U || fat_count == 0U || fat_count > 2U ||
        total == 0U || total > volume->sectors ||
        (total16 != 0U && total32 != 0U)) return -2;

    uint32_t fat_sectors = 0U;
    uint32_t root_dir_sectors = 0U;
    uint32_t root_cluster = 0U;
    uint32_t active_fat = 0U;
    uint8_t fat_type = FAT_TYPE_ANY;
    if (root_entries != 0U && fat16_sectors != 0U) {
        uint64_t root_bytes = (uint64_t)root_entries * 32U;
        root_dir_sectors = (uint32_t)((root_bytes +
            X86OS_STORAGE_BLOCK_SIZE - 1U) / X86OS_STORAGE_BLOCK_SIZE);
        fat_sectors = fat16_sectors;
        fat_type = FAT_TYPE_12;
    } else if (root_entries == 0U && fat16_sectors == 0U &&
               fat32_sectors != 0U && total16 == 0U) {
        uint32_t flags = shadow_get16(sector + 40U);
        if ((flags & 0xFF70U) != 0U) return -2;
        active_fat = (flags & 0x0080U) != 0U ? flags & 0x000FU : 0U;
        root_cluster = shadow_get32(sector + 44U);
        fat_sectors = fat32_sectors;
        fat_type = FAT_TYPE_32;
    } else {
        return -2;
    }

    uint64_t root_start = (uint64_t)reserved +
        (uint64_t)fat_count * fat_sectors;
    uint64_t metadata = root_start + root_dir_sectors;
    if (fat_sectors == 0U || metadata >= total ||
        metadata > UINT32_MAX || root_start > UINT32_MAX) return -2;
    uint32_t cluster_count = (total - (uint32_t)metadata) /
        sectors_per_cluster;
    if (cluster_count == 0U || active_fat >= fat_count) return -2;
    if (fat_type == FAT_TYPE_12) {
        uint64_t fat_entries = (uint64_t)fat_sectors *
            X86OS_STORAGE_BLOCK_SIZE * 2U / 3U;
        if (cluster_count >= FAT12_CLUSTER_LIMIT ||
            fat_entries < (uint64_t)cluster_count + 2U ||
            root_dir_sectors == 0U ||
            root_dir_sectors > REIST_VFS_SHADOW_MAX_SECTOR_READS)
            return -2;
    } else {
        uint64_t fat_entries = (uint64_t)fat_sectors *
            X86OS_STORAGE_BLOCK_SIZE / sizeof(uint32_t);
        if (cluster_count < FAT32_CLUSTER_MINIMUM ||
            cluster_count >= FAT32_RESERVED_MIN - 1U ||
            fat_entries < (uint64_t)cluster_count + 2U ||
            root_cluster < 2U || root_cluster > cluster_count + 1U)
            return -2;
    }
    if (required_type != FAT_TYPE_ANY && fat_type != required_type) return -2;
    volume->reserved_sectors = reserved;
    volume->fat_sectors = fat_sectors;
    volume->data_start = (uint32_t)metadata;
    volume->cluster_count = cluster_count;
    volume->root_cluster = root_cluster;
    volume->root_dir_start = (uint32_t)root_start;
    volume->root_dir_sectors = root_dir_sectors;
    volume->active_fat = active_fat;
    volume->sectors_per_cluster = (uint8_t)sectors_per_cluster;
    volume->fat_count = (uint8_t)fat_count;
    volume->fat_type = fat_type;
    return 0;
}

static int shadow_cluster_valid(const shadow_volume_t *volume,
                                uint32_t cluster) {
    return cluster >= 2U && cluster <= volume->cluster_count + 1U;
}

static void shadow_file_chain_guard_initialize(
        shadow_file_chain_guard_t *guard, uint32_t first) {
    guard->anchor = first;
    guard->power = 1U;
    guard->distance = 0U;
    guard->visited = 1U;
}

static int shadow_file_chain_advance(shadow_file_chain_guard_t *guard,
                                     uint32_t next) {
    if (guard == 0 ||
        guard->visited >= REIST_VFS_SHADOW_MAX_FILE_CHAIN_CLUSTERS)
        return -110;
    if (next == guard->anchor) return -5;
    ++guard->visited;
    ++guard->distance;
    if (guard->distance == guard->power) {
        guard->anchor = next;
        guard->distance = 0U;
        guard->power = guard->power <= UINT32_MAX / 2U
            ? guard->power * 2U : UINT32_MAX;
    }
    return 0;
}

static int shadow_fat_sector(shadow_volume_t *volume, uint32_t lba,
                             const uint8_t **sector) {
    if (volume == 0 || sector == 0) return -5;
    if (volume->fat_cache_valid != 0U && volume->fat_cache_lba == lba) {
        *sector = volume->fat_cache;
        return 0;
    }
    volume->fat_cache_valid = 0U;
    int status = shadow_read(volume, lba, volume->fat_cache);
    if (status != 0) return status;
    volume->fat_cache_lba = lba;
    volume->fat_cache_valid = 1U;
    *sector = volume->fat_cache;
    return 0;
}

static int shadow_next_cluster(shadow_volume_t *volume, uint32_t cluster,
                               uint32_t *next) {
    uint64_t byte_offset = volume->fat_type == FAT_TYPE_12
        ? (uint64_t)cluster + cluster / 2U
        : (uint64_t)cluster * sizeof(uint32_t);
    uint64_t sector_index = byte_offset / X86OS_STORAGE_BLOCK_SIZE;
    if (sector_index >= volume->fat_sectors) return -5;
    uint32_t lba = volume->reserved_sectors +
        volume->active_fat * volume->fat_sectors + (uint32_t)sector_index;
    const uint8_t *sector = 0;
    int status = shadow_fat_sector(volume, lba, &sector);
    if (status != 0) return status;
    uint32_t offset = (uint32_t)(byte_offset % X86OS_STORAGE_BLOCK_SIZE);
    if (volume->fat_type == FAT_TYPE_12) {
        uint16_t packed = sector[offset];
        if (offset + 1U == X86OS_STORAGE_BLOCK_SIZE) {
            if (sector_index + 1U >= volume->fat_sectors) return -5;
            const uint8_t low = (uint8_t)packed;
            status = shadow_fat_sector(volume, lba + 1U, &sector);
            if (status != 0) return status;
            packed = (uint16_t)low | (uint16_t)sector[0U] << 8U;
        } else {
            packed |= (uint16_t)sector[offset + 1U] << 8U;
        }
        *next = (cluster & 1U) != 0U ? packed >> 4U : packed & 0x0FFFU;
    } else {
        if (offset + sizeof(uint32_t) > X86OS_STORAGE_BLOCK_SIZE) return -5;
        *next = shadow_get32(sector + offset) & 0x0FFFFFFFU;
    }
    return 0;
}

static int shadow_end_of_chain(const shadow_volume_t *volume,
                               uint32_t cluster) {
    return volume->fat_type == FAT_TYPE_12
        ? cluster >= FAT12_EOC_MIN && cluster <= 0x0FFFU
        : cluster >= FAT32_EOC_MIN && cluster <= 0x0FFFFFFFU;
}

static int shadow_invalid_link(const shadow_volume_t *volume,
                               uint32_t cluster) {
    if (cluster == 0U) return 1;
    if (volume->fat_type == FAT_TYPE_12)
        return cluster == FAT12_BAD_CLUSTER || cluster >= FAT12_RESERVED_MIN;
    return cluster == FAT32_BAD_CLUSTER || cluster >= FAT32_RESERVED_MIN;
}

static uint8_t shadow_short_checksum(const uint8_t name[11]) {
    uint8_t checksum = 0U;
    for (uint32_t index = 0U; index < 11U; ++index) {
        checksum = (uint8_t)(((checksum & 1U) << 7U) |
                             ((checksum & 0xFEU) >> 1U));
        checksum = (uint8_t)(checksum + name[index]);
    }
    return checksum;
}

static void shadow_short_name(const shadow_dir_entry_t *entry,
                              char output[13]) {
    uint32_t used = 0U;
    uint8_t nt = entry->bytes[12U];
    for (uint32_t index = 0U; index < 8U && entry->bytes[index] != ' ';
         ++index) {
        char value = (char)entry->bytes[index];
        if ((nt & 0x08U) != 0U) value = shadow_lower(value);
        output[used++] = value;
    }
    if (entry->bytes[8U] != ' ') {
        output[used++] = '.';
        for (uint32_t index = 8U; index < 11U && entry->bytes[index] != ' ';
             ++index) {
            char value = (char)entry->bytes[index];
            if ((nt & 0x10U) != 0U) value = shadow_lower(value);
            output[used++] = value;
        }
    }
    output[used] = '\0';
}

static void shadow_lfn_reset(shadow_lfn_t *lfn) {
    shadow_zero(lfn, sizeof(*lfn));
}

static uint16_t shadow_lfn_character(const uint8_t *entry, uint32_t index) {
    static const uint8_t offsets[13] =
        {1U, 3U, 5U, 7U, 9U, 14U, 16U, 18U, 20U, 22U, 24U, 28U, 30U};
    return shadow_get16(entry + offsets[index]);
}

static void shadow_lfn_consume(shadow_lfn_t *lfn, const uint8_t *entry) {
    uint32_t order = entry[0U] & 0x1FU;
    int starts = (entry[0U] & 0x40U) != 0U;
    if (entry[11U] != FAT32_ATTR_LONG_NAME || entry[12U] != 0U ||
        shadow_get16(entry + 26U) != 0U || order == 0U ||
        order > FAT32_MAX_LFN_ENTRIES) {
        shadow_lfn_reset(lfn);
        return;
    }
    if (starts) {
        shadow_lfn_reset(lfn);
        lfn->active = 1U;
        lfn->checksum = entry[13U];
        lfn->expected_order = (uint8_t)order;
        lfn->slot_count = (uint8_t)order;
        for (uint32_t index = 0U;
             index < FAT32_MAX_LFN_ENTRIES * FAT32_LFN_CHARS_PER_ENTRY;
             ++index) lfn->units[index] = 0xFFFFU;
    }
    if (lfn->active == 0U || lfn->checksum != entry[13U] ||
        lfn->expected_order != order) {
        shadow_lfn_reset(lfn);
        return;
    }
    uint32_t base = (order - 1U) * FAT32_LFN_CHARS_PER_ENTRY;
    for (uint32_t index = 0U; index < FAT32_LFN_CHARS_PER_ENTRY; ++index) {
        uint16_t value = shadow_lfn_character(entry, index);
        uint32_t position = base + index;
        if (position >= FAT32_MAX_LFN_ENTRIES *
                        FAT32_LFN_CHARS_PER_ENTRY) {
            shadow_lfn_reset(lfn);
            return;
        }
        lfn->units[position] = value;
    }
    --lfn->expected_order;
}

static int shadow_lfn_finish(shadow_lfn_t *lfn) {
    if (lfn == 0 || lfn->active == 0U || lfn->expected_order != 0U ||
        lfn->slot_count == 0U || lfn->slot_count > FAT32_MAX_LFN_ENTRIES)
        return 0;
    size_t available = (size_t)lfn->slot_count * FAT32_LFN_CHARS_PER_ENTRY;
    size_t unit_count = available;
    int terminated = 0;
    for (size_t index = 0U; index < available; ++index) {
        uint16_t value = lfn->units[index];
        if (terminated == 0 && value == 0U) {
            unit_count = index;
            terminated = 1;
        } else if ((terminated == 0 && value == 0xFFFFU) ||
                   (terminated != 0 && value != 0xFFFFU)) return 0;
    }
    if (unit_count == 0U || unit_count > 255U) return 0;
    size_t output_bytes = 0U;
    return reist_utf16_to_utf8(lfn->units, unit_count, lfn->name,
                               sizeof(lfn->name), &output_bytes) &&
           output_bytes != 0U &&
           shadow_valid_component(lfn->name, (uint32_t)output_bytes);
}

/* Returns 0 to continue, 1 on match, and 2 at the directory end marker. */
static int shadow_scan_entries(const uint8_t sector[X86OS_STORAGE_BLOCK_SIZE],
                               uint32_t sector_lba,
                               const char *wanted,
                               shadow_dir_entry_t *found,
                               char visible[256], shadow_lfn_t *lfn,
                               uint32_t *entry_sector,
                               uint32_t *entry_offset) {
    for (uint32_t offset = 0U;
         offset < X86OS_STORAGE_BLOCK_SIZE; offset += 32U) {
        const uint8_t *candidate = sector + offset;
        if (candidate[0U] == 0x00U) return 2;
        if (candidate[0U] == 0xE5U) {
            shadow_lfn_reset(lfn);
            continue;
        }
        if (candidate[11U] == FAT32_ATTR_LONG_NAME) {
            shadow_lfn_consume(lfn, candidate);
            continue;
        }
        if ((candidate[11U] & FAT32_ATTR_VOLUME_ID) != 0U) {
            shadow_lfn_reset(lfn);
            continue;
        }
        shadow_dir_entry_t entry;
        shadow_copy(entry.bytes, candidate, sizeof(entry.bytes));
        char short_name[13];
        shadow_short_name(&entry, short_name);
        const char *resolved = short_name;
        if (lfn->active != 0U &&
            lfn->checksum == shadow_short_checksum(candidate) &&
            shadow_lfn_finish(lfn))
            resolved = lfn->name;
        int match = shadow_name_equal(resolved, wanted) ||
                    shadow_name_equal(short_name, wanted);
        if (match) {
            *found = entry;
            if (entry_sector != 0) *entry_sector = sector_lba;
            if (entry_offset != 0) *entry_offset = offset;
            uint32_t length = shadow_text_length(resolved, 256U);
            if (length >= 256U) return -5;
            shadow_zero(visible, 256U);
            shadow_copy(visible, resolved, length + 1U);
            return 1;
        }
        shadow_lfn_reset(lfn);
    }
    return 0;
}

static int shadow_scan_directory(shadow_volume_t *volume, uint32_t first,
                                 const char *wanted,
                                 shadow_dir_entry_t *found,
                                 char visible[256], uint32_t *entry_sector,
                                 uint32_t *entry_offset) {
    uint8_t sector[X86OS_STORAGE_BLOCK_SIZE];
    uint32_t visited[REIST_VFS_SHADOW_MAX_CHAIN_CLUSTERS];
    uint32_t visited_count = 0U;
    uint32_t cluster = first;
    shadow_lfn_t lfn;
    shadow_lfn_reset(&lfn);
    if (volume->fat_type == FAT_TYPE_12 && first == 0U) {
        for (uint32_t index = 0U; index < volume->root_dir_sectors; ++index) {
            int status = shadow_read(volume, volume->root_dir_start + index,
                                     sector);
            if (status != 0) return status;
            uint32_t sector_lba = volume->root_dir_start + index;
            int scan = shadow_scan_entries(sector, sector_lba, wanted, found,
                                           visible, &lfn, entry_sector,
                                           entry_offset);
            if (scan != 0) return scan == 2 ? 0 : scan;
        }
        return 0;
    }
    for (;;) {
        if (!shadow_cluster_valid(volume, cluster) ||
            visited_count >= REIST_VFS_SHADOW_MAX_CHAIN_CLUSTERS) return -5;
        for (uint32_t index = 0U; index < visited_count; ++index)
            if (visited[index] == cluster) return -5;
        visited[visited_count++] = cluster;
        uint64_t cluster_lba = (uint64_t)volume->data_start +
            (uint64_t)(cluster - 2U) * volume->sectors_per_cluster;
        if (cluster_lba + volume->sectors_per_cluster > volume->sectors)
            return -5;
        for (uint32_t sector_index = 0U;
             sector_index < volume->sectors_per_cluster; ++sector_index) {
            int status = shadow_read(volume,
                (uint32_t)cluster_lba + sector_index, sector);
            if (status != 0) return status;
            uint32_t sector_lba = (uint32_t)cluster_lba + sector_index;
            int scan = shadow_scan_entries(sector, sector_lba, wanted, found,
                                           visible, &lfn, entry_sector,
                                           entry_offset);
            if (scan != 0) return scan == 2 ? 0 : scan;
        }
        uint32_t next = 0U;
        int status = shadow_next_cluster(volume, cluster, &next);
        if (status != 0) return status;
        if (shadow_end_of_chain(volume, next)) return 0;
        if (shadow_invalid_link(volume, next) ||
            !shadow_cluster_valid(volume, next))
            return -5;
        cluster = next;
    }
}

static void shadow_entry_info(const shadow_dir_entry_t *entry,
                              const char *visible,
                              x86os_file_info_t *info) {
    shadow_zero(info, sizeof(*info));
    uint32_t length = shadow_text_length(visible, sizeof(info->name));
    if (length < sizeof(info->name)) shadow_copy(info->name, visible, length + 1U);
    info->type = (entry->bytes[11U] & FAT32_ATTR_DIRECTORY) != 0U
        ? X86OS_DIRECTORY : X86OS_FILE;
    info->size = shadow_get32(entry->bytes + 28U);
    info->create_time = shadow_time_from_fat(
        shadow_get16(entry->bytes + 16U), shadow_get16(entry->bytes + 14U));
    info->modify_time = shadow_time_from_fat(
        shadow_get16(entry->bytes + 24U), shadow_get16(entry->bytes + 22U));
    info->access_time = shadow_time_from_fat(
        shadow_get16(entry->bytes + 18U), 0U);
}

static int shadow_entry_cluster(const shadow_volume_t *volume,
                                const shadow_dir_entry_t *entry,
                                uint32_t *cluster) {
    uint32_t high = shadow_get16(entry->bytes + 20U);
    if (volume->fat_type == FAT_TYPE_12 && high != 0U) return -5;
    *cluster = shadow_get16(entry->bytes + 26U) | (high << 16U);
    return 0;
}

static int shadow_resolve(const reist_vfs_shadow_io_t *io,
                          const char *absolute_path, uint32_t path_length,
                          shadow_volume_t *volume, shadow_dir_entry_t *entry,
                          char visible[256], uint32_t *entry_sector,
                          uint32_t *entry_offset, uint32_t *parent_cluster) {
    if (parent_cluster != 0) *parent_cluster = 0;
    if (io == 0 || io->drive_info == 0 || io->read_sector == 0 ||
        absolute_path == 0 || volume == 0 || entry == 0 || visible == 0 ||
        path_length == 0U ||
        path_length >= X86OS_VFS_SHADOW_PATH_CAPACITY ||
        absolute_path[0U] != '/' || absolute_path[path_length] != '\0')
        return -22;
    for (uint32_t index = 0U; index < path_length; ++index)
        if (absolute_path[index] == '\0' ||
            (index > 0U && absolute_path[index] == '/' &&
             absolute_path[index - 1U] == '/')) return -22;
    uint32_t cursor = 0U;
    int status = shadow_mount(volume, io, absolute_path, path_length, &cursor);
    if (status != 0) return status;
    status = shadow_parse_bpb(volume, FAT_TYPE_ANY);
    if (status != 0) return status;
    shadow_zero(entry, sizeof(*entry));
    shadow_zero(visible, 256U);
    if (cursor >= path_length) {
        entry->bytes[11U] = FAT32_ATTR_DIRECTORY;
        uint32_t root = volume->fat_type == FAT_TYPE_12
            ? 0U : volume->root_cluster;
        entry->bytes[20U] = (uint8_t)(root >> 16U);
        entry->bytes[21U] = (uint8_t)(root >> 24U);
        entry->bytes[26U] = (uint8_t)root;
        entry->bytes[27U] = (uint8_t)(root >> 8U);
        visible[0U] = '/';
        return 0;
    }
    uint32_t directory = volume->fat_type == FAT_TYPE_12
        ? 0U : volume->root_cluster;
    for (uint32_t component = 0U;
         component < REIST_VFS_SHADOW_MAX_COMPONENTS; ++component) {
        uint32_t start = cursor;
        while (cursor < path_length && absolute_path[cursor] != '/') ++cursor;
        uint32_t length = cursor - start;
        if (!shadow_valid_component(absolute_path + start, length)) return -22;
        char wanted[256];
        shadow_zero(wanted, sizeof(wanted));
        shadow_copy(wanted, absolute_path + start, length);
        int lookup = shadow_scan_directory(volume, directory, wanted,
                                           entry, visible, entry_sector,
                                           entry_offset);
        if (lookup <= 0) return lookup == 0 ? -2 : lookup;
        uint32_t found_cluster = 0U;
        if (shadow_entry_cluster(volume, entry, &found_cluster) != 0)
            return -5;
        while (cursor < path_length && absolute_path[cursor] == '/') ++cursor;
        if (cursor >= path_length) {
            if (parent_cluster != 0) *parent_cluster = directory;
            return 0;
        }
        if ((entry->bytes[11U] & FAT32_ATTR_DIRECTORY) == 0U) return -2;
        directory = found_cluster;
        if (!shadow_cluster_valid(volume, directory)) return -5;
    }
    return -110;
}

static int shadow_stat(const reist_vfs_shadow_io_t *io,
                       const char *absolute_path, uint32_t path_length,
                       x86os_file_info_t *info, uint8_t required_type) {
    if (io == 0 || io->drive_info == 0 || io->read_sector == 0 ||
        absolute_path == 0 || info == 0 || path_length == 0U ||
        path_length >= X86OS_VFS_SHADOW_PATH_CAPACITY ||
        absolute_path[0U] != '/' || absolute_path[path_length] != '\0')
        return -22;
    for (uint32_t index = 0U; index < path_length; ++index)
        if (absolute_path[index] == '\0' ||
            (index > 0U && absolute_path[index] == '/' &&
             absolute_path[index - 1U] == '/')) return -22;

    shadow_volume_t volume;
    uint32_t cursor = 0U;
    int status = shadow_mount(&volume, io, absolute_path, path_length, &cursor);
    if (status != 0) return status;
    status = shadow_parse_bpb(&volume, required_type);
    if (status != 0) return status;
    if (cursor >= path_length) {
        shadow_zero(info, sizeof(*info));
        info->name[0U] = '/';
        info->type = X86OS_DIRECTORY;
        return 0;
    }

    uint32_t directory = volume.fat_type == FAT_TYPE_12
        ? 0U : volume.root_cluster;
    for (uint32_t component = 0U;
         component < REIST_VFS_SHADOW_MAX_COMPONENTS; ++component) {
        uint32_t start = cursor;
        while (cursor < path_length && absolute_path[cursor] != '/') ++cursor;
        uint32_t length = cursor - start;
        if (!shadow_valid_component(absolute_path + start, length)) return -22;
        char wanted[256];
        shadow_zero(wanted, sizeof(wanted));
        shadow_copy(wanted, absolute_path + start, length);
        shadow_dir_entry_t found;
        char visible[256];
        int lookup = shadow_scan_directory(&volume, directory, wanted,
                                           &found, visible, 0, 0);
        if (lookup <= 0) return lookup == 0 ? -2 : lookup;
        uint32_t found_cluster = 0U;
        if (shadow_entry_cluster(&volume, &found, &found_cluster) != 0)
            return -5;
        while (cursor < path_length && absolute_path[cursor] == '/') ++cursor;
        if (cursor >= path_length) {
            shadow_entry_info(&found, visible, info);
            return 0;
        }
        if ((found.bytes[11U] & FAT32_ATTR_DIRECTORY) == 0U) return -2;
        directory = found_cluster;
        if (!shadow_cluster_valid(&volume, directory)) return -5;
    }
    return -110;
}

int reist_vfs_shadow_fat32_stat(const reist_vfs_shadow_io_t *io,
                                const char *absolute_path,
                                uint32_t path_length,
                                x86os_file_info_t *info) {
    return shadow_stat(io, absolute_path, path_length, info, FAT_TYPE_32);
}

int reist_vfs_shadow_fat_stat(const reist_vfs_shadow_io_t *io,
                              const char *absolute_path,
                              uint32_t path_length,
                              x86os_file_info_t *info) {
    return shadow_stat(io, absolute_path, path_length, info, FAT_TYPE_ANY);
}

static int shadow_read_file(shadow_volume_t *volume,
                            const shadow_dir_entry_t *entry, uint32_t offset,
                            uint8_t *data, uint32_t capacity,
                            uint32_t *transferred) {
    if ((entry->bytes[11U] & FAT32_ATTR_DIRECTORY) != 0U) return -21;
    uint32_t size = shadow_get32(entry->bytes + 28U);
    if (offset >= size) return 0;
    uint32_t cluster = 0U;
    if (shadow_entry_cluster(volume, entry, &cluster) != 0 ||
        !shadow_cluster_valid(volume, cluster)) return -5;
    uint32_t cluster_bytes = (uint32_t)volume->sectors_per_cluster *
        X86OS_STORAGE_BLOCK_SIZE;
    uint32_t skip = offset / cluster_bytes;
    uint32_t in_cluster = offset % cluster_bytes;
    shadow_file_chain_guard_t guard;
    shadow_file_chain_guard_initialize(&guard, cluster);
    int status = 0;
    for (;;) {
        if (!shadow_cluster_valid(volume, cluster)) return -5;
        if (skip == 0U) break;
        uint32_t next = 0U;
        status = shadow_next_cluster(volume, cluster, &next);
        if (status != 0) return status;
        if (shadow_end_of_chain(volume, next) ||
            shadow_invalid_link(volume, next) ||
            !shadow_cluster_valid(volume, next)) return -5;
        status = shadow_file_chain_advance(&guard, next);
        if (status != 0) return status;
        cluster = next;
        --skip;
    }
    uint32_t amount = size - offset < capacity ? size - offset : capacity;
    uint32_t completed = 0U;
    uint8_t sector[X86OS_STORAGE_BLOCK_SIZE];
    while (completed < amount) {
        uint64_t first = (uint64_t)volume->data_start +
            (uint64_t)(cluster - 2U) * volume->sectors_per_cluster;
        uint32_t sector_in_cluster = in_cluster / X86OS_STORAGE_BLOCK_SIZE;
        uint32_t in_sector = in_cluster % X86OS_STORAGE_BLOCK_SIZE;
        if (first + sector_in_cluster >= volume->sectors) return -5;
        status = shadow_read(volume, (uint32_t)first + sector_in_cluster,
                             sector);
        if (status != 0) return status;
        uint32_t chunk = X86OS_STORAGE_BLOCK_SIZE - in_sector;
        if (chunk > amount - completed) chunk = amount - completed;
        shadow_copy(data + completed, sector + in_sector, chunk);
        completed += chunk;
        in_cluster += chunk;
        if (in_cluster < cluster_bytes || completed == amount) continue;
        uint32_t next = 0U;
        status = shadow_next_cluster(volume, cluster, &next);
        if (status != 0) return status;
        if (shadow_end_of_chain(volume, next) ||
            shadow_invalid_link(volume, next) ||
            !shadow_cluster_valid(volume, next)) return -5;
        status = shadow_file_chain_advance(&guard, next);
        if (status != 0) return status;
        cluster = next;
        in_cluster = 0U;
    }
    *transferred = completed;
    return 0;
}

int reist_vfs_shadow_fat_read(const reist_vfs_shadow_io_t *io,
                              const char *absolute_path,
                              uint32_t path_length, uint32_t offset,
                              uint8_t *data, uint32_t capacity,
                              uint32_t *transferred) {
    if (data == 0 || transferred == 0 || capacity == 0U ||
        capacity > X86OS_VFS_SHADOW_READ_CAPACITY) return -22;
    shadow_zero(data, capacity);
    *transferred = 0U;
    shadow_volume_t volume;
    shadow_dir_entry_t entry;
    char visible[256];
    int status = shadow_resolve(io, absolute_path, path_length, &volume,
                                &entry, visible, 0, 0, 0);
    if (status != 0) return status;
    status = shadow_read_file(&volume, &entry, offset, data, capacity,
                              transferred);
    if (status != 0) {
        shadow_zero(data, capacity);
        *transferred = 0U;
    }
    return status;
}

static int shadow_readdir_sector(const uint8_t sector[512], uint32_t wanted,
                                 uint32_t *current, shadow_lfn_t *lfn,
                                 shadow_dir_entry_t *entry,
                                 char visible[256], uint32_t start_offset,
                                 uint32_t *next_offset) {
    if (start_offset > 512U || (start_offset & 31U) != 0U ||
        next_offset == 0) return -22;
    *next_offset = start_offset;
    for (uint32_t offset = start_offset; offset < 512U; offset += 32U) {
        const uint8_t *candidate = sector + offset;
        if (candidate[0U] == 0x00U) return 2;
        if (candidate[0U] == 0xE5U) { shadow_lfn_reset(lfn); continue; }
        if (candidate[11U] == FAT32_ATTR_LONG_NAME) {
            shadow_lfn_consume(lfn, candidate);
            continue;
        }
        if ((candidate[11U] & FAT32_ATTR_VOLUME_ID) != 0U) {
            shadow_lfn_reset(lfn);
            continue;
        }
        shadow_dir_entry_t parsed;
        shadow_copy(parsed.bytes, candidate, sizeof(parsed.bytes));
        char short_name[13];
        shadow_short_name(&parsed, short_name);
        const char *resolved = short_name;
        if (lfn->active != 0U &&
            lfn->checksum == shadow_short_checksum(candidate) &&
            shadow_lfn_finish(lfn))
            resolved = lfn->name;
        int dot = shadow_name_equal(resolved, ".") ||
            shadow_name_equal(resolved, "..");
        if (!dot && (*current)++ == wanted) {
            uint32_t length = shadow_text_length(resolved, 256U);
            if (length == 0U || length >= 256U) return -5;
            *entry = parsed;
            shadow_zero(visible, 256U);
            shadow_copy(visible, resolved, length + 1U);
            *next_offset = offset + 32U;
            return 1;
        }
        shadow_lfn_reset(lfn);
    }
    *next_offset = 512U;
    return 0;
}

static void shadow_readdir_cursor_reset(
        reist_vfs_shadow_fat_readdir_cursor_t *cursor) {
    if (cursor != 0) shadow_zero(cursor, sizeof(*cursor));
}

static int shadow_readdir_chain_position(
        shadow_volume_t *volume, uint32_t directory,
        const reist_vfs_shadow_fat_readdir_cursor_t *cursor,
        uint32_t visited[REIST_VFS_SHADOW_MAX_CHAIN_CLUSTERS],
        uint32_t *visited_count, uint32_t *cluster) {
    if (volume == 0 || cursor == 0 || visited == 0 ||
        visited_count == 0 || cluster == 0 ||
        cursor->cluster_depth >= REIST_VFS_SHADOW_MAX_CHAIN_CLUSTERS)
        return 0;
    uint32_t current = directory;
    uint32_t count = 0U;
    for (uint32_t depth = 0U; depth <= cursor->cluster_depth; ++depth) {
        if (!shadow_cluster_valid(volume, current) ||
            count >= REIST_VFS_SHADOW_MAX_CHAIN_CLUSTERS) return 0;
        for (uint32_t seen = 0U; seen < count; ++seen)
            if (visited[seen] == current) return 0;
        visited[count++] = current;
        if (depth == cursor->cluster_depth) {
            if (current != cursor->cluster) return 0;
            *visited_count = count;
            *cluster = current;
            return 1;
        }
        uint32_t next = 0U;
        int status = shadow_next_cluster(volume, current, &next);
        if (status != 0) return status;
        if (shadow_end_of_chain(volume, next) ||
            shadow_invalid_link(volume, next) ||
            !shadow_cluster_valid(volume, next)) return 0;
        current = next;
    }
    return 0;
}

static int shadow_readdir_resume_valid(
        shadow_volume_t *volume, const shadow_dir_entry_t *directory_entry,
        uint32_t directory, uint32_t index,
        const reist_vfs_shadow_fat_readdir_cursor_t *cursor,
        uint32_t visited[REIST_VFS_SHADOW_MAX_CHAIN_CLUSTERS],
        uint32_t *visited_count, uint32_t *cluster) {
    if (volume == 0 || directory_entry == 0 || cursor == 0 ||
        cursor->version != REIST_VFS_SHADOW_READDIR_CURSOR_VERSION ||
        cursor->struct_size != sizeof(*cursor) || cursor->active != 1U ||
        cursor->next_index != index || cursor->resource != volume->resource ||
        cursor->volume_signature != volume->signature ||
        cursor->directory_generation !=
            shadow_entry_generation(directory_entry->bytes) ||
        cursor->directory_cluster != directory ||
        cursor->fat_type != volume->fat_type || cursor->entry_offset > 512U ||
        (cursor->entry_offset & 31U) != 0U) return 0;
    if (volume->fat_type == FAT_TYPE_12 && directory == 0U) {
        if (cursor->cluster != 0U || cursor->cluster_depth != 0U ||
            cursor->sector_index > volume->root_dir_sectors ||
            (cursor->sector_index == volume->root_dir_sectors &&
             cursor->entry_offset != 0U)) return 0;
        *visited_count = 0U;
        *cluster = 0U;
        return 1;
    }
    if (cursor->sector_index > volume->sectors_per_cluster ||
        (cursor->sector_index == volume->sectors_per_cluster &&
         cursor->entry_offset != 0U)) return 0;
    return shadow_readdir_chain_position(
        volume, directory, cursor, visited, visited_count, cluster);
}

static void shadow_readdir_cursor_publish(
        reist_vfs_shadow_fat_readdir_cursor_t *cursor,
        const shadow_volume_t *volume,
        const shadow_dir_entry_t *directory_entry, uint32_t directory,
        uint32_t index, uint32_t cluster, uint32_t cluster_depth,
        uint32_t sector_index, uint32_t entry_offset) {
    shadow_readdir_cursor_reset(cursor);
    if (index == UINT32_MAX) return;
    cursor->version = REIST_VFS_SHADOW_READDIR_CURSOR_VERSION;
    cursor->struct_size = sizeof(*cursor);
    cursor->next_index = index + 1U;
    cursor->resource = volume->resource;
    cursor->volume_signature = volume->signature;
    cursor->directory_generation =
        shadow_entry_generation(directory_entry->bytes);
    cursor->directory_cluster = directory;
    cursor->cluster = cluster;
    cursor->cluster_depth = cluster_depth;
    cursor->sector_index = sector_index;
    cursor->entry_offset = entry_offset;
    cursor->fat_type = volume->fat_type;
    cursor->active = 1U;
}

int reist_vfs_shadow_fat_readdir_continue(
        const reist_vfs_shadow_io_t *io, const char *absolute_path,
        uint32_t path_length, uint32_t index,
        reist_vfs_shadow_fat_readdir_cursor_t *cursor,
        x86os_file_info_t *info) {
    if (cursor == 0 || info == 0) return -22;
    shadow_zero(info, sizeof(*info));
    shadow_volume_t volume;
    shadow_dir_entry_t directory_entry;
    char visible[256];
    int status = shadow_resolve(io, absolute_path, path_length, &volume,
                                &directory_entry, visible, 0, 0, 0);
    if (status != 0) {
        shadow_readdir_cursor_reset(cursor);
        return status;
    }
    if ((directory_entry.bytes[11U] & FAT32_ATTR_DIRECTORY) == 0U) {
        shadow_readdir_cursor_reset(cursor);
        return -20;
    }
    uint32_t directory = 0U;
    if (shadow_entry_cluster(&volume, &directory_entry, &directory) != 0) {
        shadow_readdir_cursor_reset(cursor);
        return -5;
    }
    uint32_t current = 0U;
    uint32_t cluster = directory;
    uint32_t cluster_depth = 0U;
    uint32_t sector_index = 0U;
    uint32_t entry_offset = 0U;
    uint32_t visited[REIST_VFS_SHADOW_MAX_CHAIN_CLUSTERS];
    uint32_t visited_count = 0U;
    int resume = shadow_readdir_resume_valid(
        &volume, &directory_entry, directory, index, cursor, visited,
        &visited_count, &cluster);
    if (resume < 0) {
        shadow_readdir_cursor_reset(cursor);
        return resume;
    }
    if (resume != 0) {
        current = index;
        cluster_depth = cursor->cluster_depth;
        sector_index = cursor->sector_index;
        entry_offset = cursor->entry_offset;
    } else {
        shadow_readdir_cursor_reset(cursor);
    }
    shadow_lfn_t lfn;
    shadow_lfn_reset(&lfn);
    uint8_t sector[512];
    shadow_dir_entry_t found;
    if (volume.fat_type == FAT_TYPE_12 && directory == 0U) {
        if (entry_offset == 512U) {
            ++sector_index;
            entry_offset = 0U;
        }
        for (; sector_index < volume.root_dir_sectors; ++sector_index) {
            status = shadow_read(
                &volume, volume.root_dir_start + sector_index, sector);
            if (status != 0) {
                shadow_readdir_cursor_reset(cursor);
                return status;
            }
            uint32_t next_offset = 0U;
            int scan = shadow_readdir_sector(
                sector, index, &current, &lfn, &found, visible, entry_offset,
                &next_offset);
            if (scan == 1) {
                shadow_entry_info(&found, visible, info);
                shadow_readdir_cursor_publish(
                    cursor, &volume, &directory_entry, directory, index, 0U,
                    0U, sector_index, next_offset);
                return 0;
            }
            if (scan == 2) {
                shadow_readdir_cursor_reset(cursor);
                return 1;
            }
            if (scan < 0) {
                shadow_readdir_cursor_reset(cursor);
                return scan;
            }
            entry_offset = 0U;
        }
        shadow_readdir_cursor_reset(cursor);
        return 1;
    }
    if (visited_count == 0U) {
        if (!shadow_cluster_valid(&volume, cluster)) return -110;
        visited[visited_count++] = cluster;
    }
    for (;;) {
        if (entry_offset == 512U) {
            ++sector_index;
            entry_offset = 0U;
        }
        if (!shadow_cluster_valid(&volume, cluster) ||
            visited_count == 0U ||
            visited_count > REIST_VFS_SHADOW_MAX_CHAIN_CLUSTERS) {
            shadow_readdir_cursor_reset(cursor);
            return -110;
        }
        uint64_t first = (uint64_t)volume.data_start +
            (uint64_t)(cluster - 2U) * volume.sectors_per_cluster;
        if (first + volume.sectors_per_cluster > volume.sectors) {
            shadow_readdir_cursor_reset(cursor);
            return -5;
        }
        for (; sector_index < volume.sectors_per_cluster; ++sector_index) {
            status = shadow_read(
                &volume, (uint32_t)first + sector_index, sector);
            if (status != 0) {
                shadow_readdir_cursor_reset(cursor);
                return status;
            }
            uint32_t next_offset = 0U;
            int scan = shadow_readdir_sector(
                sector, index, &current, &lfn, &found, visible, entry_offset,
                &next_offset);
            if (scan == 1) {
                shadow_entry_info(&found, visible, info);
                shadow_readdir_cursor_publish(
                    cursor, &volume, &directory_entry, directory, index,
                    cluster, cluster_depth, sector_index, next_offset);
                return 0;
            }
            if (scan == 2) {
                shadow_readdir_cursor_reset(cursor);
                return 1;
            }
            if (scan < 0) {
                shadow_readdir_cursor_reset(cursor);
                return scan;
            }
            entry_offset = 0U;
        }
        uint32_t next = 0U;
        status = shadow_next_cluster(&volume, cluster, &next);
        if (status != 0) {
            shadow_readdir_cursor_reset(cursor);
            return status;
        }
        if (shadow_end_of_chain(&volume, next)) {
            shadow_readdir_cursor_reset(cursor);
            return 1;
        }
        if (shadow_invalid_link(&volume, next) ||
            !shadow_cluster_valid(&volume, next)) {
            shadow_readdir_cursor_reset(cursor);
            return -5;
        }
        if (visited_count >= REIST_VFS_SHADOW_MAX_CHAIN_CLUSTERS) {
            shadow_readdir_cursor_reset(cursor);
            return -110;
        }
        for (uint32_t seen = 0U; seen < visited_count; ++seen)
            if (visited[seen] == next) {
                shadow_readdir_cursor_reset(cursor);
                return -5;
            }
        visited[visited_count++] = next;
        cluster = next;
        ++cluster_depth;
        sector_index = 0U;
        entry_offset = 0U;
    }
}

int reist_vfs_shadow_fat_readdir(const reist_vfs_shadow_io_t *io,
                                 const char *absolute_path,
                                 uint32_t path_length, uint32_t index,
                                 x86os_file_info_t *info) {
    reist_vfs_shadow_fat_readdir_cursor_t cursor;
    shadow_zero(&cursor, sizeof(cursor));
    return reist_vfs_shadow_fat_readdir_continue(
        io, absolute_path, path_length, index, &cursor, info);
}

static int shadow_object_volume(const reist_vfs_shadow_io_t *io,
        const reist_vfs_shadow_object_t *object, shadow_volume_t *volume) {
    if (io == 0 || io->drive_info == 0 || io->read_sector == 0 ||
        object == 0 || volume == 0 ||
        object->version != REIST_VFS_SHADOW_OBJECT_VERSION ||
        object->struct_size != sizeof(*object) ||
        object->filesystem != REIST_VFS_SHADOW_OBJECT_FAT ||
        object->resource >= REIST_VFS_SHADOW_MAX_RESOURCES) return -22;
    shadow_zero(volume, sizeof(*volume));
    x86os_drive_info_t drive;
    shadow_zero(&drive, sizeof(drive));
    int available = io->drive_info(io->context, object->resource, &drive);
    if (available <= 0 || drive.sectors == 0U) return -116;
    shadow_zero(volume, sizeof(*volume));
    volume->resource = object->resource;
    volume->sectors = drive.sectors;
    volume->io = io;
    int status = shadow_parse_bpb(volume, FAT_TYPE_ANY);
    if (status != 0) return status;
    return volume->signature == object->volume_signature ? 0 : -116;
}

static int shadow_object_entry(shadow_volume_t *volume,
        const reist_vfs_shadow_object_t *object, shadow_dir_entry_t *entry,
        char visible[256]) {
    if (object->locator_a >= volume->sectors ||
        object->locator_b > X86OS_STORAGE_BLOCK_SIZE - 32U ||
        (object->locator_b & 31U) != 0U) return -116;
    uint8_t sector[X86OS_STORAGE_BLOCK_SIZE];
    int status = shadow_read(volume, object->locator_a, sector);
    if (status != 0) return status;
    const uint8_t *candidate = sector + object->locator_b;
    if (candidate[0U] == 0U || candidate[0U] == 0xE5U ||
        candidate[11U] == FAT32_ATTR_LONG_NAME ||
        (candidate[11U] & (FAT32_ATTR_VOLUME_ID | FAT32_ATTR_DIRECTORY)) != 0U)
        return -116;
    shadow_copy(entry->bytes, candidate, sizeof(entry->bytes));
    uint32_t cluster = 0U;
    if (shadow_entry_cluster(volume, entry, &cluster) != 0 ||
        cluster != object->locator_c ||
        shadow_entry_generation(entry->bytes) != object->object_generation)
        return -116;
    uint32_t size = shadow_get32(entry->bytes + 28U);
    if ((size == 0U && cluster != 0U &&
         !shadow_cluster_valid(volume, cluster)) ||
        (size != 0U && !shadow_cluster_valid(volume, cluster))) return -116;
    shadow_zero(visible, 256U);
    shadow_short_name(entry, visible);
    return 0;
}

int reist_vfs_shadow_fat_object_open(
        const reist_vfs_shadow_io_t *io, const char *absolute_path,
        uint32_t path_length, reist_vfs_shadow_object_t *object,
        x86os_file_info_t *info) {
    return reist_vfs_shadow_fat_object_open_key(io, absolute_path, path_length,
                                               object, info, 0);
}

int reist_vfs_shadow_fat_object_open_key(
        const reist_vfs_shadow_io_t *io, const char *absolute_path,
        uint32_t path_length, reist_vfs_shadow_object_t *object,
        x86os_file_info_t *info, reist_file_object_key_t *key) {
    if (key != 0) shadow_zero(key, sizeof(*key));
    if (object == 0 || info == 0) return -22;
    shadow_zero(object, sizeof(*object));
    shadow_zero(info, sizeof(*info));
    shadow_volume_t volume;
    shadow_dir_entry_t entry;
    char visible[256];
    uint32_t entry_sector = 0U;
    uint32_t entry_offset = 0U;
    uint32_t parent_cluster = 0U;
    int status = shadow_resolve(io, absolute_path, path_length, &volume,
                                &entry, visible, &entry_sector, &entry_offset,
                                &parent_cluster);
    if (status != 0) return status;
    if ((entry.bytes[11U] & FAT32_ATTR_DIRECTORY) != 0U) return -21;
    uint32_t cluster = 0U;
    if (shadow_entry_cluster(&volume, &entry, &cluster) != 0) return -5;
    uint32_t size = shadow_get32(entry.bytes + 28U);
    if ((size == 0U && cluster != 0U &&
         !shadow_cluster_valid(&volume, cluster)) ||
        (size != 0U && !shadow_cluster_valid(&volume, cluster))) return -5;
    *object = (reist_vfs_shadow_object_t){
        .version = REIST_VFS_SHADOW_OBJECT_VERSION,
        .struct_size = sizeof(*object),
        .filesystem = REIST_VFS_SHADOW_OBJECT_FAT,
        .resource = volume.resource,
        .volume_signature = volume.signature,
        .locator_a = entry_sector,
        .locator_b = entry_offset,
        .locator_c = cluster,
        .object_generation = shadow_entry_generation(entry.bytes),
    };
    if (key != 0) {
        key->resource = volume.resource;
        if (volume.fat_type == FAT_TYPE_12) {
            key->kind = REIST_FILE_OBJECT_FAT12;
            key->object_a = entry_sector;
            key->object_b = entry_offset;
        } else {
            key->kind = REIST_FILE_OBJECT_FAT32;
            key->object_a = parent_cluster;
            shadow_copy(key->alias, entry.bytes, 11U);
        }
    }
    shadow_entry_info(&entry, visible, info);
    return 0;
}

int reist_vfs_shadow_fat_object_stat(
        const reist_vfs_shadow_io_t *io,
        const reist_vfs_shadow_object_t *object, x86os_file_info_t *info) {
    if (info == 0) return -22;
    shadow_zero(info, sizeof(*info));
    shadow_volume_t volume;
    int status = shadow_object_volume(io, object, &volume);
    if (status != 0) return status;
    shadow_dir_entry_t entry;
    char visible[256];
    status = shadow_object_entry(&volume, object, &entry, visible);
    if (status == 0) shadow_entry_info(&entry, visible, info);
    return status;
}

int reist_vfs_shadow_fat_object_read(
        const reist_vfs_shadow_io_t *io,
        const reist_vfs_shadow_object_t *object, uint32_t offset,
        uint8_t *data, uint32_t capacity, uint32_t *transferred) {
    if (data == 0 || transferred == 0 || capacity == 0U ||
        capacity > X86OS_STORAGE_BULK_MAX_BYTES) return -22;
    shadow_zero(data, capacity);
    *transferred = 0U;
    shadow_volume_t volume;
    int status = shadow_object_volume(io, object, &volume);
    if (status != 0) return status;
    shadow_dir_entry_t entry;
    char visible[256];
    status = shadow_object_entry(&volume, object, &entry, visible);
    if (status != 0) return status;
    status = shadow_read_file(&volume, &entry, offset, data, capacity,
                              transferred);
    if (status != 0) {
        shadow_zero(data, capacity);
        *transferred = 0U;
    }
    return status;
}
