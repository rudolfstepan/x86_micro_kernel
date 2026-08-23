/**
 * @file userspace/programs/storage_service.c
 * @brief Implementiert den isolierten Ring-3-Storage-Dienst.
 *
 * Layer: Ring-3 system program or command.
 * Contract: Argumente und SDK-Rückgaben werden vor weiteren Operationen validiert.
 * Safety: Ressourcenarbeit ist begrenzt; Fehler werden an die Shell gemeldet und nicht verschleiert.
 */
#include <stdint.h>

#include "x86os.h"

#define FORMAT32_RESERVED 32U
#define FORMAT32_BACKUP_BOOT 6U
#define FORMAT32_FSINFO 1U
#define FORMAT32_ROOT_CLUSTER 2U
#define FORMAT32_MIN_CLUSTERS 65525U
#define FORMAT32_JOURNAL_HEADER 8U
#define FORMAT32_JOURNAL_MIRROR 31U
#define FORMAT32_SCAN_CHUNK_CLUSTERS 256U
#define FORMAT32_FAT_CHUNK_SECTORS 256U
#define FORMAT32_BAD_CLUSTER 0x0FFFFFF7U

static void put16(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8U);
}

static void put32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8U);
    p[2] = (uint8_t)(value >> 16U); p[3] = (uint8_t)(value >> 24U);
}

static uint16_t get16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8U));
}

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}

static uint32_t format32_crc32(const uint8_t *data) {
    uint32_t crc = 0xFFFFFFFFU;
    for (uint32_t i = 0U; i < X86OS_STORAGE_BLOCK_SIZE; ++i) {
        crc ^= data[i];
        for (uint32_t bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return crc ^ 0xFFFFFFFFU;
}

static int format32_write(uint32_t resource, uint32_t sector,
                          const uint8_t *data) {
    /* The privileged block-write syscall performs a bounded readback and
     * byte-for-byte comparison before it reports success. */
    return x86os_storage_block_write(resource, sector, data) == 0 ? 0 : -5;
}

static int format_fat32(uint32_t resource) {
    x86os_drive_info_t drive;
    uint8_t sector[X86OS_STORAGE_BLOCK_SIZE];
    if (x86os_drive_info(resource, &drive) <= 0 ||
        drive.type != X86OS_DRIVE_PARTITION || drive.sectors < 131072U)
        return -22;
    uint32_t fat_sectors = (drive.sectors / 128U) + 1U;
    uint32_t data_start = FORMAT32_RESERVED + 2U * fat_sectors;
    if (data_start >= drive.sectors) return -22;
    uint32_t clusters = drive.sectors - data_start;
    if (clusters < FORMAT32_MIN_CLUSTERS) return -22;

    for (uint32_t i = 0U; i < sizeof(sector); ++i) sector[i] = 0U;
    sector[0] = 0xEBU; sector[1] = 0x58U; sector[2] = 0x90U;
    sector[3] = 'R'; sector[4] = 'E'; sector[5] = 'I'; sector[6] = 'S';
    put16(sector + 11U, 512U); sector[13] = 1U;
    put16(sector + 14U, FORMAT32_RESERVED); sector[16] = 2U;
    put16(sector + 17U, 0U); put16(sector + 19U, 0U);
    sector[21] = 0xF8U; put16(sector + 22U, 0U);
    put16(sector + 24U, 63U); put16(sector + 26U, 255U);
    put32(sector + 28U, 0U); put32(sector + 32U, drive.sectors);
    put32(sector + 36U, fat_sectors); put16(sector + 40U, 0U);
    put16(sector + 42U, 0U); put32(sector + 44U, FORMAT32_ROOT_CLUSTER);
    put16(sector + 48U, FORMAT32_FSINFO); put16(sector + 50U, FORMAT32_BACKUP_BOOT);
    sector[66] = 0x29U; put32(sector + 67U, 0x52454953U);
    for (uint32_t i = 0U; i < 11U; ++i) sector[71U + i] = "REIST FAT32"[i];
    for (uint32_t i = 0U; i < 8U; ++i) sector[82U + i] = "FAT32   "[i];
    sector[510] = 0x55U; sector[511] = 0xAAU;
    if (format32_write(resource, 0U, sector) != 0 ||
        format32_write(resource, FORMAT32_BACKUP_BOOT, sector) != 0) return -5;

    for (uint32_t i = 0U; i < sizeof(sector); ++i) sector[i] = 0U;
    put32(sector + 0U, 0x41615252U); put32(sector + 484U, 0x61417272U);
    put32(sector + 488U, 0xFFFFFFFFU); put32(sector + 492U, 0xFFFFFFFFU);
    put32(sector + 508U, 0xAA550000U);
    if (format32_write(resource, FORMAT32_FSINFO, sector) != 0 ||
        format32_write(resource, FORMAT32_BACKUP_BOOT + 1U, sector) != 0) return -5;

    for (uint32_t i = 0U; i < sizeof(sector); ++i) sector[i] = 0U;
    put32(sector + 0U, 0x0FFFFFF8U); put32(sector + 4U, 0x0FFFFFFFU);
    put32(sector + 8U, 0x0FFFFFFFU); put32(sector + 12U, 0x0FFFFFFFU);
    if (format32_write(resource, FORMAT32_RESERVED, sector) != 0 ||
        format32_write(resource, FORMAT32_RESERVED + fat_sectors, sector) != 0) return -5;

    /* Initialize the same redundant clean journal record consumed by the
     * kernel ATA recovery path.  The CRC covers the complete sector. */
    for (uint32_t i = 0U; i < sizeof(sector); ++i) sector[i] = 0U;
    put32(sector + 0U, 0x4A545352U); put32(sector + 4U, 2U);
    put32(sector + 8U, 0U); put32(sector + 12U, 1U);
    put32(sector + 16U, 0U); put32(sector + 20U, 0U);
    put32(sector + 20U, format32_crc32(sector));
    if (format32_write(resource, FORMAT32_JOURNAL_HEADER, sector) != 0 ||
        format32_write(resource, FORMAT32_JOURNAL_MIRROR, sector) != 0) return -5;
    for (uint32_t i = 0U; i < sizeof(sector); ++i) sector[i] = 0U;
    if (format32_write(resource, data_start, sector) != 0) return -5;
    uint32_t fingerprint = 0U;
    if (x86os_storage_block_flush(resource) != 0 ||
        x86os_storage_media_commit(resource, &fingerprint) != 0 ||
        fingerprint == 0U) return -5;
    return 0;
}

static int format_fat32_prepare(uint32_t resource, uint32_t cursor) {
    x86os_drive_info_t drive;
    uint8_t zero[X86OS_STORAGE_BLOCK_SIZE];
    if (x86os_drive_info(resource, &drive) <= 0 ||
        drive.type != X86OS_DRIVE_PARTITION || drive.sectors < 131072U)
        return -22;
    uint32_t fat_sectors = (drive.sectors / 128U) + 1U;
    uint32_t data_start = FORMAT32_RESERVED + 2U * fat_sectors;
    if (data_start >= drive.sectors || cursor > fat_sectors) return -22;
    for (uint32_t i = 0U; i < sizeof(zero); ++i) zero[i] = 0U;
    if (cursor == 0U && format32_write(resource, 0U, zero) != 0) return -5;
    uint32_t chunk_end = cursor + FORMAT32_FAT_CHUNK_SECTORS;
    if (chunk_end < cursor || chunk_end > fat_sectors) chunk_end = fat_sectors;
    for (uint32_t index = cursor; index < chunk_end; ++index) {
        if (format32_write(resource, FORMAT32_RESERVED + index, zero) != 0 ||
            format32_write(resource,
                           FORMAT32_RESERVED + fat_sectors + index,
                           zero) != 0) return -5;
    }
    if (x86os_storage_block_flush(resource) != 0) return -5;
    return chunk_end == fat_sectors ? 0 : (int)chunk_end;
}

static int format32_mark_bad(uint32_t resource, uint32_t fat_sectors,
                             uint32_t cluster) {
    uint8_t sector[X86OS_STORAGE_BLOCK_SIZE];
    uint32_t fat_sector = cluster / 128U;
    uint32_t entry_offset = (cluster % 128U) * sizeof(uint32_t);
    if (fat_sector >= fat_sectors ||
        x86os_storage_block_read(resource, FORMAT32_RESERVED + fat_sector,
                                 sector) != 0) return -5;
    put32(sector + entry_offset, FORMAT32_BAD_CLUSTER);
    if (format32_write(resource, FORMAT32_RESERVED + fat_sector, sector) != 0 ||
        format32_write(resource,
                       FORMAT32_RESERVED + fat_sectors + fat_sector,
                       sector) != 0) return -5;
    return 0;
}

static int format_fat32_scan(uint32_t resource, uint32_t start_cluster) {
    x86os_drive_info_t drive;
    uint8_t boot[X86OS_STORAGE_BLOCK_SIZE];
    if (x86os_drive_info(resource, &drive) <= 0 ||
        drive.type != X86OS_DRIVE_PARTITION || drive.sectors < 131072U ||
        x86os_storage_block_read(resource, 0U, boot) != 0) return -22;
    uint32_t fat_sectors = get32(boot + 36U);
    uint32_t data_start = FORMAT32_RESERVED + 2U * fat_sectors;
    if (get16(boot + 11U) != X86OS_STORAGE_BLOCK_SIZE || boot[13] != 1U ||
        get16(boot + 14U) != FORMAT32_RESERVED || boot[16] != 2U ||
        get32(boot + 32U) != drive.sectors || fat_sectors == 0U ||
        data_start >= drive.sectors || boot[510] != 0x55U ||
        boot[511] != 0xAAU) return -22;
    uint32_t cluster_count = drive.sectors - data_start;
    uint32_t cluster_end = cluster_count + 2U;
    if (cluster_count < FORMAT32_MIN_CLUSTERS || start_cluster < 3U ||
        start_cluster >= cluster_end) return -22;
    uint32_t chunk_end = start_cluster + FORMAT32_SCAN_CHUNK_CLUSTERS;
    if (chunk_end < start_cluster || chunk_end > cluster_end)
        chunk_end = cluster_end;

    for (uint32_t cluster = start_cluster; cluster < chunk_end; ++cluster) {
        uint32_t sector = data_start + (cluster - 2U);
        int probe = x86os_storage_format_probe(resource, sector);
        if (probe < 0) return probe;
        if (probe == 1 &&
            format32_mark_bad(resource, fat_sectors, cluster) != 0) return -5;
    }
    if (x86os_storage_block_flush(resource) != 0) return -5;
    return chunk_end == cluster_end ? 0 : (int)chunk_end;
}

#define FORMAT_FAT12_SECTORS 2880U
#define FORMAT_FAT12_JOURNAL_ENTRIES 64U
#define FORMAT_FAT12_JOURNAL_SECTORS \
    (2U + FORMAT_FAT12_JOURNAL_ENTRIES * 2U)
#define FORMAT_FAT12_REMAP_SPARES 8U
#define FORMAT_FAT12_REPLICA_SECTORS 54U
#define FORMAT_FAT12_SAFETY_SECTORS \
    (FORMAT_FAT12_JOURNAL_SECTORS + 3U + FORMAT_FAT12_REMAP_SPARES + \
     FORMAT_FAT12_REPLICA_SECTORS)
#define FORMAT_FAT12_RESERVED (1U + FORMAT_FAT12_SAFETY_SECTORS)
#define FORMAT_FAT12_LAYOUT_BASE \
    (FORMAT_FAT12_RESERVED - FORMAT_FAT12_SAFETY_SECTORS)
#define FORMAT_FAT12_REMAP_BASE \
    (FORMAT_FAT12_LAYOUT_BASE + FORMAT_FAT12_JOURNAL_SECTORS)
#define FORMAT_FAT12_FAT_SECTORS 9U
#define FORMAT_FAT12_ROOT_SECTORS 14U
#define FORMAT_FAT12_DATA_START (FORMAT_FAT12_RESERVED + \
                                 2U * FORMAT_FAT12_FAT_SECTORS + \
                                 FORMAT_FAT12_ROOT_SECTORS)
#define FORMAT_FAT12_VOLUME_ID 0x52454953U
#define FAT12_MAX_FAT_SECTORS 12U
#define FAT12_JOURNAL_MAGIC 0x524A3132U
#define FAT12_JOURNAL_VERSION 2U
#define FAT12_JOURNAL_CLEAN 0U
#define FAT12_JOURNAL_ACTIVE 1U
#define FAT12_CLUSTER_INDEX_CAPACITY 4086U
#define FAT12_MAX_DIRECTORIES 256U
#define FAT12_MAX_CHAIN_REPAIRS 128U
#define FAT12_MAX_SHORT_REPAIRS 128U
#define FAT12_MAX_LOOP_REPAIRS 128U
#define FAT12_MAX_DIRECTORY_REPAIR_SECTORS 64U

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t media_fingerprint;
    uint64_t sequence;
    uint32_t state;
    uint32_t entry_count;
    uint32_t crc32;
} format_journal_header_t;

typedef struct __attribute__((packed)) {
    uint32_t target_sector;
    uint32_t data_crc32;
    uint64_t sequence;
    uint32_t metadata_crc32;
} format_journal_entry_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t entry_size;
    uint32_t media_fingerprint;
    uint64_t sequence;
    uint32_t entry_count;
    uint32_t crc32;
} format_remap_header_t;

static void format_fill(uint8_t *data, uint8_t value, uint32_t length) {
    for (uint32_t index = 0U; index < length; ++index) data[index] = value;
}

static void format_copy(uint8_t *destination, const char *source,
                        uint32_t length) {
    for (uint32_t index = 0U; index < length; ++index)
        destination[index] = (uint8_t)source[index];
}

static int format_equal(const uint8_t *left, const uint8_t *right,
                        uint32_t length) {
    for (uint32_t index = 0U; index < length; ++index)
        if (left[index] != right[index]) return 0;
    return 1;
}

static uint32_t format_crc32(const void *data, uint32_t length) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFU;
    for (uint32_t index = 0U; index < length; ++index) {
        crc ^= bytes[index];
        for (uint32_t bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return crc ^ 0xFFFFFFFFU;
}

static void format_metadata_sector(uint8_t *sector, uint32_t sector_number) {
    format_fill(sector, 0U, X86OS_STORAGE_BLOCK_SIZE);
    if (sector_number == FORMAT_FAT12_LAYOUT_BASE ||
        sector_number == FORMAT_FAT12_LAYOUT_BASE + 1U) {
        format_journal_header_t header = {
            .magic = 0x524A3132U, .version = 2U,
            .header_size = sizeof(format_journal_header_t),
            .media_fingerprint = FORMAT_FAT12_VOLUME_ID, .sequence = 1U,
            .state = 0U, .entry_count = 0U, .crc32 = 0U
        };
        header.crc32 = format_crc32(&header, sizeof(header));
        for (uint32_t index = 0U; index < sizeof(header); ++index)
            sector[index] = ((const uint8_t *)&header)[index];
    } else if (sector_number == FORMAT_FAT12_REMAP_BASE ||
               sector_number == FORMAT_FAT12_REMAP_BASE + 1U) {
        format_remap_header_t header = {
            .magic = 0x52504D31U, .version = 1U,
            .entry_size = 16U, .media_fingerprint = FORMAT_FAT12_VOLUME_ID,
            .sequence = 1U, .entry_count = 0U, .crc32 = 0U
        };
        header.crc32 = format_crc32(&header, sizeof(header));
        for (uint32_t index = 0U; index < sizeof(header); ++index)
            sector[index] = ((const uint8_t *)&header)[index];
    }
}

static int format_write(uint32_t resource, uint32_t sector,
                        const uint8_t *data) {
    uint8_t verify[X86OS_STORAGE_BLOCK_SIZE];
    if (x86os_storage_block_write(resource, sector, data) != 0 ||
        x86os_storage_block_read(resource, sector, verify) != 0 ||
        !format_equal(data, verify, sizeof(verify))) return -84;
    return 0;
}

typedef struct {
    uint32_t reserved_sectors;
    uint32_t fat_sectors;
    uint32_t root_entries;
    uint32_t root_sectors;
    uint32_t total_sectors;
    uint32_t data_start;
    uint32_t cluster_count;
    uint32_t volume_id;
    uint8_t media;
    uint8_t sectors_per_cluster;
    uint8_t reist_layout;
} fat12_check_layout_t;

/* Fixed service-owned scratch space: FAT12 inspection never allocates and the
 * accepted FAT size is capped before either buffer is indexed. */
static uint8_t fat12_copies[2U][FAT12_MAX_FAT_SECTORS *
                                X86OS_STORAGE_BLOCK_SIZE];
static uint8_t fat12_repair_fat[FAT12_MAX_FAT_SECTORS *
                                X86OS_STORAGE_BLOCK_SIZE];
static uint16_t fat12_cluster_owner[FAT12_CLUSTER_INDEX_CAPACITY];
static uint16_t fat12_chain_seen[FAT12_CLUSTER_INDEX_CAPACITY];
static uint16_t fat12_seen_generation;

typedef struct {
    uint16_t start_cluster;
} fat12_directory_work_t;

typedef struct {
    uint16_t cut_cluster;
    uint16_t tail_cluster;
} fat12_chain_repair_t;

typedef struct {
    uint32_t directory_sector;
    uint32_t original_size;
    uint32_t repaired_size;
    uint16_t entry_offset;
    uint16_t start_cluster;
} fat12_short_repair_t;

typedef struct {
    uint16_t start_cluster;
    uint16_t expected_clusters;
    uint16_t cut_cluster;
    uint16_t tail_cluster;
} fat12_loop_repair_t;

static fat12_directory_work_t fat12_directory_queue[FAT12_MAX_DIRECTORIES];
static fat12_chain_repair_t fat12_chain_repairs[FAT12_MAX_CHAIN_REPAIRS];
static fat12_short_repair_t fat12_short_repairs[FAT12_MAX_SHORT_REPAIRS];
static fat12_loop_repair_t fat12_loop_repairs[FAT12_MAX_LOOP_REPAIRS];
static uint32_t fat12_directory_repair_sectors[
    FAT12_MAX_DIRECTORY_REPAIR_SECTORS];
static uint32_t fat12_chain_repair_count;
static uint32_t fat12_short_repair_count;
static uint32_t fat12_short_issue_count;
static uint32_t fat12_loop_repair_count;
static uint32_t fat12_loop_issue_count;
static uint32_t fat12_directory_repair_sector_count;

static int fat12_parse_layout(uint32_t resource, fat12_check_layout_t *layout) {
    x86os_drive_info_t drive;
    uint8_t boot[X86OS_STORAGE_BLOCK_SIZE];
    if (layout == 0 || x86os_drive_info(resource, &drive) <= 0 ||
        drive.type != X86OS_DRIVE_FDD || drive.sectors == 0U ||
        x86os_storage_block_read(resource, 0U, boot) != 0) return -22;

    uint32_t bytes_per_sector = get16(boot + 11U);
    uint32_t sectors_per_cluster = boot[13U];
    uint32_t reserved = get16(boot + 14U);
    uint32_t fats = boot[16U];
    uint32_t root_entries = get16(boot + 17U);
    uint32_t total = get16(boot + 19U);
    if (total == 0U) total = get32(boot + 32U);
    uint32_t fat_sectors = get16(boot + 22U);
    if (bytes_per_sector != X86OS_STORAGE_BLOCK_SIZE ||
        sectors_per_cluster == 0U || sectors_per_cluster > 128U ||
        (sectors_per_cluster & (sectors_per_cluster - 1U)) != 0U ||
        reserved == 0U || fats != 2U || root_entries == 0U ||
        total == 0U || total != drive.sectors || fat_sectors == 0U ||
        fat_sectors > FAT12_MAX_FAT_SECTORS || boot[510U] != 0x55U ||
        boot[511U] != 0xAAU) return -22;

    uint32_t root_sectors = (root_entries * 32U + 511U) / 512U;
    uint32_t fat_area = fats * fat_sectors;
    if (reserved > total || fat_area > total - reserved ||
        root_sectors > total - reserved - fat_area) return -22;
    uint32_t data_start = reserved + fat_area + root_sectors;
    uint32_t cluster_count = (total - data_start) / sectors_per_cluster;
    uint32_t fat_entries = (fat_sectors * X86OS_STORAGE_BLOCK_SIZE * 2U) / 3U;
    if (cluster_count == 0U || cluster_count >= 4085U ||
        fat_entries <= cluster_count + 1U) return -22;

    *layout = (fat12_check_layout_t){
        .reserved_sectors = reserved,
        .fat_sectors = fat_sectors,
        .root_entries = root_entries,
        .root_sectors = root_sectors,
        .total_sectors = total,
        .data_start = data_start,
        .cluster_count = cluster_count,
        .volume_id = get32(boot + 39U),
        .media = boot[21U],
        .sectors_per_cluster = (uint8_t)sectors_per_cluster,
        .reist_layout = reserved == FORMAT_FAT12_RESERVED &&
            fat_sectors == FORMAT_FAT12_FAT_SECTORS &&
            root_entries == 224U && total == FORMAT_FAT12_SECTORS &&
            sectors_per_cluster == 1U && get32(boot + 39U) != 0U &&
            format_equal(boot + 54U, (const uint8_t *)"REIST12", 7U),
    };
    return 0;
}

static uint32_t fat12_entry(const uint8_t *fat, uint32_t cluster) {
    uint32_t offset = cluster + cluster / 2U;
    uint32_t packed = (uint32_t)fat[offset] |
                      ((uint32_t)fat[offset + 1U] << 8U);
    return (cluster & 1U) != 0U ? packed >> 4U : packed & 0x0FFFU;
}

static int fat12_copy_valid(const uint8_t *fat,
                            const fat12_check_layout_t *layout) {
    uint32_t first = fat12_entry(fat, 0U);
    uint32_t second = fat12_entry(fat, 1U);
    if (first != (0x0F00U | layout->media) || second < 0x0FF8U)
        return 0;
    uint32_t last_cluster = layout->cluster_count + 1U;
    for (uint32_t cluster = 2U; cluster <= last_cluster; ++cluster) {
        uint32_t value = fat12_entry(fat, cluster);
        if (value == 0U || (value >= 2U && value <= last_cluster) ||
            value == 0x0FF7U || value >= 0x0FF8U) continue;
        return 0;
    }
    return 1;
}

static int fat12_inspect(uint32_t resource, fat12_check_layout_t *layout) {
    int result = fat12_parse_layout(resource, layout);
    if (result != 0) return result;
    uint32_t bytes = layout->fat_sectors * X86OS_STORAGE_BLOCK_SIZE;
    for (uint32_t copy = 0U; copy < 2U; ++copy) {
        uint32_t base = layout->reserved_sectors + copy * layout->fat_sectors;
        for (uint32_t sector = 0U; sector < layout->fat_sectors; ++sector) {
            if (x86os_storage_block_read(resource, base + sector,
                    fat12_copies[copy] + sector * X86OS_STORAGE_BLOCK_SIZE) != 0)
                return -5;
        }
    }
    uint32_t flags = format_equal(fat12_copies[0], fat12_copies[1], bytes)
        ? 0U : X86OS_FAT12_RESULT_MIRROR_MISMATCH;
    if (!fat12_copy_valid(fat12_copies[0], layout))
        flags |= X86OS_FAT12_RESULT_PRIMARY_INVALID;
    if (!fat12_copy_valid(fat12_copies[1], layout))
        flags |= X86OS_FAT12_RESULT_SECONDARY_INVALID;
    return (int)flags;
}

typedef struct {
    uint32_t resource;
    const fat12_check_layout_t *layout;
    uint32_t flags;
    uint32_t next_owner;
    uint32_t directory_count;
    uint32_t repair_count;
    uint32_t short_repair_count;
    uint32_t short_issue_count;
    uint32_t loop_repair_count;
    uint32_t loop_issue_count;
} fat12_scan_state_t;

static int fat12_is_eoc(uint32_t value) {
    return value >= 0x0FF8U;
}

static void fat12_set_entry(uint8_t *fat, uint32_t cluster, uint32_t value) {
    uint32_t offset = cluster + cluster / 2U;
    value &= 0x0FFFU;
    if ((cluster & 1U) == 0U) {
        fat[offset] = (uint8_t)value;
        fat[offset + 1U] = (uint8_t)((fat[offset + 1U] & 0xF0U) |
                                     (value >> 8U));
    } else {
        fat[offset] = (uint8_t)((fat[offset] & 0x0FU) | (value << 4U));
        fat[offset + 1U] = (uint8_t)(value >> 4U);
    }
}

static uint32_t fat12_allocate_owner(fat12_scan_state_t *state) {
    if (state->next_owner == 0U ||
        state->next_owner > state->layout->cluster_count) {
        state->flags |= X86OS_FAT12_RESULT_SCAN_LIMIT;
        return 0U;
    }
    return state->next_owner++;
}

static void fat12_add_repair(fat12_scan_state_t *state,
                             uint32_t cut_cluster, uint32_t tail_cluster) {
    if (state->repair_count >= FAT12_MAX_CHAIN_REPAIRS) {
        state->flags |= X86OS_FAT12_RESULT_SCAN_LIMIT;
        return;
    }
    fat12_chain_repairs[state->repair_count++] = (fat12_chain_repair_t){
        .cut_cluster = (uint16_t)cut_cluster,
        .tail_cluster = (uint16_t)tail_cluster,
    };
}

static void fat12_add_short_repair(fat12_scan_state_t *state,
        uint32_t directory_sector, uint32_t entry_offset,
        uint32_t start_cluster, uint32_t original_size,
        uint32_t repaired_size) {
    if (state->short_repair_count >= FAT12_MAX_SHORT_REPAIRS ||
        entry_offset > X86OS_STORAGE_BLOCK_SIZE - 32U ||
        repaired_size >= original_size) {
        state->flags |= X86OS_FAT12_RESULT_SCAN_LIMIT;
        return;
    }
    fat12_short_repairs[state->short_repair_count++] =
        (fat12_short_repair_t){
            .directory_sector = directory_sector,
            .original_size = original_size,
            .repaired_size = repaired_size,
            .entry_offset = (uint16_t)entry_offset,
            .start_cluster = (uint16_t)start_cluster,
        };
}

static void fat12_add_loop_repair(fat12_scan_state_t *state,
        uint32_t start_cluster, uint32_t expected_clusters,
        uint32_t cut_cluster, uint32_t tail_cluster) {
    if (state->loop_repair_count >= FAT12_MAX_LOOP_REPAIRS ||
        expected_clusters == 0U || expected_clusters > UINT16_MAX) {
        state->flags |= X86OS_FAT12_RESULT_SCAN_LIMIT;
        return;
    }
    fat12_loop_repairs[state->loop_repair_count++] =
        (fat12_loop_repair_t){
            .start_cluster = (uint16_t)start_cluster,
            .expected_clusters = (uint16_t)expected_clusters,
            .cut_cluster = (uint16_t)cut_cluster,
            .tail_cluster = (uint16_t)tail_cluster,
        };
}

static uint32_t fat12_walk_chain(fat12_scan_state_t *state,
        uint32_t start_cluster, uint32_t expected_clusters, int directory,
        uint32_t *actual_clusters_out, int *normal_end_out) {
    uint32_t local = 0U;
    if (actual_clusters_out != 0) *actual_clusters_out = 0U;
    if (normal_end_out != 0) *normal_end_out = 0;
    if (start_cluster == 0U) {
        if (directory || expected_clusters != 0U)
            local |= X86OS_FAT12_RESULT_CHAIN_SHORT;
        state->flags |= local;
        return local;
    }
    uint32_t last_cluster = state->layout->cluster_count + 1U;
    if (start_cluster < 2U || start_cluster > last_cluster) {
        local |= X86OS_FAT12_RESULT_CHAIN_INVALID;
        state->flags |= local;
        return local;
    }
    uint32_t owner = fat12_allocate_owner(state);
    if (owner == 0U) return X86OS_FAT12_RESULT_SCAN_LIMIT;
    if (++fat12_seen_generation == 0U) {
        for (uint32_t cluster = 0U; cluster <= last_cluster; ++cluster)
            fat12_chain_seen[cluster] = 0U;
        fat12_seen_generation = 1U;
    }

    uint32_t cluster = start_cluster;
    uint32_t count = 0U;
    uint32_t cut_cluster = 0U;
    uint32_t tail_cluster = 0U;
    int normal_end = 0;
    for (uint32_t steps = 0U; steps < state->layout->cluster_count; ++steps) {
        if (cluster < 2U || cluster > last_cluster) {
            local |= X86OS_FAT12_RESULT_CHAIN_INVALID;
            break;
        }
        if (fat12_chain_seen[cluster] == fat12_seen_generation) {
            local |= X86OS_FAT12_RESULT_CHAIN_LOOP;
            break;
        }
        fat12_chain_seen[cluster] = fat12_seen_generation;
        if (fat12_cluster_owner[cluster] != 0U &&
            fat12_cluster_owner[cluster] != owner)
            local |= X86OS_FAT12_RESULT_CHAIN_CROSSLINK;
        else
            fat12_cluster_owner[cluster] = (uint16_t)owner;

        ++count;
        uint32_t next = fat12_entry(fat12_copies[0], cluster);
        if (!directory && count == expected_clusters) {
            cut_cluster = cluster;
            tail_cluster = next;
        }
        if (fat12_is_eoc(next)) {
            normal_end = 1;
            break;
        }
        if (next == 0U || next == 1U || next == 0x0FF7U ||
            next < 2U || next > last_cluster) {
            local |= X86OS_FAT12_RESULT_CHAIN_INVALID;
            break;
        }
        cluster = next;
    }
    if (!normal_end && local == 0U)
        local |= X86OS_FAT12_RESULT_CHAIN_LOOP;
    if (actual_clusters_out != 0) *actual_clusters_out = count;
    if (normal_end_out != 0) *normal_end_out = normal_end;

    if (!directory) {
        if (expected_clusters == 0U && start_cluster != 0U) {
            local |= X86OS_FAT12_RESULT_CHAIN_EXCESS |
                     X86OS_FAT12_RESULT_DIRECTORY_INVALID;
        } else if (count < expected_clusters) {
            local |= X86OS_FAT12_RESULT_CHAIN_SHORT;
        } else if (normal_end && count > expected_clusters) {
            uint32_t ambiguity = X86OS_FAT12_RESULT_CHAIN_INVALID |
                X86OS_FAT12_RESULT_CHAIN_LOOP |
                X86OS_FAT12_RESULT_CHAIN_CROSSLINK;
            local |= X86OS_FAT12_RESULT_CHAIN_EXCESS;
            if ((local & ambiguity) == 0U && expected_clusters != 0U &&
                cut_cluster >= 2U && tail_cluster >= 2U &&
                tail_cluster <= last_cluster)
                fat12_add_repair(state, cut_cluster, tail_cluster);
        }
    }
    if ((local & X86OS_FAT12_RESULT_CHAIN_LOOP) != 0U)
        ++state->loop_issue_count;
    if (!directory && local == X86OS_FAT12_RESULT_CHAIN_LOOP &&
        expected_clusters != 0U && expected_clusters <= count &&
        cut_cluster >= 2U && cut_cluster <= last_cluster &&
        tail_cluster >= 2U && tail_cluster <= last_cluster)
        fat12_add_loop_repair(state, start_cluster, expected_clusters,
                              cut_cluster, tail_cluster);
    state->flags |= local;
    return local;
}

static int fat12_dot_entry(const uint8_t *entry) {
    if (entry[0] != '.') return 0;
    return entry[1] == ' ' || (entry[1] == '.' && entry[2] == ' ');
}

static void fat12_enqueue_directory(fat12_scan_state_t *state,
                                    uint32_t start_cluster) {
    if (state->directory_count >= FAT12_MAX_DIRECTORIES) {
        state->flags |= X86OS_FAT12_RESULT_SCAN_LIMIT;
        return;
    }
    fat12_directory_queue[state->directory_count++].start_cluster =
        (uint16_t)start_cluster;
}

/* Returns one after the FAT end-of-directory marker. */
static int fat12_process_directory_entry(fat12_scan_state_t *state,
        uint32_t directory_sector, uint32_t entry_offset,
        const uint8_t *entry) {
    if (entry[0] == 0U) return 1;
    if (entry[0] == 0xE5U) return 0;
    uint32_t attributes = entry[11U];
    if (attributes == 0x0FU) return 0; /* VFAT long-name slot. */
    if ((attributes & 0xC0U) != 0U ||
        (attributes & 0x18U) == 0x18U || get16(entry + 20U) != 0U) {
        state->flags |= X86OS_FAT12_RESULT_DIRECTORY_INVALID;
        return 0;
    }
    uint32_t start_cluster = get16(entry + 26U);
    uint32_t size = get32(entry + 28U);
    if ((attributes & 0x08U) != 0U) {
        if (start_cluster != 0U || size != 0U)
            state->flags |= X86OS_FAT12_RESULT_DIRECTORY_INVALID;
        return 0;
    }
    if ((attributes & 0x10U) != 0U) {
        if (fat12_dot_entry(entry)) return 0;
        if (size != 0U || start_cluster < 2U ||
            start_cluster > state->layout->cluster_count + 1U) {
            state->flags |= X86OS_FAT12_RESULT_DIRECTORY_INVALID;
            return 0;
        }
        fat12_enqueue_directory(state, start_cluster);
        return 0;
    }

    uint32_t cluster_bytes = state->layout->sectors_per_cluster *
                             X86OS_STORAGE_BLOCK_SIZE;
    uint32_t expected = size / cluster_bytes +
                        (size % cluster_bytes != 0U ? 1U : 0U);
    if (expected > state->layout->cluster_count) {
        state->flags |= X86OS_FAT12_RESULT_CHAIN_SHORT;
        ++state->short_issue_count;
        return 0;
    }
    uint32_t actual_clusters = 0U;
    int normal_end = 0;
    uint32_t local = fat12_walk_chain(state, start_cluster, expected, 0,
                                      &actual_clusters, &normal_end);
    if ((local & X86OS_FAT12_RESULT_CHAIN_SHORT) != 0U)
        ++state->short_issue_count;
    if (local == X86OS_FAT12_RESULT_CHAIN_SHORT && normal_end &&
        actual_clusters != 0U) {
        uint32_t repaired_size = actual_clusters * cluster_bytes;
        fat12_add_short_repair(state, directory_sector, entry_offset,
            start_cluster, size, repaired_size);
    }
    return 0;
}

static int fat12_scan_directory_sector(fat12_scan_state_t *state,
                                       uint32_t sector) {
    uint8_t data[X86OS_STORAGE_BLOCK_SIZE];
    if (sector >= state->layout->total_sectors ||
        x86os_storage_block_read(state->resource, sector, data) != 0)
        return -5;
    for (uint32_t offset = 0U; offset < X86OS_STORAGE_BLOCK_SIZE;
         offset += 32U)
        if (fat12_process_directory_entry(state, sector, offset,
                                          data + offset)) return 1;
    return 0;
}

static int fat12_scan_root(fat12_scan_state_t *state) {
    uint32_t root_start = state->layout->reserved_sectors +
                          2U * state->layout->fat_sectors;
    for (uint32_t sector = 0U; sector < state->layout->root_sectors; ++sector) {
        int result = fat12_scan_directory_sector(state, root_start + sector);
        if (result != 0) return result < 0 ? result : 0;
    }
    return 0;
}

static int fat12_scan_subdirectory(fat12_scan_state_t *state,
                                   uint32_t start_cluster) {
    uint32_t local = fat12_walk_chain(state, start_cluster, 0U, 1, 0, 0);
    if (local != 0U) return 0;
    uint32_t last_cluster = state->layout->cluster_count + 1U;
    uint32_t cluster = start_cluster;
    for (uint32_t steps = 0U; steps < state->layout->cluster_count; ++steps) {
        if (cluster < 2U || cluster > last_cluster) return -84;
        uint32_t first_sector = state->layout->data_start +
            (cluster - 2U) * state->layout->sectors_per_cluster;
        for (uint32_t index = 0U;
             index < state->layout->sectors_per_cluster; ++index) {
            int result = fat12_scan_directory_sector(state,
                                                       first_sector + index);
            if (result != 0) return result < 0 ? result : 0;
        }
        uint32_t next = fat12_entry(fat12_copies[0], cluster);
        if (fat12_is_eoc(next)) return 0;
        cluster = next;
    }
    return -84;
}

static int fat12_scan_chains(uint32_t resource,
                             const fat12_check_layout_t *layout) {
    uint32_t last_cluster = layout->cluster_count + 1U;
    for (uint32_t cluster = 0U; cluster <= last_cluster; ++cluster) {
        fat12_cluster_owner[cluster] = 0U;
        fat12_chain_seen[cluster] = 0U;
    }
    fat12_seen_generation = 0U;
    fat12_chain_repair_count = 0U;
    fat12_short_repair_count = 0U;
    fat12_short_issue_count = 0U;
    fat12_loop_repair_count = 0U;
    fat12_loop_issue_count = 0U;
    fat12_scan_state_t state = {
        .resource = resource,
        .layout = layout,
        .flags = 0U,
        .next_owner = 1U,
        .directory_count = 0U,
        .repair_count = 0U,
        .short_repair_count = 0U,
        .short_issue_count = 0U,
        .loop_repair_count = 0U,
        .loop_issue_count = 0U,
    };
    int result = fat12_scan_root(&state);
    if (result != 0) return result;
    for (uint32_t index = 0U; index < state.directory_count; ++index) {
        result = fat12_scan_subdirectory(&state,
            fat12_directory_queue[index].start_cluster);
        if (result != 0) return result;
    }
    for (uint32_t cluster = 2U; cluster <= last_cluster; ++cluster) {
        uint32_t value = fat12_entry(fat12_copies[0], cluster);
        if (value != 0U && value != 0x0FF7U &&
            fat12_cluster_owner[cluster] == 0U)
            state.flags |= X86OS_FAT12_RESULT_ORPHAN_CLUSTER;
    }
    fat12_chain_repair_count = state.repair_count;
    fat12_short_repair_count = state.short_repair_count;
    fat12_short_issue_count = state.short_issue_count;
    fat12_loop_repair_count = state.loop_repair_count;
    fat12_loop_issue_count = state.loop_issue_count;
    return (int)state.flags;
}

static int fat12_check_volume(uint32_t resource,
                              fat12_check_layout_t *layout) {
    int result = fat12_inspect(resource, layout);
    return result == 0 ? fat12_scan_chains(resource, layout) : result;
}

static void fat12_copy_bytes(void *destination, const void *source,
                             uint32_t length) {
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    for (uint32_t index = 0U; index < length; ++index) out[index] = in[index];
}

static int fat12_journal_header_valid(const format_journal_header_t *header,
                                      uint32_t volume_id) {
    if (header->magic != FAT12_JOURNAL_MAGIC ||
        header->version != FAT12_JOURNAL_VERSION ||
        header->header_size != sizeof(*header) ||
        header->media_fingerprint != volume_id ||
        header->state > FAT12_JOURNAL_ACTIVE ||
        header->entry_count > FORMAT_FAT12_JOURNAL_ENTRIES) return 0;
    format_journal_header_t copy = *header;
    uint32_t expected = copy.crc32;
    copy.crc32 = 0U;
    return expected == format_crc32(&copy, sizeof(copy));
}

static void fat12_prepare_journal_header(format_journal_header_t *header,
        uint32_t volume_id, uint64_t sequence, uint32_t state,
        uint32_t entry_count) {
    *header = (format_journal_header_t){
        .magic = FAT12_JOURNAL_MAGIC,
        .version = FAT12_JOURNAL_VERSION,
        .header_size = sizeof(*header),
        .media_fingerprint = volume_id,
        .sequence = sequence,
        .state = state,
        .entry_count = entry_count,
        .crc32 = 0U,
    };
    header->crc32 = format_crc32(header, sizeof(*header));
}

static int fat12_load_clean_journal(uint32_t resource,
        const fat12_check_layout_t *layout, format_journal_header_t *selected) {
    uint8_t sectors[2U][X86OS_STORAGE_BLOCK_SIZE];
    format_journal_header_t headers[2U];
    int valid[2U] = {0, 0};
    for (uint32_t copy = 0U; copy < 2U; ++copy) {
        if (x86os_storage_block_read(resource,
                FORMAT_FAT12_LAYOUT_BASE + copy, sectors[copy]) == 0) {
            fat12_copy_bytes(&headers[copy], sectors[copy], sizeof(headers[copy]));
            valid[copy] = fat12_journal_header_valid(&headers[copy],
                                                     layout->volume_id);
        }
    }
    if (!valid[0] && !valid[1]) return -84;
    if (valid[0] && valid[1] &&
        headers[0].sequence == headers[1].sequence &&
        !format_equal((const uint8_t *)&headers[0],
                      (const uint8_t *)&headers[1], sizeof(headers[0])))
        return -84;
    *selected = !valid[1] || (valid[0] &&
        headers[0].sequence >= headers[1].sequence) ? headers[0] : headers[1];
    if (selected->state != FAT12_JOURNAL_CLEAN ||
        selected->sequence == UINT64_MAX) return -84;
    return 0;
}

static int fat12_write_journal_header(uint32_t resource,
        const format_journal_header_t *header) {
    uint8_t sector[X86OS_STORAGE_BLOCK_SIZE];
    format_fill(sector, 0U, sizeof(sector));
    fat12_copy_bytes(sector, header, sizeof(*header));
    if (format_write(resource, FORMAT_FAT12_LAYOUT_BASE, sector) != 0 ||
        format_write(resource, FORMAT_FAT12_LAYOUT_BASE + 1U, sector) != 0)
        return -84;
    return x86os_storage_block_flush(resource) == 0 ? 0 : -5;
}

static int fat12_record_old_sector(uint32_t resource,
        format_journal_header_t *header, uint32_t target_sector,
        const uint8_t *old_sector) {
    uint32_t index = header->entry_count;
    if (index >= FORMAT_FAT12_JOURNAL_ENTRIES || old_sector == 0) return -28;
    format_journal_entry_t entry = {
        .target_sector = target_sector,
        .data_crc32 = format_crc32(old_sector, X86OS_STORAGE_BLOCK_SIZE),
        .sequence = header->sequence,
        .metadata_crc32 = 0U,
    };
    entry.metadata_crc32 = format_crc32(&entry, sizeof(entry));
    uint8_t metadata[X86OS_STORAGE_BLOCK_SIZE];
    format_fill(metadata, 0U, sizeof(metadata));
    fat12_copy_bytes(metadata, &entry, sizeof(entry));
    uint32_t journal_sector = FORMAT_FAT12_LAYOUT_BASE + 2U + index * 2U;
    if (format_write(resource, journal_sector, old_sector) != 0 ||
        format_write(resource, journal_sector + 1U, metadata) != 0)
        return -84;
    fat12_prepare_journal_header(header, header->media_fingerprint,
        header->sequence, FAT12_JOURNAL_ACTIVE, index + 1U);
    return fat12_write_journal_header(resource, header);
}

static int fat12_record_old_mirror(uint32_t resource,
        format_journal_header_t *header, uint32_t target_copy,
        uint32_t fat_sector, uint32_t target_sector) {
    const uint8_t *old_sector = fat12_copies[target_copy] +
        fat_sector * X86OS_STORAGE_BLOCK_SIZE;
    return fat12_record_old_sector(resource, header, target_sector,
                                   old_sector);
}

static int fat12_repair_mirror(uint32_t resource) {
    fat12_check_layout_t layout;
    int diagnosis = fat12_inspect(resource, &layout);
    if (diagnosis <= 0) return diagnosis;
    uint32_t invalid = (uint32_t)diagnosis &
        (X86OS_FAT12_RESULT_PRIMARY_INVALID |
         X86OS_FAT12_RESULT_SECONDARY_INVALID);
    if (!layout.reist_layout ||
        ((uint32_t)diagnosis & X86OS_FAT12_RESULT_MIRROR_MISMATCH) == 0U ||
        (invalid != X86OS_FAT12_RESULT_PRIMARY_INVALID &&
         invalid != X86OS_FAT12_RESULT_SECONDARY_INVALID)) return -84;

    uint32_t token = 0U;
    if (x86os_storage_maintenance_acquire(resource, 0U, &token) != 0 ||
        token == 0U) return -30;

    int result = fat12_inspect(resource, &layout);
    if (result != diagnosis || !layout.reist_layout) result = -84;
    uint32_t target = invalid == X86OS_FAT12_RESULT_PRIMARY_INVALID ? 0U : 1U;
    uint32_t source = target ^ 1U;
    format_journal_header_t journal;
    if (result >= 0 && fat12_load_clean_journal(resource, &layout, &journal) != 0)
        result = -84;
    if (result >= 0) {
        fat12_prepare_journal_header(&journal, layout.volume_id,
            journal.sequence + 1U, FAT12_JOURNAL_ACTIVE, 0U);
        result = fat12_write_journal_header(resource, &journal);
    }
    for (uint32_t sector = 0U;
         result == 0 && sector < layout.fat_sectors; ++sector) {
        uint32_t target_sector = layout.reserved_sectors +
            target * layout.fat_sectors + sector;
        result = fat12_record_old_mirror(resource, &journal, target, sector,
                                         target_sector);
        if (result == 0 &&
            x86os_storage_maintenance_renew(resource, token, 0U) != 0)
            result = -30;
    }
    for (uint32_t sector = 0U;
         result == 0 && sector < layout.fat_sectors; ++sector) {
        uint32_t target_sector = layout.reserved_sectors +
            target * layout.fat_sectors + sector;
        result = format_write(resource, target_sector,
            fat12_copies[source] + sector * X86OS_STORAGE_BLOCK_SIZE);
        if (result == 0 &&
            x86os_storage_maintenance_renew(resource, token, 0U) != 0)
            result = -30;
    }
    if (result == 0 && x86os_storage_block_flush(resource) != 0) result = -5;
    if (result == 0 && fat12_inspect(resource, &layout) != 0) result = -84;
    if (result == 0 &&
        x86os_storage_maintenance_renew(resource, token, 0U) != 0)
        result = -30;
    if (result == 0) {
        fat12_prepare_journal_header(&journal, layout.volume_id,
            journal.sequence, FAT12_JOURNAL_CLEAN, 0U);
        result = fat12_write_journal_header(resource, &journal);
    }
    if (x86os_storage_maintenance_release(resource, token) != 0 && result == 0)
        result = -5;
    return result == 0
        ? diagnosis | (int)X86OS_FAT12_RESULT_MIRROR_REPAIRED : result;
}

static int fat12_apply_chain_repairs(const fat12_check_layout_t *layout) {
    uint32_t bytes = layout->fat_sectors * X86OS_STORAGE_BLOCK_SIZE;
    fat12_copy_bytes(fat12_repair_fat, fat12_copies[0], bytes);
    uint32_t last_cluster = layout->cluster_count + 1U;
    for (uint32_t index = 0U; index < fat12_chain_repair_count; ++index) {
        uint32_t cut = fat12_chain_repairs[index].cut_cluster;
        uint32_t cluster = fat12_chain_repairs[index].tail_cluster;
        if (cut < 2U || cut > last_cluster || cluster < 2U ||
            cluster > last_cluster) return -84;
        fat12_set_entry(fat12_repair_fat, cut, 0x0FFFU);
        int normal_end = 0;
        for (uint32_t steps = 0U; steps < layout->cluster_count; ++steps) {
            if (cluster < 2U || cluster > last_cluster) return -84;
            uint32_t next = fat12_entry(fat12_repair_fat, cluster);
            fat12_set_entry(fat12_repair_fat, cluster, 0U);
            if (fat12_is_eoc(next)) {
                normal_end = 1;
                break;
            }
            if (next < 2U || next > last_cluster || next == 0x0FF7U)
                return -84;
            cluster = next;
        }
        if (!normal_end) return -84;
    }
    return 0;
}

static int fat12_repair_chains(uint32_t resource) {
    fat12_check_layout_t layout;
    int diagnosis = fat12_check_volume(resource, &layout);
    if (diagnosis != (int)X86OS_FAT12_RESULT_CHAIN_EXCESS ||
        !layout.reist_layout || fat12_chain_repair_count == 0U) return -84;

    uint32_t token = 0U;
    if (x86os_storage_maintenance_acquire(resource, 0U, &token) != 0 ||
        token == 0U) return -30;

    int result = fat12_check_volume(resource, &layout);
    if (result != diagnosis || !layout.reist_layout ||
        fat12_chain_repair_count == 0U) result = -84;
    if (result >= 0) result = fat12_apply_chain_repairs(&layout);

    format_journal_header_t journal;
    if (result == 0 && fat12_load_clean_journal(resource, &layout, &journal) != 0)
        result = -84;
    if (result == 0) {
        fat12_prepare_journal_header(&journal, layout.volume_id,
            journal.sequence + 1U, FAT12_JOURNAL_ACTIVE, 0U);
        result = fat12_write_journal_header(resource, &journal);
    }

    uint32_t changed_sectors = 0U;
    for (uint32_t copy = 0U; result == 0 && copy < 2U; ++copy) {
        for (uint32_t sector = 0U; result == 0 &&
             sector < layout.fat_sectors; ++sector) {
            const uint8_t *replacement = fat12_repair_fat +
                sector * X86OS_STORAGE_BLOCK_SIZE;
            const uint8_t *old = fat12_copies[copy] +
                sector * X86OS_STORAGE_BLOCK_SIZE;
            if (format_equal(replacement, old, X86OS_STORAGE_BLOCK_SIZE))
                continue;
            uint32_t target_sector = layout.reserved_sectors +
                copy * layout.fat_sectors + sector;
            result = fat12_record_old_mirror(resource, &journal, copy, sector,
                                             target_sector);
            if (result == 0 &&
                x86os_storage_maintenance_renew(resource, token, 0U) != 0)
                result = -30;
            ++changed_sectors;
        }
    }
    if (result == 0 && changed_sectors == 0U) result = -84;

    for (uint32_t copy = 0U; result == 0 && copy < 2U; ++copy) {
        for (uint32_t sector = 0U; result == 0 &&
             sector < layout.fat_sectors; ++sector) {
            const uint8_t *replacement = fat12_repair_fat +
                sector * X86OS_STORAGE_BLOCK_SIZE;
            const uint8_t *old = fat12_copies[copy] +
                sector * X86OS_STORAGE_BLOCK_SIZE;
            if (format_equal(replacement, old, X86OS_STORAGE_BLOCK_SIZE))
                continue;
            uint32_t target_sector = layout.reserved_sectors +
                copy * layout.fat_sectors + sector;
            result = format_write(resource, target_sector, replacement);
            if (result == 0 &&
                x86os_storage_maintenance_renew(resource, token, 0U) != 0)
                result = -30;
        }
    }
    if (result == 0 && x86os_storage_block_flush(resource) != 0) result = -5;
    if (result == 0 && fat12_check_volume(resource, &layout) != 0) result = -84;
    if (result == 0 &&
        x86os_storage_maintenance_renew(resource, token, 0U) != 0)
        result = -30;
    if (result == 0) {
        fat12_prepare_journal_header(&journal, layout.volume_id,
            journal.sequence, FAT12_JOURNAL_CLEAN, 0U);
        result = fat12_write_journal_header(resource, &journal);
    }
    if (x86os_storage_maintenance_release(resource, token) != 0 && result == 0)
        result = -5;
    return result == 0
        ? diagnosis | (int)X86OS_FAT12_RESULT_CHAINS_REPAIRED : result;
}

static int fat12_apply_loop_repairs(const fat12_check_layout_t *layout) {
    uint32_t bytes = layout->fat_sectors * X86OS_STORAGE_BLOCK_SIZE;
    fat12_copy_bytes(fat12_repair_fat, fat12_copies[0], bytes);
    uint32_t last_cluster = layout->cluster_count + 1U;
    for (uint32_t repair_index = 0U;
         repair_index < fat12_loop_repair_count; ++repair_index) {
        const fat12_loop_repair_t *repair =
            &fat12_loop_repairs[repair_index];
        if (++fat12_seen_generation == 0U) {
            for (uint32_t cluster = 0U; cluster <= last_cluster; ++cluster)
                fat12_chain_seen[cluster] = 0U;
            fat12_seen_generation = 1U;
        }

        uint32_t cluster = repair->start_cluster;
        uint32_t tail = 0U;
        for (uint32_t index = 0U; index < repair->expected_clusters; ++index) {
            if (cluster < 2U || cluster > last_cluster ||
                fat12_chain_seen[cluster] == fat12_seen_generation)
                return -84;
            fat12_chain_seen[cluster] = fat12_seen_generation;
            uint32_t next = fat12_entry(fat12_repair_fat, cluster);
            if (index + 1U == repair->expected_clusters) {
                if (cluster != repair->cut_cluster ||
                    next != repair->tail_cluster || next < 2U ||
                    next > last_cluster) return -84;
                tail = next;
                fat12_set_entry(fat12_repair_fat, cluster, 0x0FFFU);
                break;
            }
            if (next < 2U || next > last_cluster || next == 0x0FF7U ||
                fat12_is_eoc(next)) return -84;
            cluster = next;
        }

        int stopped = 0;
        cluster = tail;
        for (uint32_t steps = 0U; steps < layout->cluster_count; ++steps) {
            if (cluster < 2U || cluster > last_cluster) return -84;
            if (fat12_chain_seen[cluster] == fat12_seen_generation) {
                stopped = 1;
                break;
            }
            uint32_t next = fat12_entry(fat12_repair_fat, cluster);
            if (next == 0U) {
                stopped = 1;
                break;
            }
            if (next < 2U || next > last_cluster || next == 0x0FF7U ||
                fat12_is_eoc(next)) return -84;
            fat12_set_entry(fat12_repair_fat, cluster, 0U);
            cluster = next;
        }
        if (!stopped) return -84;
    }
    return 0;
}

static int fat12_repair_loops(uint32_t resource) {
    fat12_check_layout_t layout;
    int diagnosis = fat12_check_volume(resource, &layout);
    if (diagnosis != (int)X86OS_FAT12_RESULT_CHAIN_LOOP ||
        !layout.reist_layout || fat12_loop_repair_count == 0U ||
        fat12_loop_issue_count != fat12_loop_repair_count) return -84;

    uint32_t token = 0U;
    if (x86os_storage_maintenance_acquire(resource, 0U, &token) != 0 ||
        token == 0U) return -30;

    int result = fat12_check_volume(resource, &layout);
    if (result != diagnosis || !layout.reist_layout ||
        fat12_loop_repair_count == 0U ||
        fat12_loop_issue_count != fat12_loop_repair_count)
        result = -84;
    if (result == diagnosis) result = fat12_apply_loop_repairs(&layout);

    format_journal_header_t journal;
    if (result == 0 && fat12_load_clean_journal(resource, &layout, &journal) != 0)
        result = -84;
    if (result == 0) {
        fat12_prepare_journal_header(&journal, layout.volume_id,
            journal.sequence + 1U, FAT12_JOURNAL_ACTIVE, 0U);
        result = fat12_write_journal_header(resource, &journal);
    }

    uint32_t changed_sectors = 0U;
    for (uint32_t copy = 0U; result == 0 && copy < 2U; ++copy) {
        for (uint32_t sector = 0U; result == 0 &&
             sector < layout.fat_sectors; ++sector) {
            const uint8_t *replacement = fat12_repair_fat +
                sector * X86OS_STORAGE_BLOCK_SIZE;
            const uint8_t *old = fat12_copies[copy] +
                sector * X86OS_STORAGE_BLOCK_SIZE;
            if (format_equal(replacement, old, X86OS_STORAGE_BLOCK_SIZE))
                continue;
            uint32_t target_sector = layout.reserved_sectors +
                copy * layout.fat_sectors + sector;
            result = fat12_record_old_mirror(resource, &journal, copy, sector,
                                             target_sector);
            if (result == 0 &&
                x86os_storage_maintenance_renew(resource, token, 0U) != 0)
                result = -30;
            ++changed_sectors;
        }
    }
    if (result == 0 && changed_sectors == 0U) result = -84;

    for (uint32_t copy = 0U; result == 0 && copy < 2U; ++copy) {
        for (uint32_t sector = 0U; result == 0 &&
             sector < layout.fat_sectors; ++sector) {
            const uint8_t *replacement = fat12_repair_fat +
                sector * X86OS_STORAGE_BLOCK_SIZE;
            const uint8_t *old = fat12_copies[copy] +
                sector * X86OS_STORAGE_BLOCK_SIZE;
            if (format_equal(replacement, old, X86OS_STORAGE_BLOCK_SIZE))
                continue;
            uint32_t target_sector = layout.reserved_sectors +
                copy * layout.fat_sectors + sector;
            result = format_write(resource, target_sector, replacement);
            if (result == 0 &&
                x86os_storage_maintenance_renew(resource, token, 0U) != 0)
                result = -30;
        }
    }
    if (result == 0 && x86os_storage_block_flush(resource) != 0) result = -5;
    if (result == 0 && fat12_check_volume(resource, &layout) != 0) result = -84;
    if (result == 0 &&
        x86os_storage_maintenance_renew(resource, token, 0U) != 0)
        result = -30;
    if (result == 0) {
        fat12_prepare_journal_header(&journal, layout.volume_id,
            journal.sequence, FAT12_JOURNAL_CLEAN, 0U);
        result = fat12_write_journal_header(resource, &journal);
    }
    if (x86os_storage_maintenance_release(resource, token) != 0 && result == 0)
        result = -5;
    return result == 0
        ? diagnosis | (int)X86OS_FAT12_RESULT_LOOPS_REPAIRED : result;
}

static int fat12_apply_orphan_reclaim(const fat12_check_layout_t *layout,
                                      uint32_t *reclaimed_out) {
    uint32_t bytes = layout->fat_sectors * X86OS_STORAGE_BLOCK_SIZE;
    fat12_copy_bytes(fat12_repair_fat, fat12_copies[0], bytes);
    uint32_t reclaimed = 0U;
    uint32_t last_cluster = layout->cluster_count + 1U;
    for (uint32_t cluster = 2U; cluster <= last_cluster; ++cluster) {
        uint32_t value = fat12_entry(fat12_copies[0], cluster);
        if (value == 0U || value == 0x0FF7U ||
            fat12_cluster_owner[cluster] != 0U)
            continue;
        fat12_set_entry(fat12_repair_fat, cluster, 0U);
        ++reclaimed;
    }
    if (reclaimed_out != 0) *reclaimed_out = reclaimed;
    return reclaimed == 0U ? -84 : 0;
}

static int fat12_reclaim_orphans(uint32_t resource) {
    fat12_check_layout_t layout;
    int diagnosis = fat12_check_volume(resource, &layout);
    if (diagnosis != (int)X86OS_FAT12_RESULT_ORPHAN_CLUSTER ||
        !layout.reist_layout) return -84;

    uint32_t token = 0U;
    if (x86os_storage_maintenance_acquire(resource, 0U, &token) != 0 ||
        token == 0U) return -30;

    int result = fat12_check_volume(resource, &layout);
    if (result != diagnosis || !layout.reist_layout) result = -84;
    uint32_t reclaimed = 0U;
    if (result == diagnosis)
        result = fat12_apply_orphan_reclaim(&layout, &reclaimed);

    format_journal_header_t journal;
    if (result == 0 && fat12_load_clean_journal(resource, &layout, &journal) != 0)
        result = -84;
    if (result == 0) {
        fat12_prepare_journal_header(&journal, layout.volume_id,
            journal.sequence + 1U, FAT12_JOURNAL_ACTIVE, 0U);
        result = fat12_write_journal_header(resource, &journal);
    }

    uint32_t changed_sectors = 0U;
    for (uint32_t copy = 0U; result == 0 && copy < 2U; ++copy) {
        for (uint32_t sector = 0U; result == 0 &&
             sector < layout.fat_sectors; ++sector) {
            const uint8_t *replacement = fat12_repair_fat +
                sector * X86OS_STORAGE_BLOCK_SIZE;
            const uint8_t *old = fat12_copies[copy] +
                sector * X86OS_STORAGE_BLOCK_SIZE;
            if (format_equal(replacement, old, X86OS_STORAGE_BLOCK_SIZE))
                continue;
            uint32_t target_sector = layout.reserved_sectors +
                copy * layout.fat_sectors + sector;
            result = fat12_record_old_mirror(resource, &journal, copy, sector,
                                             target_sector);
            if (result == 0 &&
                x86os_storage_maintenance_renew(resource, token, 0U) != 0)
                result = -30;
            ++changed_sectors;
        }
    }
    if (result == 0 && (changed_sectors == 0U || reclaimed == 0U)) result = -84;

    for (uint32_t copy = 0U; result == 0 && copy < 2U; ++copy) {
        for (uint32_t sector = 0U; result == 0 &&
             sector < layout.fat_sectors; ++sector) {
            const uint8_t *replacement = fat12_repair_fat +
                sector * X86OS_STORAGE_BLOCK_SIZE;
            const uint8_t *old = fat12_copies[copy] +
                sector * X86OS_STORAGE_BLOCK_SIZE;
            if (format_equal(replacement, old, X86OS_STORAGE_BLOCK_SIZE))
                continue;
            uint32_t target_sector = layout.reserved_sectors +
                copy * layout.fat_sectors + sector;
            result = format_write(resource, target_sector, replacement);
            if (result == 0 &&
                x86os_storage_maintenance_renew(resource, token, 0U) != 0)
                result = -30;
        }
    }
    if (result == 0 && x86os_storage_block_flush(resource) != 0) result = -5;
    if (result == 0 && fat12_check_volume(resource, &layout) != 0) result = -84;
    if (result == 0 &&
        x86os_storage_maintenance_renew(resource, token, 0U) != 0)
        result = -30;
    if (result == 0) {
        fat12_prepare_journal_header(&journal, layout.volume_id,
            journal.sequence, FAT12_JOURNAL_CLEAN, 0U);
        result = fat12_write_journal_header(resource, &journal);
    }
    if (x86os_storage_maintenance_release(resource, token) != 0 && result == 0)
        result = -5;
    return result == 0
        ? diagnosis | (int)X86OS_FAT12_RESULT_ORPHANS_RECLAIMED : result;
}

static int fat12_collect_short_repair_sectors(
        const fat12_check_layout_t *layout) {
    fat12_directory_repair_sector_count = 0U;
    for (uint32_t index = 0U; index < fat12_short_repair_count; ++index) {
        uint32_t sector = fat12_short_repairs[index].directory_sector;
        if (sector >= layout->total_sectors) return -84;
        int found = 0;
        for (uint32_t known = 0U;
             known < fat12_directory_repair_sector_count; ++known) {
            if (fat12_directory_repair_sectors[known] == sector) {
                found = 1;
                break;
            }
        }
        if (found) continue;
        if (fat12_directory_repair_sector_count >=
            FAT12_MAX_DIRECTORY_REPAIR_SECTORS)
            return -28;
        fat12_directory_repair_sectors[
            fat12_directory_repair_sector_count++] = sector;
    }
    return fat12_directory_repair_sector_count == 0U ? -84 : 0;
}

static int fat12_update_short_repair_sector(uint32_t sector,
                                             uint8_t *data, int apply) {
    uint32_t matches = 0U;
    for (uint32_t index = 0U; index < fat12_short_repair_count; ++index) {
        const fat12_short_repair_t *repair = &fat12_short_repairs[index];
        if (repair->directory_sector != sector) continue;
        uint32_t offset = repair->entry_offset;
        if ((offset & 31U) != 0U ||
            offset > X86OS_STORAGE_BLOCK_SIZE - 32U) return -84;
        uint8_t *entry = data + offset;
        uint32_t attributes = entry[11U];
        if (entry[0] == 0U || entry[0] == 0xE5U || attributes == 0x0FU ||
            (attributes & 0xC0U) != 0U || (attributes & 0x18U) != 0U ||
            get16(entry + 20U) != 0U ||
            get16(entry + 26U) != repair->start_cluster ||
            get32(entry + 28U) != repair->original_size ||
            repair->repaired_size >= repair->original_size)
            return -84;
        if (apply) put32(entry + 28U, repair->repaired_size);
        ++matches;
    }
    return matches == 0U ? -84 : 0;
}

static int fat12_repair_short_files(uint32_t resource) {
    fat12_check_layout_t layout;
    int diagnosis = fat12_check_volume(resource, &layout);
    if (diagnosis != (int)X86OS_FAT12_RESULT_CHAIN_SHORT ||
        !layout.reist_layout || fat12_short_repair_count == 0U ||
        fat12_short_issue_count != fat12_short_repair_count) return -84;

    uint32_t token = 0U;
    if (x86os_storage_maintenance_acquire(resource, 0U, &token) != 0 ||
        token == 0U) return -30;

    int result = fat12_check_volume(resource, &layout);
    if (result != diagnosis || !layout.reist_layout ||
        fat12_short_repair_count == 0U ||
        fat12_short_issue_count != fat12_short_repair_count)
        result = -84;
    if (result == diagnosis)
        result = fat12_collect_short_repair_sectors(&layout);

    format_journal_header_t journal;
    if (result == 0 && fat12_load_clean_journal(resource, &layout, &journal) != 0)
        result = -84;
    if (result == 0) {
        fat12_prepare_journal_header(&journal, layout.volume_id,
            journal.sequence + 1U, FAT12_JOURNAL_ACTIVE, 0U);
        result = fat12_write_journal_header(resource, &journal);
    }

    uint8_t sector_data[X86OS_STORAGE_BLOCK_SIZE];
    for (uint32_t index = 0U;
         result == 0 && index < fat12_directory_repair_sector_count; ++index) {
        uint32_t sector = fat12_directory_repair_sectors[index];
        if (x86os_storage_block_read(resource, sector, sector_data) != 0)
            result = -5;
        if (result == 0)
            result = fat12_update_short_repair_sector(sector, sector_data, 0);
        if (result == 0)
            result = fat12_record_old_sector(resource, &journal, sector,
                                             sector_data);
        if (result == 0 &&
            x86os_storage_maintenance_renew(resource, token, 0U) != 0)
            result = -30;
    }

    for (uint32_t index = 0U;
         result == 0 && index < fat12_directory_repair_sector_count; ++index) {
        uint32_t sector = fat12_directory_repair_sectors[index];
        if (x86os_storage_block_read(resource, sector, sector_data) != 0)
            result = -5;
        if (result == 0)
            result = fat12_update_short_repair_sector(sector, sector_data, 1);
        if (result == 0) result = format_write(resource, sector, sector_data);
        if (result == 0 &&
            x86os_storage_maintenance_renew(resource, token, 0U) != 0)
            result = -30;
    }

    if (result == 0 && x86os_storage_block_flush(resource) != 0) result = -5;
    if (result == 0 && fat12_check_volume(resource, &layout) != 0) result = -84;
    if (result == 0 &&
        x86os_storage_maintenance_renew(resource, token, 0U) != 0)
        result = -30;
    if (result == 0) {
        fat12_prepare_journal_header(&journal, layout.volume_id,
            journal.sequence, FAT12_JOURNAL_CLEAN, 0U);
        result = fat12_write_journal_header(resource, &journal);
    }
    if (x86os_storage_maintenance_release(resource, token) != 0 && result == 0)
        result = -5;
    return result == 0
        ? diagnosis | (int)X86OS_FAT12_RESULT_SHORT_FILES_REPAIRED : result;
}

static void format_boot_sector(uint8_t *sector) {
    format_fill(sector, 0U, X86OS_STORAGE_BLOCK_SIZE);
    sector[0] = 0xEB; sector[1] = 0x3C; sector[2] = 0x90;
    format_copy(sector + 3, "REISTOS ", 8U);
    sector[11] = 0x00; sector[12] = 0x02; /* 512 bytes/sector */
    sector[13] = 1U;                       /* sectors/cluster */
    sector[14] = (uint8_t)(FORMAT_FAT12_RESERVED & 0xFFU);
    sector[15] = (uint8_t)(FORMAT_FAT12_RESERVED >> 8U);
    sector[16] = 2U;                       /* FAT copies */
    sector[17] = 0xE0; sector[18] = 0x00;  /* 224 root entries */
    sector[19] = 0x40; sector[20] = 0x0B;  /* 2880 sectors */
    sector[21] = 0xF0;
    sector[22] = FORMAT_FAT12_FAT_SECTORS;
    sector[24] = 18U;                      /* sectors/track */
    sector[26] = 2U;                       /* heads */
    sector[36] = 0U;                       /* floppy drive */
    sector[38] = 0x29;
    format_copy(sector + 39, "SIER", 4U);
    format_copy(sector + 43, "REIST FAT12", 11U);
    format_copy(sector + 54, "REIST12 ", 8U);
    sector[510] = 0x55; sector[511] = 0xAA;
}

static void format_fat_sector(uint8_t *sector, uint32_t fat_sector) {
    format_fill(sector, 0U, X86OS_STORAGE_BLOCK_SIZE);
    if (fat_sector == 0U) {
        sector[0] = 0xF0; sector[1] = 0xFF; sector[2] = 0xFF;
    }
}

static int format_fat12(uint32_t resource) {
    uint8_t sector[X86OS_STORAGE_BLOCK_SIZE];
    x86os_drive_info_t drive;
    if (x86os_drive_info(resource, &drive) <= 0 ||
        drive.type != X86OS_DRIVE_FDD)
        return -22;
    if (FORMAT_FAT12_RESERVED <= FORMAT_FAT12_SAFETY_SECTORS)
        return -22;

    /* Write the journal/remap reservation and filesystem contents first. */
    format_fill(sector, 0U, sizeof(sector));
    for (uint32_t index = 1U; index < FORMAT_FAT12_SECTORS; ++index) {
        if (index == FORMAT_FAT12_RESERVED || index == FORMAT_FAT12_DATA_START ||
            index == FORMAT_FAT12_LAYOUT_BASE ||
            index == FORMAT_FAT12_LAYOUT_BASE + 1U ||
            index == FORMAT_FAT12_REMAP_BASE ||
            index == FORMAT_FAT12_REMAP_BASE + 1U) {
            format_metadata_sector(sector, index);
        } else {
            format_fill(sector, 0U, sizeof(sector));
        }
        if (format_write(resource, index, sector) != 0) return -5;
    }
    for (uint32_t copy = 0U; copy < 2U; ++copy) {
        for (uint32_t index = 0U; index < FORMAT_FAT12_FAT_SECTORS; ++index) {
            format_fat_sector(sector, index);
            if (format_write(resource,
                    FORMAT_FAT12_RESERVED + copy * FORMAT_FAT12_FAT_SECTORS + index,
                    sector) != 0) return -5;
        }
    }
    format_boot_sector(sector);
    if (format_write(resource, 0U, sector) != 0) return -5;
    format_fill(sector, 0U, sizeof(sector));
    if (x86os_storage_block_read(resource, 0U, sector) != 0)
        return -5;
    uint8_t expected[X86OS_STORAGE_BLOCK_SIZE];
    format_boot_sector(expected);
    return format_equal(sector, expected, sizeof(sector)) ? 0 : -84;
}

int main(void) {
    int bind = x86os_storage_bind();
    if (bind != 0) {
        x86os_puts("STORAGE SERVICE_BIND_FAILED code=");
        x86os_print_number(bind);
        x86os_puts("\nSTORAGE is an internal service. Use svcctl list/status.\n");
        return 1;
    }
    for (;;) {
        x86os_storage_descriptor_t request;
        uint8_t data[X86OS_STORAGE_BLOCK_SIZE];
        int claim = x86os_storage_claim(&request, data);
        if (claim == -11) {
            if (x86os_sleep_ms(5U) != 0) (void)x86os_yield();
            continue;
        }
        if (claim != 0 || request.version != X86OS_STORAGE_REQUEST_VERSION ||
            request.struct_size < sizeof(request) || request.handle == 0U ||
            request.length > sizeof(data)) return 2;

        int result = -95;
        if (request.operation == X86OS_STORAGE_BLOCK_READ &&
            request.length == X86OS_STORAGE_BLOCK_SIZE) {
            result = x86os_storage_block_read(request.resource,
                                               request.offset, data);
        }
        if (request.operation == X86OS_STORAGE_BLOCK_WRITE &&
            request.length == X86OS_STORAGE_BLOCK_SIZE) {
            result = x86os_storage_block_write(request.resource,
                                                request.offset, data);
        }
        if (request.operation == X86OS_STORAGE_FORMAT_FAT12 &&
            request.length == 0U)
            result = format_fat12(request.resource);
        if (request.operation == X86OS_STORAGE_FORMAT_FAT32 &&
            request.length == 0U)
            result = format_fat32(request.resource);
        if (request.operation == X86OS_STORAGE_FORMAT_FAT32_SCAN &&
            request.length == 0U)
            result = format_fat32_scan(request.resource, request.offset);
        if (request.operation == X86OS_STORAGE_FORMAT_FAT32_PREPARE &&
            request.length == 0U)
            result = format_fat32_prepare(request.resource, request.offset);
        if (request.operation == X86OS_STORAGE_CHECK_FAT12 &&
            request.length == 0U) {
            fat12_check_layout_t layout;
            result = fat12_check_volume(request.resource, &layout);
        }
        if (request.operation == X86OS_STORAGE_REPAIR_FAT12_MIRROR &&
            request.length == 0U)
            result = fat12_repair_mirror(request.resource);
        if (request.operation == X86OS_STORAGE_REPAIR_FAT12_CHAINS &&
            request.length == 0U)
            result = fat12_repair_chains(request.resource);
        if (request.operation == X86OS_STORAGE_REPAIR_FAT12_SHORT_FILES &&
            request.length == 0U)
            result = fat12_repair_short_files(request.resource);
        if (request.operation == X86OS_STORAGE_RECLAIM_FAT12_ORPHANS &&
            request.length == 0U)
            result = fat12_reclaim_orphans(request.resource);
        if (request.operation == X86OS_STORAGE_REPAIR_FAT12_LOOPS &&
            request.length == 0U)
            result = fat12_repair_loops(request.resource);
        if (x86os_storage_complete(request.handle, result, data) != 0)
            return 3;
    }
}
