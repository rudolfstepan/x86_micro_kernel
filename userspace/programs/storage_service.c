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

#define BOOT_PARTITION_SECTORS 6144U
#define BOOT_MANIFEST_MAGIC_0 0x42363858U
#define BOOT_MANIFEST_MAGIC_1 0x32544F4FU
#define BOOT_MANIFEST_VERSION 3U
#define BOOT_MANIFEST_HEADER_SIZE 336U
#define BOOT_CONTROL_PRIMARY_LBA 97U
#define BOOT_CONTROL_SECONDARY_LBA 98U
#define BOOT_CONTROL_MAGIC_0 0x53494552U
#define BOOT_CONTROL_MAGIC_1 0x31434254U
#define BOOT_CONTROL_VERSION_V1 1U
#define BOOT_CONTROL_VERSION_V2 2U
#define BOOT_CONTROL_HEADER_SIZE 64U
#define BOOT_CONTROL_CRC_OFFSET 60U
#define BOOT_CONTROL_SLOT_A 0U
#define BOOT_CONTROL_SLOT_B 1U
#define BOOT_CONTROL_SLOT_NONE 0xFFU
#define BOOT_MANIFEST_B_LBA 96U
#define BOOT_KERNEL_A_LBA 128U
#define BOOT_KERNEL_B_LBA 3136U
#define BOOT_CONTROL_ATTEMPT_LIMIT 2U
#define BOOT_STATUS_ACK_TIMEOUT_MS 30000U

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

static uint64_t get64(const uint8_t *p) {
    return (uint64_t)get32(p) | ((uint64_t)get32(p + 4U) << 32U);
}

static void put64(uint8_t *p, uint64_t value) {
    put32(p, (uint32_t)value);
    put32(p + 4U, (uint32_t)(value >> 32U));
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
#define FAT12_REMAP_MAGIC 0x52504D31U
#define FAT12_REMAP_VERSION 1U
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
#define FAT12_MAX_DIRECTORY_LOOP_REPAIRS 128U
#define FAT12_MAX_SHORT_LOOP_REPAIRS 128U
#define FAT12_MAX_DIRECTORY_SIZE_REPAIRS 128U
#define FAT12_MAX_VOLUME_LABEL_REPAIRS 128U
#define FAT12_MAX_ZERO_FILE_REPAIRS 128U
#define FAT12_MAX_ZERO_START_REPAIRS 128U
#define FAT12_MAX_DOT_SIZE_REPAIRS 128U
#define FAT12_MAX_DOT_CLUSTER_REPAIRS 128U
#define FAT12_MAX_DIRECTORY_REPAIR_SECTORS 64U
#define FAT12_MAX_REQUIRED_CROSSLINK_FILES 128U
#define FAT12_MAX_EMPTY_DIRECTORY_CROSSLINKS 128U
#define FAT12_MAX_DIRECTORY_TOPOLOGY_REPAIRS 128U
#define FAT12_MAX_LFN_SLOTS 20U
#define FAT12_MAX_ORPHAN_CHAINS 128U
#define FAT12_MAX_SALVAGE_SLOTS (FAT12_MAX_ORPHAN_CHAINS + 1U)
#define FAT12_MAX_SALVAGE_SECTORS FORMAT_FAT12_JOURNAL_ENTRIES
#define FAT12_MAX_CLONE_CLUSTERS 48U

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

typedef struct __attribute__((packed)) {
    uint32_t bad_sector;
    uint32_t replacement_sector;
    uint64_t sequence;
} format_remap_entry_t;

typedef struct {
    format_remap_header_t header;
    format_remap_entry_t entries[FORMAT_FAT12_REMAP_SPARES];
} fat12_remap_state_t;

_Static_assert(sizeof(format_remap_entry_t) == 16U,
               "FAT12 remap entry ABI drift");

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

typedef struct {
    uint64_t sequence;
    uint32_t successful_mask;
    uint32_t version;
    uint8_t active_slot;
    uint8_t pending_slot;
    uint8_t attempts_remaining;
    uint8_t source_copy;
} boot_control_state_t;

typedef struct {
    boot_control_state_t selected;
    uint32_t first_lba;
    uint32_t second_lba;
    uint8_t selected_copy;
    uint8_t mirrored;
} boot_control_selection_t;

/*
 * The storage service is single-threaded.  Keep the bounded boot-control
 * workspace service-owned so acknowledgement cannot consume several sectors
 * of the small Ring-3 stack after compiler inlining.
 */
static uint8_t boot_manifest_sector[X86OS_STORAGE_BLOCK_SIZE];
static uint8_t boot_control_sectors[2][X86OS_STORAGE_BLOCK_SIZE];
static uint8_t boot_control_checked[X86OS_STORAGE_BLOCK_SIZE];
static uint8_t boot_control_confirmation[X86OS_STORAGE_BLOCK_SIZE];
static uint8_t boot_control_verify[X86OS_STORAGE_BLOCK_SIZE];

static int boot_bytes_zero(const uint8_t *data, uint32_t length) {
    for (uint32_t index = 0U; index < length; ++index)
        if (data[index] != 0U) return 0;
    return 1;
}

static int boot_manifest_valid(const uint8_t *sector,
                               uint32_t expected_kernel_lba) {
    if (get32(sector) != BOOT_MANIFEST_MAGIC_0 ||
        get32(sector + 4U) != BOOT_MANIFEST_MAGIC_1 ||
        get32(sector + 8U) != BOOT_MANIFEST_VERSION ||
        get32(sector + 12U) != BOOT_MANIFEST_HEADER_SIZE ||
        get32(sector + 16U) != 1U || get32(sector + 20U) == 0U ||
        get32(sector + 20U) > 64U ||
        get32(sector + 24U) != expected_kernel_lba ||
        get32(sector + 32U) != BOOT_PARTITION_SECTORS ||
        get32(sector + 40U) != 1U) return 0;
    uint32_t checksum = 0U;
    for (uint32_t offset = 0U; offset < X86OS_STORAGE_BLOCK_SIZE;
         offset += 4U) checksum += get32(sector + offset);
    return checksum == 0U;
}

static int boot_control_parse(const uint8_t *sector, uint8_t source_copy,
                              boot_control_state_t *state) {
    if (sector == 0 || state == 0 ||
        get32(sector) != BOOT_CONTROL_MAGIC_0 ||
        get32(sector + 4U) != BOOT_CONTROL_MAGIC_1 ||
        (get32(sector + 8U) != BOOT_CONTROL_VERSION_V1 &&
         get32(sector + 8U) != BOOT_CONTROL_VERSION_V2) ||
        get32(sector + 12U) != BOOT_CONTROL_HEADER_SIZE ||
        !boot_bytes_zero(sector + 32U, BOOT_CONTROL_CRC_OFFSET - 32U) ||
        !boot_bytes_zero(sector + BOOT_CONTROL_HEADER_SIZE,
                         X86OS_STORAGE_BLOCK_SIZE - BOOT_CONTROL_HEADER_SIZE))
        return -84;
    for (uint32_t index = 0U; index < sizeof(boot_control_checked); ++index)
        boot_control_checked[index] = sector[index];
    uint32_t expected_crc =
        get32(boot_control_checked + BOOT_CONTROL_CRC_OFFSET);
    put32(boot_control_checked + BOOT_CONTROL_CRC_OFFSET, 0U);
    if (expected_crc != format_crc32(boot_control_checked,
                                     sizeof(boot_control_checked))) return -84;

    uint64_t sequence = get64(sector + 16U);
    uint32_t version = get32(sector + 8U);
    uint8_t active = sector[24U];
    uint8_t pending = sector[25U];
    uint8_t attempts = sector[26U];
    uint8_t limit = sector[27U];
    uint32_t successful = get32(sector + 28U);
    if (sequence == 0U || active > BOOT_CONTROL_SLOT_B ||
        (pending != BOOT_CONTROL_SLOT_NONE && pending > BOOT_CONTROL_SLOT_B) ||
        limit != BOOT_CONTROL_ATTEMPT_LIMIT || attempts > limit ||
        (successful & ~0x03U) != 0U ||
        (successful & (1U << active)) == 0U) return -84;
    if ((pending == BOOT_CONTROL_SLOT_NONE && attempts != 0U) ||
        (version == BOOT_CONTROL_VERSION_V1 &&
         (pending == BOOT_CONTROL_SLOT_A ||
          (pending == BOOT_CONTROL_SLOT_B && active != BOOT_CONTROL_SLOT_A))) ||
        (version == BOOT_CONTROL_VERSION_V2 &&
         pending != BOOT_CONTROL_SLOT_NONE && pending == active))
        return -84;
    *state = (boot_control_state_t){
        .sequence = sequence,
        .successful_mask = successful,
        .version = version,
        .active_slot = active,
        .pending_slot = pending,
        .attempts_remaining = attempts,
        .source_copy = source_copy,
    };
    return 0;
}

static int boot_control_reconcile(
        const uint8_t sectors[2][X86OS_STORAGE_BLOCK_SIZE],
        boot_control_selection_t *selection) {
    boot_control_state_t states[2];
    int valid[2] = {
        boot_control_parse(sectors[0], 0U, &states[0]) == 0,
        boot_control_parse(sectors[1], 1U, &states[1]) == 0,
    };
    if (!valid[0] && !valid[1]) return -84;
    uint8_t selected = valid[0] ? 0U : 1U;
    uint8_t first = valid[0] ? 1U : 0U;
    uint8_t mirrored = 0U;
    if (valid[0] && valid[1]) {
        if (states[0].sequence == states[1].sequence) {
            if (!format_equal(sectors[0], sectors[1],
                              X86OS_STORAGE_BLOCK_SIZE)) return -84;
            selected = 0U;
            first = 1U;
            mirrored = 1U;
        } else if (states[0].sequence != UINT64_MAX &&
                   states[0].sequence + 1U == states[1].sequence) {
            selected = 1U;
            first = 0U;
        } else if (states[1].sequence != UINT64_MAX &&
                   states[1].sequence + 1U == states[0].sequence) {
            selected = 0U;
            first = 1U;
        } else {
            return -84;
        }
    }
    selection->selected = states[selected];
    selection->selected_copy = selected;
    selection->first_lba = first == 0U ? BOOT_CONTROL_PRIMARY_LBA :
                                         BOOT_CONTROL_SECONDARY_LBA;
    selection->second_lba = selected == 0U ? BOOT_CONTROL_PRIMARY_LBA :
                                             BOOT_CONTROL_SECONDARY_LBA;
    selection->mirrored = mirrored;
    return 0;
}

static int boot_control_read_pair(
        uint32_t resource,
        uint8_t sectors[2][X86OS_STORAGE_BLOCK_SIZE],
        boot_control_selection_t *selection) {
    if (x86os_storage_block_read(resource, BOOT_CONTROL_PRIMARY_LBA,
                                  sectors[0]) != 0 ||
        x86os_storage_block_read(resource, BOOT_CONTROL_SECONDARY_LBA,
                                  sectors[1]) != 0) return -5;
    return boot_control_reconcile(
        (const uint8_t (*)[X86OS_STORAGE_BLOCK_SIZE])sectors, selection);
}

static int boot_control_write_copy(uint32_t resource, uint32_t lba,
                                   const uint8_t *record) {
    if (x86os_storage_block_write(resource, lba, record) != 0 ||
        x86os_storage_block_flush(resource) != 0 ||
        x86os_storage_block_read(resource, lba, boot_control_verify) != 0 ||
        !format_equal(record, boot_control_verify,
                      sizeof(boot_control_verify))) return -84;
    return 0;
}

static __attribute__((noinline)) int boot_confirm_pending(
        const x86os_boot_status_t *status) {
    if (status == 0 || status->version != X86OS_BOOT_STATUS_VERSION ||
        status->struct_size != sizeof(*status) || status->reserved != 0U ||
        (status->flags & X86OS_BOOT_STATUS_SYSTEM_READY) == 0U ||
        (status->flags & ~(X86OS_BOOT_STATUS_SYSTEM_READY |
                           X86OS_BOOT_STATUS_PENDING_TRIAL)) != 0U ||
        status->partition_sectors != BOOT_PARTITION_SECTORS ||
        status->selected_slot > BOOT_CONTROL_SLOT_B ||
        status->active_slot > BOOT_CONTROL_SLOT_B ||
        (status->pending_slot != BOOT_CONTROL_SLOT_NONE &&
         status->pending_slot > BOOT_CONTROL_SLOT_B)) return -22;

    x86os_drive_info_t drive;
    if (x86os_drive_info(status->resource, &drive) <= 0 ||
        drive.type != X86OS_DRIVE_PARTITION ||
        drive.sectors != status->partition_sectors ||
        x86os_storage_block_read(
            status->resource,
            status->selected_slot == BOOT_CONTROL_SLOT_B ?
                BOOT_MANIFEST_B_LBA : 0U,
                                 boot_manifest_sector) != 0 ||
        !boot_manifest_valid(
            boot_manifest_sector,
            status->selected_slot == BOOT_CONTROL_SLOT_B ?
                BOOT_KERNEL_B_LBA : BOOT_KERNEL_A_LBA)) return -19;

    boot_control_selection_t selected;
    if (boot_control_read_pair(status->resource, boot_control_sectors,
                               &selected) != 0)
        return -84;
    if (status->sequence != selected.selected.sequence ||
        status->active_slot != selected.selected.active_slot ||
        status->pending_slot != selected.selected.pending_slot ||
        status->attempts_remaining != selected.selected.attempts_remaining)
        return -116;

    if ((status->flags & X86OS_BOOT_STATUS_PENDING_TRIAL) == 0U) {
        if (status->pending_slot != BOOT_CONTROL_SLOT_NONE) return -22;
        if (!selected.mirrored) {
            const uint8_t *record =
                boot_control_sectors[selected.selected_copy];
            if (boot_control_write_copy(status->resource, selected.first_lba,
                                        record) != 0 ||
                boot_control_write_copy(status->resource, selected.second_lba,
                                        record) != 0) return -84;
        }
        return 0;
    }
    if (status->pending_slot > BOOT_CONTROL_SLOT_B ||
        status->selected_slot != status->pending_slot ||
        status->active_slot == status->pending_slot ||
        selected.selected.sequence == UINT64_MAX) return -22;

    for (uint32_t index = 0U; index < sizeof(boot_control_confirmation);
         ++index)
        boot_control_confirmation[index] =
            boot_control_sectors[selected.selected_copy][index];
    put64(boot_control_confirmation + 16U, selected.selected.sequence + 1U);
    boot_control_confirmation[24U] = status->pending_slot;
    boot_control_confirmation[25U] = BOOT_CONTROL_SLOT_NONE;
    boot_control_confirmation[26U] = 0U;
    uint32_t successful_mask = selected.selected.successful_mask;
    successful_mask |= 1U << status->pending_slot;
    put32(boot_control_confirmation + 28U, successful_mask);
    put32(boot_control_confirmation + BOOT_CONTROL_CRC_OFFSET, 0U);
    put32(boot_control_confirmation + BOOT_CONTROL_CRC_OFFSET,
          format_crc32(boot_control_confirmation,
                       sizeof(boot_control_confirmation)));

    if (boot_control_write_copy(status->resource, selected.first_lba,
                                boot_control_confirmation) != 0 ||
        boot_control_write_copy(status->resource, selected.second_lba,
                                boot_control_confirmation) != 0) return -84;

    boot_control_selection_t verified;
    if (boot_control_read_pair(status->resource, boot_control_sectors,
                               &verified) != 0 ||
        !verified.mirrored ||
        verified.selected.sequence != selected.selected.sequence + 1U ||
        verified.selected.active_slot != status->pending_slot ||
        verified.selected.pending_slot != BOOT_CONTROL_SLOT_NONE ||
        verified.selected.attempts_remaining != 0U ||
        (verified.selected.successful_mask &
         (1U << status->pending_slot)) == 0U) return -84;
    return 0;
}

static __attribute__((noinline)) void boot_success_ack_poll(
        uint32_t deadline_ms, uint8_t *active) {
    if (active == 0 || *active == 0U) return;
    if ((int32_t)(x86os_uptime_ms() - deadline_ms) >= 0) {
        *active = 0U;
        return;
    }

    x86os_boot_status_t status = {0};
    int result = x86os_boot_status(&status);
    if (result == -11) return;
    *active = 0U;
    if (result == 0) (void)boot_confirm_pending(&status);
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
static uint16_t fat12_cluster_references[FAT12_CLUSTER_INDEX_CAPACITY];
static uint16_t fat12_cluster_required[FAT12_CLUSTER_INDEX_CAPACITY];
static uint16_t fat12_chain_seen[FAT12_CLUSTER_INDEX_CAPACITY];
static uint16_t fat12_seen_generation;

typedef struct {
    uint32_t parent_directory_sector;
    uint16_t parent_entry_offset;
    uint16_t start_cluster;
    uint16_t parent_cluster;
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

typedef struct {
    uint16_t start_cluster;
    uint16_t cut_cluster;
    uint16_t tail_cluster;
} fat12_directory_loop_repair_t;

typedef struct {
    uint32_t directory_sector;
    uint32_t original_size;
    uint32_t repaired_size;
    uint16_t entry_offset;
    uint16_t start_cluster;
    uint16_t unique_clusters;
    uint16_t cut_cluster;
    uint16_t tail_cluster;
} fat12_short_loop_repair_t;

typedef struct {
    uint32_t directory_sector;
    uint32_t original_size;
    uint16_t entry_offset;
    uint16_t start_cluster;
} fat12_directory_size_repair_t;

typedef struct {
    uint32_t directory_sector;
    uint32_t original_size;
    uint16_t entry_offset;
    uint16_t original_start_cluster;
} fat12_volume_label_repair_t;

typedef struct {
    uint32_t directory_sector;
    uint16_t entry_offset;
    uint16_t start_cluster;
    uint16_t cluster_count;
} fat12_zero_file_repair_t;

typedef struct {
    uint32_t directory_sector;
    uint32_t original_size;
    uint16_t entry_offset;
} fat12_zero_start_repair_t;

typedef struct {
    uint32_t directory_sector;
    uint32_t original_size;
    uint16_t entry_offset;
    uint16_t expected_start_cluster;
    uint8_t dot_kind;
} fat12_dot_size_repair_t;

typedef struct {
    uint32_t directory_sector;
    uint16_t entry_offset;
    uint16_t original_start_cluster;
    uint16_t expected_start_cluster;
    uint8_t dot_kind;
} fat12_dot_cluster_repair_t;

typedef struct {
    uint32_t directory_sector;
    uint32_t original_size;
    uint16_t entry_offset;
    uint16_t start_cluster;
    uint16_t cluster_count;
    uint16_t owner;
    uint16_t replacement_start;
} fat12_required_crosslink_file_t;

typedef struct {
    uint32_t parent_directory_sector;
    uint16_t parent_entry_offset;
    uint16_t start_cluster;
    uint16_t parent_cluster;
    uint16_t replacement_start;
} fat12_empty_directory_crosslink_t;

typedef struct {
    uint32_t sector;
    uint32_t crc32;
    uint16_t offset;
} fat12_directory_slot_t;

typedef struct {
    uint32_t parent_directory_sector;
    uint32_t short_crc32;
    uint16_t parent_entry_offset;
    uint16_t start_cluster;
    uint16_t parent_cluster;
    uint16_t lfn_count;
    fat12_directory_slot_t lfn[FAT12_MAX_LFN_SLOTS];
} fat12_directory_topology_repair_t;

typedef struct {
    uint16_t start_cluster;
    uint16_t cluster_count;
} fat12_orphan_chain_t;

typedef struct {
    uint32_t sector;
    uint16_t offset;
} fat12_salvage_slot_t;

typedef struct {
    uint32_t sector;
    uint32_t original_crc32;
    uint8_t new_directory_data;
    uint8_t data[X86OS_STORAGE_BLOCK_SIZE];
} fat12_salvage_sector_t;

static fat12_directory_work_t fat12_directory_queue[FAT12_MAX_DIRECTORIES];
static fat12_chain_repair_t fat12_chain_repairs[FAT12_MAX_CHAIN_REPAIRS];
static fat12_short_repair_t fat12_short_repairs[FAT12_MAX_SHORT_REPAIRS];
static fat12_loop_repair_t fat12_loop_repairs[FAT12_MAX_LOOP_REPAIRS];
static fat12_directory_loop_repair_t fat12_directory_loop_repairs[
    FAT12_MAX_DIRECTORY_LOOP_REPAIRS];
static fat12_short_loop_repair_t fat12_short_loop_repairs[
    FAT12_MAX_SHORT_LOOP_REPAIRS];
static fat12_directory_size_repair_t fat12_directory_size_repairs[
    FAT12_MAX_DIRECTORY_SIZE_REPAIRS];
static fat12_volume_label_repair_t fat12_volume_label_repairs[
    FAT12_MAX_VOLUME_LABEL_REPAIRS];
static fat12_zero_file_repair_t fat12_zero_file_repairs[
    FAT12_MAX_ZERO_FILE_REPAIRS];
static fat12_zero_start_repair_t fat12_zero_start_repairs[
    FAT12_MAX_ZERO_START_REPAIRS];
static fat12_dot_size_repair_t fat12_dot_size_repairs[
    FAT12_MAX_DOT_SIZE_REPAIRS];
static fat12_dot_cluster_repair_t fat12_dot_cluster_repairs[
    FAT12_MAX_DOT_CLUSTER_REPAIRS];
static fat12_required_crosslink_file_t fat12_required_crosslink_files[
    FAT12_MAX_REQUIRED_CROSSLINK_FILES];
static fat12_empty_directory_crosslink_t fat12_empty_directory_crosslinks[
    FAT12_MAX_EMPTY_DIRECTORY_CROSSLINKS];
static fat12_directory_topology_repair_t fat12_directory_topology_repairs[
    FAT12_MAX_DIRECTORY_TOPOLOGY_REPAIRS];
static fat12_orphan_chain_t fat12_orphan_chains[FAT12_MAX_ORPHAN_CHAINS];
static fat12_salvage_slot_t fat12_salvage_deleted_slots[
    FAT12_MAX_ORPHAN_CHAINS];
static fat12_salvage_slot_t fat12_salvage_end_slots[
    FAT12_MAX_SALVAGE_SLOTS];
static fat12_salvage_slot_t fat12_salvage_targets[
    FAT12_MAX_ORPHAN_CHAINS];
static fat12_salvage_sector_t fat12_salvage_sectors[
    FAT12_MAX_SALVAGE_SECTORS];
static uint16_t fat12_clone_source[FAT12_MAX_CLONE_CLUSTERS];
static uint16_t fat12_clone_destination[FAT12_MAX_CLONE_CLUSTERS];
static uint8_t fat12_clone_claimed[FAT12_CLUSTER_INDEX_CAPACITY];
static uint16_t fat12_regular_references[FAT12_CLUSTER_INDEX_CAPACITY];
static uint16_t fat12_clone_remaining[FAT12_CLUSTER_INDEX_CAPACITY];
static uint32_t fat12_directory_repair_sectors[
    FAT12_MAX_DIRECTORY_REPAIR_SECTORS];
static uint32_t fat12_chain_repair_count;
static uint32_t fat12_short_repair_count;
static uint32_t fat12_short_issue_count;
static uint32_t fat12_loop_repair_count;
static uint32_t fat12_loop_issue_count;
static uint32_t fat12_directory_loop_repair_count;
static uint32_t fat12_short_loop_repair_count;
static uint32_t fat12_directory_repair_sector_count;
static uint32_t fat12_excess_issue_count;
static uint32_t fat12_directory_size_repair_count;
static uint32_t fat12_volume_label_repair_count;
static uint32_t fat12_zero_file_repair_count;
static uint32_t fat12_zero_start_repair_count;
static uint32_t fat12_dot_size_repair_count;
static uint32_t fat12_dot_cluster_repair_count;
static uint32_t fat12_required_crosslink_file_count;
static uint32_t fat12_required_crosslink_file_overflow;
static uint32_t fat12_empty_directory_crosslink_count;
static uint32_t fat12_empty_directory_crosslink_overflow;
static uint32_t fat12_directory_count;
static uint32_t fat12_directory_topology_repair_count;
static uint32_t fat12_orphan_chain_count;
static uint32_t fat12_salvage_sector_count;
static uint32_t fat12_clone_cluster_count;
static uint32_t fat12_directory_invalid_issue_count;

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
    uint32_t directory_loop_repair_count;
    uint32_t short_loop_repair_count;
    uint32_t excess_issue_count;
    uint32_t directory_size_repair_count;
    uint32_t volume_label_repair_count;
    uint32_t zero_file_repair_count;
    uint32_t zero_start_repair_count;
    uint32_t dot_size_repair_count;
    uint32_t dot_cluster_repair_count;
    uint32_t required_crosslink_file_count;
    uint32_t required_crosslink_file_overflow;
    uint32_t empty_directory_crosslink_count;
    uint32_t empty_directory_crosslink_overflow;
    uint32_t directory_invalid_issue_count;
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

static void fat12_add_directory_loop_repair(fat12_scan_state_t *state,
        uint32_t start_cluster, uint32_t cut_cluster,
        uint32_t tail_cluster) {
    if (state->directory_loop_repair_count >=
        FAT12_MAX_DIRECTORY_LOOP_REPAIRS) {
        state->flags |= X86OS_FAT12_RESULT_SCAN_LIMIT;
        return;
    }
    fat12_directory_loop_repairs[state->directory_loop_repair_count++] =
        (fat12_directory_loop_repair_t){
            .start_cluster = (uint16_t)start_cluster,
            .cut_cluster = (uint16_t)cut_cluster,
            .tail_cluster = (uint16_t)tail_cluster,
        };
}

static void fat12_add_short_loop_repair(fat12_scan_state_t *state,
        uint32_t directory_sector, uint32_t entry_offset,
        uint32_t start_cluster, uint32_t unique_clusters,
        uint32_t cut_cluster, uint32_t tail_cluster,
        uint32_t original_size, uint32_t repaired_size) {
    if (state->short_loop_repair_count >= FAT12_MAX_SHORT_LOOP_REPAIRS ||
        entry_offset > X86OS_STORAGE_BLOCK_SIZE - 32U ||
        unique_clusters == 0U || unique_clusters > UINT16_MAX ||
        repaired_size >= original_size) {
        state->flags |= X86OS_FAT12_RESULT_SCAN_LIMIT;
        return;
    }
    fat12_short_loop_repairs[state->short_loop_repair_count++] =
        (fat12_short_loop_repair_t){
            .directory_sector = directory_sector,
            .original_size = original_size,
            .repaired_size = repaired_size,
            .entry_offset = (uint16_t)entry_offset,
            .start_cluster = (uint16_t)start_cluster,
            .unique_clusters = (uint16_t)unique_clusters,
            .cut_cluster = (uint16_t)cut_cluster,
            .tail_cluster = (uint16_t)tail_cluster,
        };
}

static void fat12_mark_directory_invalid(fat12_scan_state_t *state) {
    state->flags |= X86OS_FAT12_RESULT_DIRECTORY_INVALID;
    ++state->directory_invalid_issue_count;
}

static void fat12_add_directory_size_repair(fat12_scan_state_t *state,
        uint32_t directory_sector, uint32_t entry_offset,
        uint32_t start_cluster, uint32_t original_size) {
    if (state->directory_size_repair_count >=
            FAT12_MAX_DIRECTORY_SIZE_REPAIRS ||
        entry_offset > X86OS_STORAGE_BLOCK_SIZE - 32U ||
        original_size == 0U) {
        state->flags |= X86OS_FAT12_RESULT_SCAN_LIMIT;
        return;
    }
    fat12_directory_size_repairs[state->directory_size_repair_count++] =
        (fat12_directory_size_repair_t){
            .directory_sector = directory_sector,
            .original_size = original_size,
            .entry_offset = (uint16_t)entry_offset,
            .start_cluster = (uint16_t)start_cluster,
        };
}

static void fat12_add_volume_label_repair(fat12_scan_state_t *state,
        uint32_t directory_sector, uint32_t entry_offset,
        uint32_t original_start_cluster, uint32_t original_size) {
    if (state->volume_label_repair_count >=
            FAT12_MAX_VOLUME_LABEL_REPAIRS ||
        entry_offset > X86OS_STORAGE_BLOCK_SIZE - 32U ||
        (original_start_cluster == 0U && original_size == 0U)) {
        state->flags |= X86OS_FAT12_RESULT_SCAN_LIMIT;
        return;
    }
    fat12_volume_label_repairs[state->volume_label_repair_count++] =
        (fat12_volume_label_repair_t){
            .directory_sector = directory_sector,
            .original_size = original_size,
            .entry_offset = (uint16_t)entry_offset,
            .original_start_cluster = (uint16_t)original_start_cluster,
        };
}

static void fat12_add_zero_file_repair(fat12_scan_state_t *state,
        uint32_t directory_sector, uint32_t entry_offset,
        uint32_t start_cluster, uint32_t cluster_count) {
    if (state->zero_file_repair_count >= FAT12_MAX_ZERO_FILE_REPAIRS ||
        entry_offset > X86OS_STORAGE_BLOCK_SIZE - 32U ||
        start_cluster < 2U || start_cluster > UINT16_MAX ||
        cluster_count == 0U || cluster_count > UINT16_MAX) {
        state->flags |= X86OS_FAT12_RESULT_SCAN_LIMIT;
        return;
    }
    fat12_zero_file_repairs[state->zero_file_repair_count++] =
        (fat12_zero_file_repair_t){
            .directory_sector = directory_sector,
            .entry_offset = (uint16_t)entry_offset,
            .start_cluster = (uint16_t)start_cluster,
            .cluster_count = (uint16_t)cluster_count,
        };
}

static void fat12_add_zero_start_repair(fat12_scan_state_t *state,
        uint32_t directory_sector, uint32_t entry_offset,
        uint32_t original_size) {
    if (state->zero_start_repair_count >= FAT12_MAX_ZERO_START_REPAIRS ||
        entry_offset > X86OS_STORAGE_BLOCK_SIZE - 32U ||
        original_size == 0U) {
        state->flags |= X86OS_FAT12_RESULT_SCAN_LIMIT;
        return;
    }
    fat12_zero_start_repairs[state->zero_start_repair_count++] =
        (fat12_zero_start_repair_t){
            .directory_sector = directory_sector,
            .original_size = original_size,
            .entry_offset = (uint16_t)entry_offset,
        };
}

static void fat12_add_dot_size_repair(fat12_scan_state_t *state,
        uint32_t directory_sector, uint32_t entry_offset,
        uint32_t expected_start_cluster, uint32_t original_size,
        uint32_t dot_kind) {
    if (state->dot_size_repair_count >= FAT12_MAX_DOT_SIZE_REPAIRS ||
        entry_offset > X86OS_STORAGE_BLOCK_SIZE - 32U ||
        expected_start_cluster > UINT16_MAX || original_size == 0U ||
        (dot_kind != 1U && dot_kind != 2U)) {
        state->flags |= X86OS_FAT12_RESULT_SCAN_LIMIT;
        return;
    }
    fat12_dot_size_repairs[state->dot_size_repair_count++] =
        (fat12_dot_size_repair_t){
            .directory_sector = directory_sector,
            .original_size = original_size,
            .entry_offset = (uint16_t)entry_offset,
            .expected_start_cluster = (uint16_t)expected_start_cluster,
            .dot_kind = (uint8_t)dot_kind,
        };
}

static void fat12_add_dot_cluster_repair(fat12_scan_state_t *state,
        uint32_t directory_sector, uint32_t entry_offset,
        uint32_t original_start_cluster, uint32_t expected_start_cluster,
        uint32_t dot_kind) {
    if (state->dot_cluster_repair_count >=
            FAT12_MAX_DOT_CLUSTER_REPAIRS ||
        entry_offset > X86OS_STORAGE_BLOCK_SIZE - 32U ||
        original_start_cluster > UINT16_MAX ||
        expected_start_cluster > UINT16_MAX ||
        original_start_cluster == expected_start_cluster ||
        (dot_kind != 1U && dot_kind != 2U)) {
        state->flags |= X86OS_FAT12_RESULT_SCAN_LIMIT;
        return;
    }
    fat12_dot_cluster_repairs[state->dot_cluster_repair_count++] =
        (fat12_dot_cluster_repair_t){
            .directory_sector = directory_sector,
            .entry_offset = (uint16_t)entry_offset,
            .original_start_cluster = (uint16_t)original_start_cluster,
            .expected_start_cluster = (uint16_t)expected_start_cluster,
            .dot_kind = (uint8_t)dot_kind,
        };
}

static void fat12_add_required_crosslink_file(fat12_scan_state_t *state,
        uint32_t directory_sector, uint32_t entry_offset,
        uint32_t start_cluster, uint32_t cluster_count,
        uint32_t original_size, uint32_t owner) {
    if (state->required_crosslink_file_count >=
            FAT12_MAX_REQUIRED_CROSSLINK_FILES ||
        entry_offset > X86OS_STORAGE_BLOCK_SIZE - 32U ||
        start_cluster < 2U || start_cluster > UINT16_MAX ||
        cluster_count == 0U || cluster_count > UINT16_MAX ||
        owner == 0U || owner > UINT16_MAX) {
        state->required_crosslink_file_overflow = 1U;
        return;
    }
    fat12_required_crosslink_files[
        state->required_crosslink_file_count++] =
        (fat12_required_crosslink_file_t){
            .directory_sector = directory_sector,
            .original_size = original_size,
            .entry_offset = (uint16_t)entry_offset,
            .start_cluster = (uint16_t)start_cluster,
            .cluster_count = (uint16_t)cluster_count,
            .owner = (uint16_t)owner,
            .replacement_start = 0U,
        };
}

static uint32_t fat12_walk_chain(fat12_scan_state_t *state,
        uint32_t start_cluster, uint32_t expected_clusters, int directory,
        uint32_t *actual_clusters_out, int *normal_end_out,
        uint32_t *last_unique_cluster_out, uint32_t *last_unique_next_out,
        uint32_t *owner_out) {
    uint32_t local = 0U;
    if (actual_clusters_out != 0) *actual_clusters_out = 0U;
    if (normal_end_out != 0) *normal_end_out = 0;
    if (last_unique_cluster_out != 0) *last_unique_cluster_out = 0U;
    if (last_unique_next_out != 0) *last_unique_next_out = 0U;
    if (owner_out != 0) *owner_out = 0U;
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
    if (owner_out != 0) *owner_out = owner;
    if (++fat12_seen_generation == 0U) {
        for (uint32_t cluster = 0U; cluster <= last_cluster; ++cluster)
            fat12_chain_seen[cluster] = 0U;
        fat12_seen_generation = 1U;
    }

    uint32_t cluster = start_cluster;
    uint32_t count = 0U;
    uint32_t cut_cluster = 0U;
    uint32_t tail_cluster = 0U;
    uint32_t last_unique_cluster = 0U;
    uint32_t last_unique_next = 0U;
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
        if (fat12_cluster_references[cluster] == UINT16_MAX) {
            local |= X86OS_FAT12_RESULT_SCAN_LIMIT;
            break;
        }
        ++fat12_cluster_references[cluster];
        if (directory || count + 1U <= expected_clusters) {
            if (fat12_cluster_required[cluster] == UINT16_MAX) {
                local |= X86OS_FAT12_RESULT_SCAN_LIMIT;
                break;
            }
            ++fat12_cluster_required[cluster];
        }
        if (fat12_cluster_owner[cluster] != 0U &&
            fat12_cluster_owner[cluster] != owner)
            local |= X86OS_FAT12_RESULT_CHAIN_CROSSLINK;
        else
            fat12_cluster_owner[cluster] = (uint16_t)owner;

        ++count;
        uint32_t next = fat12_entry(fat12_copies[0], cluster);
        last_unique_cluster = cluster;
        last_unique_next = next;
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
    if (last_unique_cluster_out != 0)
        *last_unique_cluster_out = last_unique_cluster;
    if (last_unique_next_out != 0) *last_unique_next_out = last_unique_next;

    if (!directory) {
        if (expected_clusters == 0U && start_cluster != 0U) {
            local |= X86OS_FAT12_RESULT_CHAIN_EXCESS |
                     X86OS_FAT12_RESULT_DIRECTORY_INVALID;
        } else if (count < expected_clusters) {
            local |= X86OS_FAT12_RESULT_CHAIN_SHORT;
        } else if (normal_end && count > expected_clusters) {
            uint32_t ambiguity = X86OS_FAT12_RESULT_CHAIN_INVALID |
                                 X86OS_FAT12_RESULT_CHAIN_LOOP;
            local |= X86OS_FAT12_RESULT_CHAIN_EXCESS;
            if ((local & ambiguity) == 0U && expected_clusters != 0U &&
                cut_cluster >= 2U && tail_cluster >= 2U &&
                tail_cluster <= last_cluster)
                fat12_add_repair(state, cut_cluster, tail_cluster);
        }
    }
    if ((local & X86OS_FAT12_RESULT_CHAIN_EXCESS) != 0U)
        ++state->excess_issue_count;
    if ((local & X86OS_FAT12_RESULT_CHAIN_LOOP) != 0U)
        ++state->loop_issue_count;
    if (!directory && local == X86OS_FAT12_RESULT_CHAIN_LOOP &&
        expected_clusters != 0U && expected_clusters <= count &&
        cut_cluster >= 2U && cut_cluster <= last_cluster &&
        tail_cluster >= 2U && tail_cluster <= last_cluster)
        fat12_add_loop_repair(state, start_cluster, expected_clusters,
                              cut_cluster, tail_cluster);
    if (directory && local == X86OS_FAT12_RESULT_CHAIN_LOOP &&
        last_unique_cluster >= 2U && last_unique_cluster <= last_cluster &&
        last_unique_next >= 2U && last_unique_next <= last_cluster)
        fat12_add_directory_loop_repair(state, start_cluster,
                                        last_unique_cluster,
                                        last_unique_next);
    state->flags |= local;
    return local;
}

static int fat12_dot_entry_kind(const uint8_t *entry) {
    if (entry[0] != '.') return 0;
    uint32_t kind = entry[1] == '.' ? 2U : 1U;
    uint32_t first_space = kind == 2U ? 2U : 1U;
    for (uint32_t index = first_space; index < 11U; ++index)
        if (entry[index] != ' ') return 0;
    return (int)kind;
}

static void fat12_enqueue_directory(fat12_scan_state_t *state,
        uint32_t start_cluster, uint32_t parent_cluster,
        uint32_t parent_directory_sector, uint32_t parent_entry_offset) {
    if (state->directory_count >= FAT12_MAX_DIRECTORIES) {
        state->flags |= X86OS_FAT12_RESULT_SCAN_LIMIT;
        return;
    }
    fat12_directory_queue[state->directory_count++] =
        (fat12_directory_work_t){
            .parent_directory_sector = parent_directory_sector,
            .parent_entry_offset = (uint16_t)parent_entry_offset,
            .start_cluster = (uint16_t)start_cluster,
            .parent_cluster = (uint16_t)parent_cluster,
        };
}

/* Returns one after the FAT end-of-directory marker. */
static int fat12_process_directory_entry(fat12_scan_state_t *state,
        uint32_t directory_sector, uint32_t entry_offset,
        uint32_t current_cluster, uint32_t parent_cluster,
        const uint8_t *entry) {
    if (entry[0] == 0U) return 1;
    if (entry[0] == 0xE5U) return 0;
    uint32_t attributes = entry[11U];
    if (attributes == 0x0FU) return 0; /* VFAT long-name slot. */
    if ((attributes & 0xC0U) != 0U ||
        (attributes & 0x18U) == 0x18U || get16(entry + 20U) != 0U) {
        fat12_mark_directory_invalid(state);
        return 0;
    }
    uint32_t start_cluster = get16(entry + 26U);
    uint32_t size = get32(entry + 28U);
    if ((attributes & 0x08U) != 0U) {
        if (start_cluster != 0U || size != 0U) {
            fat12_mark_directory_invalid(state);
            fat12_add_volume_label_repair(state, directory_sector,
                entry_offset, start_cluster, size);
        }
        return 0;
    }
    if ((attributes & 0x10U) != 0U) {
        int dot_kind = fat12_dot_entry_kind(entry);
        if (dot_kind != 0) {
            uint32_t expected_start = dot_kind == 1
                ? current_cluster : parent_cluster;
            if (current_cluster == 0U) {
                fat12_mark_directory_invalid(state);
                return 0;
            }
            if (start_cluster != expected_start) {
                fat12_mark_directory_invalid(state);
                if (size == 0U)
                    fat12_add_dot_cluster_repair(state, directory_sector,
                        entry_offset, start_cluster, expected_start,
                        (uint32_t)dot_kind);
                return 0;
            }
            if (size != 0U) {
                fat12_mark_directory_invalid(state);
                fat12_add_dot_size_repair(state, directory_sector,
                    entry_offset, expected_start, size, (uint32_t)dot_kind);
            }
            return 0;
        }
        if (start_cluster < 2U ||
            start_cluster > state->layout->cluster_count + 1U) {
            fat12_mark_directory_invalid(state);
            return 0;
        }
        if (size != 0U) {
            fat12_mark_directory_invalid(state);
            fat12_add_directory_size_repair(state, directory_sector,
                entry_offset, start_cluster, size);
        }
        fat12_enqueue_directory(state, start_cluster, current_cluster,
                                directory_sector, entry_offset);
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
    uint32_t last_unique_cluster = 0U;
    uint32_t last_unique_next = 0U;
    uint32_t owner = 0U;
    int normal_end = 0;
    if (expected == 0U && start_cluster != 0U)
        fat12_mark_directory_invalid(state);
    uint32_t local = fat12_walk_chain(state, start_cluster, expected, 0,
        &actual_clusters, &normal_end, &last_unique_cluster,
        &last_unique_next, &owner);
    if ((local & ~X86OS_FAT12_RESULT_CHAIN_CROSSLINK) == 0U &&
        normal_end && expected != 0U && actual_clusters == expected)
        fat12_add_required_crosslink_file(state, directory_sector,
            entry_offset, start_cluster, expected, size, owner);
    if (local == X86OS_FAT12_RESULT_CHAIN_SHORT && start_cluster == 0U &&
        expected != 0U && actual_clusters == 0U && !normal_end)
        fat12_add_zero_start_repair(state, directory_sector, entry_offset,
                                    size);
    uint32_t zero_file_flags = X86OS_FAT12_RESULT_CHAIN_EXCESS |
                               X86OS_FAT12_RESULT_DIRECTORY_INVALID;
    if (local == zero_file_flags && normal_end && actual_clusters != 0U)
        fat12_add_zero_file_repair(state, directory_sector, entry_offset,
            start_cluster, actual_clusters);
    if ((local & X86OS_FAT12_RESULT_CHAIN_SHORT) != 0U)
        ++state->short_issue_count;
    if (local == X86OS_FAT12_RESULT_CHAIN_SHORT && normal_end &&
        actual_clusters != 0U) {
        uint32_t repaired_size = actual_clusters * cluster_bytes;
        fat12_add_short_repair(state, directory_sector, entry_offset,
            start_cluster, size, repaired_size);
    }
    uint32_t short_loop_flags = X86OS_FAT12_RESULT_CHAIN_LOOP |
                                X86OS_FAT12_RESULT_CHAIN_SHORT;
    if (local == short_loop_flags && !normal_end && actual_clusters != 0U &&
        last_unique_cluster >= 2U && last_unique_next >= 2U &&
        last_unique_next <= state->layout->cluster_count + 1U) {
        uint32_t repaired_size = actual_clusters * cluster_bytes;
        fat12_add_short_loop_repair(state, directory_sector, entry_offset,
            start_cluster, actual_clusters, last_unique_cluster,
            last_unique_next, size, repaired_size);
    }
    return 0;
}

static int fat12_scan_directory_sector(fat12_scan_state_t *state,
        uint32_t sector, uint32_t current_cluster,
        uint32_t parent_cluster) {
    uint8_t data[X86OS_STORAGE_BLOCK_SIZE];
    if (sector >= state->layout->total_sectors ||
        x86os_storage_block_read(state->resource, sector, data) != 0)
        return -5;
    for (uint32_t offset = 0U; offset < X86OS_STORAGE_BLOCK_SIZE;
         offset += 32U)
        if (fat12_process_directory_entry(state, sector, offset,
                current_cluster, parent_cluster, data + offset)) return 1;
    return 0;
}

static int fat12_scan_root(fat12_scan_state_t *state) {
    uint32_t root_start = state->layout->reserved_sectors +
                          2U * state->layout->fat_sectors;
    for (uint32_t sector = 0U; sector < state->layout->root_sectors; ++sector) {
        int result = fat12_scan_directory_sector(state, root_start + sector,
                                                  0U, 0U);
        if (result != 0) return result < 0 ? result : 0;
    }
    return 0;
}

static int fat12_scan_subdirectory(fat12_scan_state_t *state,
        uint32_t start_cluster, uint32_t parent_cluster) {
    uint32_t local = fat12_walk_chain(state, start_cluster, 0U, 1,
                                      0, 0, 0, 0, 0);
    if (local != 0U && local != X86OS_FAT12_RESULT_CHAIN_LOOP) return 0;
    uint32_t last_cluster = state->layout->cluster_count + 1U;
    if (++fat12_seen_generation == 0U) {
        for (uint32_t index = 0U; index <= last_cluster; ++index)
            fat12_chain_seen[index] = 0U;
        fat12_seen_generation = 1U;
    }
    uint32_t cluster = start_cluster;
    for (uint32_t steps = 0U; steps < state->layout->cluster_count; ++steps) {
        if (cluster < 2U || cluster > last_cluster) return -84;
        if (fat12_chain_seen[cluster] == fat12_seen_generation) return 0;
        fat12_chain_seen[cluster] = fat12_seen_generation;
        uint32_t first_sector = state->layout->data_start +
            (cluster - 2U) * state->layout->sectors_per_cluster;
        for (uint32_t index = 0U;
             index < state->layout->sectors_per_cluster; ++index) {
            int result = fat12_scan_directory_sector(state,
                first_sector + index, start_cluster, parent_cluster);
            if (result != 0) return result < 0 ? result : 0;
        }
        uint32_t next = fat12_entry(fat12_copies[0], cluster);
        if (fat12_is_eoc(next)) return 0;
        cluster = next;
    }
    return -84;
}

static int fat12_empty_directory_entry_valid(const uint8_t *entry,
        uint32_t dot_kind, uint32_t expected_start) {
    uint32_t attributes = entry[11U];
    return fat12_dot_entry_kind(entry) == (int)dot_kind &&
        (attributes & 0xC0U) == 0U &&
        (attributes & 0x18U) == 0x10U &&
        get16(entry + 20U) == 0U &&
        get16(entry + 26U) == expected_start &&
        get32(entry + 28U) == 0U;
}

static int fat12_record_empty_directory_crosslink(
        fat12_scan_state_t *state, const fat12_directory_work_t *work) {
    if (state->layout->sectors_per_cluster != 1U ||
        !fat12_is_eoc(fat12_entry(fat12_copies[0], work->start_cluster)))
        return 0;
    uint32_t sector = state->layout->data_start +
                      (work->start_cluster - 2U);
    uint8_t data[X86OS_STORAGE_BLOCK_SIZE];
    if (sector >= state->layout->total_sectors ||
        x86os_storage_block_read(state->resource, sector, data) != 0)
        return -5;
    if (!fat12_empty_directory_entry_valid(data, 1U,
                                           work->start_cluster) ||
        !fat12_empty_directory_entry_valid(data + 32U, 2U,
                                           work->parent_cluster) ||
        data[64U] != 0U) return 0;
    if (state->empty_directory_crosslink_count >=
        FAT12_MAX_EMPTY_DIRECTORY_CROSSLINKS) {
        state->empty_directory_crosslink_overflow = 1U;
        return 0;
    }
    fat12_empty_directory_crosslinks[
        state->empty_directory_crosslink_count++] =
        (fat12_empty_directory_crosslink_t){
            .parent_directory_sector = work->parent_directory_sector,
            .parent_entry_offset = work->parent_entry_offset,
            .start_cluster = work->start_cluster,
            .parent_cluster = work->parent_cluster,
            .replacement_start = 0U,
        };
    return 0;
}

static int fat12_scan_chains(uint32_t resource,
                             const fat12_check_layout_t *layout) {
    uint32_t last_cluster = layout->cluster_count + 1U;
    for (uint32_t cluster = 0U; cluster <= last_cluster; ++cluster) {
        fat12_cluster_owner[cluster] = 0U;
        fat12_cluster_references[cluster] = 0U;
        fat12_cluster_required[cluster] = 0U;
        fat12_chain_seen[cluster] = 0U;
    }
    fat12_seen_generation = 0U;
    fat12_chain_repair_count = 0U;
    fat12_short_repair_count = 0U;
    fat12_short_issue_count = 0U;
    fat12_loop_repair_count = 0U;
    fat12_loop_issue_count = 0U;
    fat12_directory_loop_repair_count = 0U;
    fat12_short_loop_repair_count = 0U;
    fat12_excess_issue_count = 0U;
    fat12_directory_size_repair_count = 0U;
    fat12_volume_label_repair_count = 0U;
    fat12_zero_file_repair_count = 0U;
    fat12_zero_start_repair_count = 0U;
    fat12_dot_size_repair_count = 0U;
    fat12_dot_cluster_repair_count = 0U;
    fat12_required_crosslink_file_count = 0U;
    fat12_required_crosslink_file_overflow = 0U;
    fat12_empty_directory_crosslink_count = 0U;
    fat12_empty_directory_crosslink_overflow = 0U;
    fat12_directory_count = 0U;
    fat12_directory_topology_repair_count = 0U;
    fat12_clone_cluster_count = 0U;
    fat12_directory_invalid_issue_count = 0U;
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
        .directory_loop_repair_count = 0U,
        .short_loop_repair_count = 0U,
        .excess_issue_count = 0U,
        .directory_size_repair_count = 0U,
        .volume_label_repair_count = 0U,
        .zero_file_repair_count = 0U,
        .zero_start_repair_count = 0U,
        .dot_size_repair_count = 0U,
        .dot_cluster_repair_count = 0U,
        .required_crosslink_file_count = 0U,
        .required_crosslink_file_overflow = 0U,
        .empty_directory_crosslink_count = 0U,
        .empty_directory_crosslink_overflow = 0U,
        .directory_invalid_issue_count = 0U,
    };
    int result = fat12_scan_root(&state);
    if (result != 0) return result;
    for (uint32_t index = 0U; index < state.directory_count; ++index) {
        result = fat12_scan_subdirectory(&state,
            fat12_directory_queue[index].start_cluster,
            fat12_directory_queue[index].parent_cluster);
        if (result != 0) return result;
        result = fat12_record_empty_directory_crosslink(
            &state, &fat12_directory_queue[index]);
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
    fat12_directory_loop_repair_count =
        state.directory_loop_repair_count;
    fat12_short_loop_repair_count = state.short_loop_repair_count;
    fat12_excess_issue_count = state.excess_issue_count;
    fat12_directory_size_repair_count = state.directory_size_repair_count;
    fat12_volume_label_repair_count = state.volume_label_repair_count;
    fat12_zero_file_repair_count = state.zero_file_repair_count;
    fat12_zero_start_repair_count = state.zero_start_repair_count;
    fat12_dot_size_repair_count = state.dot_size_repair_count;
    fat12_dot_cluster_repair_count = state.dot_cluster_repair_count;
    fat12_required_crosslink_file_count =
        state.required_crosslink_file_count;
    fat12_required_crosslink_file_overflow =
        state.required_crosslink_file_overflow;
    fat12_empty_directory_crosslink_count =
        state.empty_directory_crosslink_count;
    fat12_empty_directory_crosslink_overflow =
        state.empty_directory_crosslink_overflow;
    fat12_directory_count = state.directory_count;
    fat12_directory_invalid_issue_count =
        state.directory_invalid_issue_count;
    return (int)state.flags;
}

static int fat12_validate_persistence(uint32_t resource,
                                      const fat12_check_layout_t *layout);

static int fat12_check_volume(uint32_t resource,
                              fat12_check_layout_t *layout) {
    int result = fat12_inspect(resource, layout);
    if (result != 0) return result;
    result = fat12_validate_persistence(resource, layout);
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
    int unsupported = 0;
    for (uint32_t copy = 0U; copy < 2U; ++copy) {
        if (x86os_storage_block_read(resource,
                FORMAT_FAT12_LAYOUT_BASE + copy, sectors[copy]) == 0) {
            fat12_copy_bytes(&headers[copy], sectors[copy], sizeof(headers[copy]));
            if (headers[copy].magic == FAT12_JOURNAL_MAGIC &&
                headers[copy].version != FAT12_JOURNAL_VERSION)
                unsupported = 1;
            valid[copy] = fat12_journal_header_valid(&headers[copy],
                                                     layout->volume_id);
        }
    }
    if (!valid[0] && !valid[1]) return unsupported ? -95 : -84;
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

static uint32_t fat12_remap_crc(const format_remap_header_t *header,
        const format_remap_entry_t *entries) {
    uint8_t payload[sizeof(*header) +
                    FORMAT_FAT12_REMAP_SPARES * sizeof(*entries)];
    format_remap_header_t copy = *header;
    copy.crc32 = 0U;
    fat12_copy_bytes(payload, &copy, sizeof(copy));
    uint32_t bytes = copy.entry_count * sizeof(*entries);
    if (bytes != 0U) fat12_copy_bytes(payload + sizeof(copy), entries, bytes);
    return format_crc32(payload, sizeof(copy) + bytes);
}

static int fat12_remap_entries_valid(const format_remap_header_t *header,
        const format_remap_entry_t *entries,
        const fat12_check_layout_t *layout) {
    uint32_t spare = FORMAT_FAT12_REMAP_BASE + 3U;
    if (header->entry_count > FORMAT_FAT12_REMAP_SPARES) return 0;
    for (uint32_t index = 0U; index < header->entry_count; ++index) {
        format_remap_entry_t entry = entries[index];
        if (entry.bad_sector < layout->reserved_sectors ||
            entry.bad_sector >= layout->data_start ||
            entry.replacement_sector < spare ||
            entry.replacement_sector >= spare + FORMAT_FAT12_REMAP_SPARES ||
            entry.sequence == 0U || entry.sequence > header->sequence)
            return 0;
        for (uint32_t previous = 0U; previous < index; ++previous)
            if (entries[previous].bad_sector == entry.bad_sector ||
                entries[previous].replacement_sector ==
                    entry.replacement_sector ||
                entries[previous].bad_sector == entry.replacement_sector ||
                entries[previous].replacement_sector == entry.bad_sector)
                return 0;
    }
    return header->crc32 == fat12_remap_crc(header, entries);
}

static int fat12_load_remap(uint32_t resource,
        const fat12_check_layout_t *layout, fat12_remap_state_t *selected) {
    uint8_t headers_raw[2U][X86OS_STORAGE_BLOCK_SIZE];
    uint8_t table[X86OS_STORAGE_BLOCK_SIZE];
    format_remap_header_t headers[2U];
    format_remap_entry_t entries[FORMAT_FAT12_REMAP_SPARES];
    int valid[2U] = {0, 0};
    int unsupported = 0;
    uint32_t maximum_count = 0U;
    format_fill(table, 0U, sizeof(table));
    format_fill((uint8_t *)entries, 0U, sizeof(entries));
    for (uint32_t copy = 0U; copy < 2U; ++copy) {
        if (x86os_storage_block_read(resource,
                FORMAT_FAT12_REMAP_BASE + copy, headers_raw[copy]) != 0)
            continue;
        fat12_copy_bytes(&headers[copy], headers_raw[copy],
                         sizeof(headers[copy]));
        if (headers[copy].magic == FAT12_REMAP_MAGIC &&
            headers[copy].version != FAT12_REMAP_VERSION) unsupported = 1;
        valid[copy] = headers[copy].magic == FAT12_REMAP_MAGIC &&
            headers[copy].version == FAT12_REMAP_VERSION &&
            headers[copy].entry_size == sizeof(format_remap_entry_t) &&
            headers[copy].media_fingerprint == layout->volume_id &&
            headers[copy].sequence != 0U &&
            headers[copy].entry_count <= FORMAT_FAT12_REMAP_SPARES;
        if (valid[copy] && headers[copy].entry_count > maximum_count)
            maximum_count = headers[copy].entry_count;
    }
    if (!valid[0] && !valid[1]) return unsupported ? -95 : -84;
    if (maximum_count != 0U) {
        if (x86os_storage_block_read(resource,
                FORMAT_FAT12_REMAP_BASE + 2U, table) != 0) return -5;
        fat12_copy_bytes(entries, table, sizeof(entries));
    }
    valid[0] = valid[0] &&
        fat12_remap_entries_valid(&headers[0], entries, layout);
    valid[1] = valid[1] &&
        fat12_remap_entries_valid(&headers[1], entries, layout);
    if (!valid[0] && !valid[1]) return -84;
    if (valid[0] && valid[1] &&
        headers[0].sequence == headers[1].sequence &&
        !format_equal((const uint8_t *)&headers[0],
                      (const uint8_t *)&headers[1], sizeof(headers[0])))
        return -84;
    uint32_t chosen = !valid[1] ||
        (valid[0] && headers[0].sequence >= headers[1].sequence) ? 0U : 1U;
    selected->header = headers[chosen];
    format_fill((uint8_t *)selected->entries, 0U,
                sizeof(selected->entries));
    fat12_copy_bytes(selected->entries, entries,
        selected->header.entry_count * sizeof(selected->entries[0]));
    return 0;
}

static int fat12_validate_persistence(uint32_t resource,
        const fat12_check_layout_t *layout) {
    if (!layout->reist_layout) return 0;
    format_journal_header_t journal;
    fat12_remap_state_t remap;
    int result = fat12_load_clean_journal(resource, layout, &journal);
    return result == 0 ? fat12_load_remap(resource, layout, &remap) : result;
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

static int fat12_crosslinks_are_excess_only(
        const fat12_check_layout_t *layout) {
    uint32_t last_cluster = layout->cluster_count + 1U;
    int found = 0;
    for (uint32_t cluster = 2U; cluster <= last_cluster; ++cluster) {
        if (fat12_cluster_references[cluster] <= 1U) continue;
        found = 1;
        if (fat12_cluster_required[cluster] > 1U) return 0;
    }
    return found;
}

static int fat12_apply_crosslink_repairs(
        const fat12_check_layout_t *layout) {
    uint32_t bytes = layout->fat_sectors * X86OS_STORAGE_BLOCK_SIZE;
    fat12_copy_bytes(fat12_repair_fat, fat12_copies[0], bytes);
    uint32_t last_cluster = layout->cluster_count + 1U;
    for (uint32_t index = 0U; index < fat12_chain_repair_count; ++index) {
        uint32_t cut = fat12_chain_repairs[index].cut_cluster;
        uint32_t tail = fat12_chain_repairs[index].tail_cluster;
        if (cut < 2U || cut > last_cluster || tail < 2U ||
            tail > last_cluster || fat12_cluster_required[cut] != 1U)
            return -84;
        fat12_set_entry(fat12_repair_fat, cut, 0x0FFFU);
    }
    for (uint32_t cluster = 2U; cluster <= last_cluster; ++cluster) {
        if (fat12_cluster_references[cluster] != 0U &&
            fat12_cluster_required[cluster] == 0U)
            fat12_set_entry(fat12_repair_fat, cluster, 0U);
    }
    return 0;
}

static int fat12_repair_crosslinks(uint32_t resource) {
    fat12_check_layout_t layout;
    uint32_t diagnosis_flags = X86OS_FAT12_RESULT_CHAIN_CROSSLINK |
                               X86OS_FAT12_RESULT_CHAIN_EXCESS;
    int diagnosis = fat12_check_volume(resource, &layout);
    if (diagnosis != (int)diagnosis_flags || !layout.reist_layout ||
        fat12_chain_repair_count == 0U ||
        fat12_excess_issue_count != fat12_chain_repair_count ||
        !fat12_crosslinks_are_excess_only(&layout)) return -84;

    uint32_t token = 0U;
    if (x86os_storage_maintenance_acquire(resource, 0U, &token) != 0 ||
        token == 0U) return -30;

    int result = fat12_check_volume(resource, &layout);
    if (result != diagnosis || !layout.reist_layout ||
        fat12_chain_repair_count == 0U ||
        fat12_excess_issue_count != fat12_chain_repair_count ||
        !fat12_crosslinks_are_excess_only(&layout))
        result = -84;
    if (result == diagnosis) result = fat12_apply_crosslink_repairs(&layout);

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
        ? diagnosis | (int)X86OS_FAT12_RESULT_CROSSLINKS_REPAIRED : result;
}

static int fat12_required_crosslinks_are_regular(
        const fat12_check_layout_t *layout) {
    uint32_t last_cluster = layout->cluster_count + 1U;
    if (fat12_required_crosslink_file_overflow != 0U ||
        fat12_required_crosslink_file_count < 2U) return 0;
    for (uint32_t cluster = 0U; cluster <= last_cluster; ++cluster)
        fat12_regular_references[cluster] = 0U;
    for (uint32_t index = 0U;
         index < fat12_required_crosslink_file_count; ++index) {
        const fat12_required_crosslink_file_t *file =
            &fat12_required_crosslink_files[index];
        uint32_t cluster = file->start_cluster;
        for (uint32_t step = 0U; step < file->cluster_count; ++step) {
            if (cluster < 2U || cluster > last_cluster ||
                fat12_regular_references[cluster] == UINT16_MAX) return 0;
            ++fat12_regular_references[cluster];
            uint32_t next = fat12_entry(fat12_copies[0], cluster);
            if (step + 1U == file->cluster_count) {
                if (!fat12_is_eoc(next)) return 0;
            } else {
                if (next < 2U || next > last_cluster) return 0;
                cluster = next;
            }
        }
    }
    int found = 0;
    for (uint32_t cluster = 2U; cluster <= last_cluster; ++cluster) {
        if (fat12_cluster_required[cluster] <= 1U) continue;
        found = 1;
        if (fat12_regular_references[cluster] !=
            fat12_cluster_required[cluster]) return 0;
    }
    return found;
}

static int fat12_plan_required_crosslink_clones(
        const fat12_check_layout_t *layout) {
    uint32_t bytes = layout->fat_sectors * X86OS_STORAGE_BLOCK_SIZE;
    uint32_t last_cluster = layout->cluster_count + 1U;
    fat12_copy_bytes(fat12_repair_fat, fat12_copies[0], bytes);
    fat12_clone_cluster_count = 0U;
    for (uint32_t cluster = 0U; cluster <= last_cluster; ++cluster) {
        fat12_clone_claimed[cluster] = 0U;
        fat12_clone_remaining[cluster] =
            fat12_cluster_references[cluster];
    }
    for (uint32_t index = 0U;
         index < fat12_required_crosslink_file_count; ++index)
        fat12_required_crosslink_files[index].replacement_start = 0U;

    for (uint32_t index = 0U;
         index < fat12_required_crosslink_file_count; ++index) {
        fat12_required_crosslink_file_t *file =
            &fat12_required_crosslink_files[index];
        uint32_t cluster = file->start_cluster;
        int conflict = 0;
        for (uint32_t step = 0U; step < file->cluster_count; ++step) {
            if (cluster < 2U || cluster > last_cluster) return -84;
            if (fat12_clone_claimed[cluster] != 0U) conflict = 1;
            uint32_t next = fat12_entry(fat12_copies[0], cluster);
            if (step + 1U == file->cluster_count) {
                if (!fat12_is_eoc(next)) return -84;
            } else {
                if (next < 2U || next > last_cluster) return -84;
                cluster = next;
            }
        }
        if (!conflict) {
            cluster = file->start_cluster;
            for (uint32_t step = 0U; step < file->cluster_count; ++step) {
                fat12_clone_claimed[cluster] = 1U;
                uint32_t next = fat12_entry(fat12_copies[0], cluster);
                if (step + 1U != file->cluster_count) cluster = next;
            }
            continue;
        }
        if (file->cluster_count > FAT12_MAX_CLONE_CLUSTERS -
                fat12_clone_cluster_count) return -28;
        uint32_t clone_base = fat12_clone_cluster_count;
        cluster = file->start_cluster;
        for (uint32_t step = 0U; step < file->cluster_count; ++step) {
            uint32_t destination = 0U;
            for (uint32_t free_cluster = 2U;
                 free_cluster <= last_cluster; ++free_cluster) {
                if (fat12_cluster_references[free_cluster] == 0U &&
                    fat12_entry(fat12_repair_fat, free_cluster) == 0U) {
                    destination = free_cluster;
                    break;
                }
            }
            if (destination == 0U) return -28;
            uint32_t clone_index = fat12_clone_cluster_count++;
            fat12_clone_source[clone_index] = (uint16_t)cluster;
            fat12_clone_destination[clone_index] = (uint16_t)destination;
            fat12_set_entry(fat12_repair_fat, destination, 0x0FFFU);
            uint32_t next = fat12_entry(fat12_copies[0], cluster);
            if (step + 1U != file->cluster_count) cluster = next;
        }
        file->replacement_start = fat12_clone_destination[clone_base];
        for (uint32_t step = 0U; step < file->cluster_count; ++step) {
            uint32_t clone_index = clone_base + step;
            uint32_t next = step + 1U == file->cluster_count
                ? 0x0FFFU : fat12_clone_destination[clone_index + 1U];
            fat12_set_entry(fat12_repair_fat,
                fat12_clone_destination[clone_index], next);
            uint32_t source = fat12_clone_source[clone_index];
            if (fat12_clone_remaining[source] == 0U) return -84;
            --fat12_clone_remaining[source];
        }
    }
    if (fat12_clone_cluster_count == 0U) return -84;
    for (uint32_t cluster = 2U; cluster <= last_cluster; ++cluster) {
        if (fat12_cluster_references[cluster] != 0U &&
            fat12_clone_remaining[cluster] == 0U)
            fat12_set_entry(fat12_repair_fat, cluster, 0U);
    }
    return 0;
}

static int fat12_collect_required_crosslink_directory_sectors(void) {
    fat12_directory_repair_sector_count = 0U;
    for (uint32_t index = 0U;
         index < fat12_required_crosslink_file_count; ++index) {
        const fat12_required_crosslink_file_t *file =
            &fat12_required_crosslink_files[index];
        if (file->replacement_start == 0U) continue;
        int found = 0;
        for (uint32_t known = 0U;
             known < fat12_directory_repair_sector_count; ++known) {
            if (fat12_directory_repair_sectors[known] ==
                file->directory_sector) {
                found = 1;
                break;
            }
        }
        if (found) continue;
        if (fat12_directory_repair_sector_count >=
            FAT12_MAX_DIRECTORY_REPAIR_SECTORS) return -28;
        fat12_directory_repair_sectors[
            fat12_directory_repair_sector_count++] =
            file->directory_sector;
    }
    return fat12_directory_repair_sector_count == 0U ? -84 : 0;
}

static int fat12_update_required_crosslink_directory_sector(uint32_t sector,
        uint8_t *data, int apply) {
    uint32_t matches = 0U;
    for (uint32_t index = 0U;
         index < fat12_required_crosslink_file_count; ++index) {
        const fat12_required_crosslink_file_t *file =
            &fat12_required_crosslink_files[index];
        if (file->replacement_start == 0U ||
            file->directory_sector != sector) continue;
        uint32_t offset = file->entry_offset;
        if ((offset & 31U) != 0U ||
            offset > X86OS_STORAGE_BLOCK_SIZE - 32U) return -84;
        uint8_t *entry = data + offset;
        uint32_t attributes = entry[11U];
        if (entry[0] == 0U || entry[0] == 0xE5U || attributes == 0x0FU ||
            (attributes & 0xC0U) != 0U || (attributes & 0x18U) != 0U ||
            get16(entry + 20U) != 0U ||
            get16(entry + 26U) != file->start_cluster ||
            get32(entry + 28U) != file->original_size)
            return -84;
        if (apply) put16(entry + 26U, file->replacement_start);
        ++matches;
    }
    return matches == 0U ? -84 : 0;
}

static int fat12_repair_required_crosslinks(uint32_t resource) {
    fat12_check_layout_t layout;
    int diagnosis = fat12_check_volume(resource, &layout);
    if (diagnosis != (int)X86OS_FAT12_RESULT_CHAIN_CROSSLINK ||
        !layout.reist_layout ||
        !fat12_required_crosslinks_are_regular(&layout)) return -84;

    uint32_t token = 0U;
    if (x86os_storage_maintenance_acquire(resource, 0U, &token) != 0 ||
        token == 0U) return -30;

    int result = fat12_check_volume(resource, &layout);
    if (result != diagnosis || !layout.reist_layout ||
        !fat12_required_crosslinks_are_regular(&layout)) result = -84;
    if (result == diagnosis)
        result = fat12_plan_required_crosslink_clones(&layout);
    if (result == 0)
        result = fat12_collect_required_crosslink_directory_sectors();

    uint32_t changed_fat_sectors = 0U;
    for (uint32_t copy = 0U; result == 0 && copy < 2U; ++copy) {
        for (uint32_t sector = 0U; sector < layout.fat_sectors; ++sector) {
            const uint8_t *replacement = fat12_repair_fat +
                sector * X86OS_STORAGE_BLOCK_SIZE;
            const uint8_t *old = fat12_copies[copy] +
                sector * X86OS_STORAGE_BLOCK_SIZE;
            if (!format_equal(replacement, old, X86OS_STORAGE_BLOCK_SIZE))
                ++changed_fat_sectors;
        }
    }
    uint32_t destination_sectors = fat12_clone_cluster_count *
                                   layout.sectors_per_cluster;
    if (result == 0 && (changed_fat_sectors == 0U ||
        destination_sectors > FORMAT_FAT12_JOURNAL_ENTRIES ||
        changed_fat_sectors > FORMAT_FAT12_JOURNAL_ENTRIES -
            destination_sectors ||
        fat12_directory_repair_sector_count >
            FORMAT_FAT12_JOURNAL_ENTRIES - destination_sectors -
                changed_fat_sectors)) result = -28;

    format_journal_header_t journal;
    if (result == 0 &&
        fat12_load_clean_journal(resource, &layout, &journal) != 0)
        result = -84;
    if (result == 0) {
        fat12_prepare_journal_header(&journal, layout.volume_id,
            journal.sequence + 1U, FAT12_JOURNAL_ACTIVE, 0U);
        result = fat12_write_journal_header(resource, &journal);
    }

    uint8_t sector_data[X86OS_STORAGE_BLOCK_SIZE];
    for (uint32_t index = 0U; result == 0 &&
         index < fat12_clone_cluster_count; ++index) {
        uint32_t destination_first = layout.data_start +
            (fat12_clone_destination[index] - 2U) *
                layout.sectors_per_cluster;
        for (uint32_t offset = 0U; result == 0 &&
             offset < layout.sectors_per_cluster; ++offset) {
            uint32_t target = destination_first + offset;
            if (x86os_storage_block_read(resource, target, sector_data) != 0)
                result = -5;
            if (result == 0)
                result = fat12_record_old_sector(resource, &journal, target,
                                                 sector_data);
            if (result == 0 &&
                x86os_storage_maintenance_renew(resource, token, 0U) != 0)
                result = -30;
        }
    }
    for (uint32_t copy = 0U; result == 0 && copy < 2U; ++copy) {
        for (uint32_t sector = 0U; result == 0 &&
             sector < layout.fat_sectors; ++sector) {
            const uint8_t *replacement = fat12_repair_fat +
                sector * X86OS_STORAGE_BLOCK_SIZE;
            const uint8_t *old = fat12_copies[copy] +
                sector * X86OS_STORAGE_BLOCK_SIZE;
            if (format_equal(replacement, old, X86OS_STORAGE_BLOCK_SIZE))
                continue;
            uint32_t target = layout.reserved_sectors +
                copy * layout.fat_sectors + sector;
            result = fat12_record_old_mirror(resource, &journal, copy,
                                             sector, target);
            if (result == 0 &&
                x86os_storage_maintenance_renew(resource, token, 0U) != 0)
                result = -30;
        }
    }
    for (uint32_t index = 0U; result == 0 &&
         index < fat12_directory_repair_sector_count; ++index) {
        uint32_t sector = fat12_directory_repair_sectors[index];
        if (x86os_storage_block_read(resource, sector, sector_data) != 0 ||
            fat12_update_required_crosslink_directory_sector(
                sector, sector_data, 0) != 0) result = -84;
        if (result == 0)
            result = fat12_record_old_sector(resource, &journal, sector,
                                             sector_data);
        if (result == 0 &&
            x86os_storage_maintenance_renew(resource, token, 0U) != 0)
            result = -30;
    }

    for (uint32_t index = 0U; result == 0 &&
         index < fat12_clone_cluster_count; ++index) {
        uint32_t source_first = layout.data_start +
            (fat12_clone_source[index] - 2U) * layout.sectors_per_cluster;
        uint32_t destination_first = layout.data_start +
            (fat12_clone_destination[index] - 2U) *
                layout.sectors_per_cluster;
        for (uint32_t offset = 0U; result == 0 &&
             offset < layout.sectors_per_cluster; ++offset) {
            if (x86os_storage_block_read(resource, source_first + offset,
                    sector_data) != 0 ||
                format_write(resource, destination_first + offset,
                             sector_data) != 0) result = -84;
            if (result == 0 &&
                x86os_storage_maintenance_renew(resource, token, 0U) != 0)
                result = -30;
        }
    }
    for (uint32_t copy = 0U; result == 0 && copy < 2U; ++copy) {
        for (uint32_t sector = 0U; result == 0 &&
             sector < layout.fat_sectors; ++sector) {
            const uint8_t *replacement = fat12_repair_fat +
                sector * X86OS_STORAGE_BLOCK_SIZE;
            const uint8_t *old = fat12_copies[copy] +
                sector * X86OS_STORAGE_BLOCK_SIZE;
            if (format_equal(replacement, old, X86OS_STORAGE_BLOCK_SIZE))
                continue;
            uint32_t target = layout.reserved_sectors +
                copy * layout.fat_sectors + sector;
            result = format_write(resource, target, replacement);
            if (result == 0 &&
                x86os_storage_maintenance_renew(resource, token, 0U) != 0)
                result = -30;
        }
    }
    for (uint32_t index = 0U; result == 0 &&
         index < fat12_directory_repair_sector_count; ++index) {
        uint32_t sector = fat12_directory_repair_sectors[index];
        if (x86os_storage_block_read(resource, sector, sector_data) != 0 ||
            fat12_update_required_crosslink_directory_sector(
                sector, sector_data, 1) != 0 ||
            format_write(resource, sector, sector_data) != 0) result = -84;
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
    return result == 0 ? diagnosis |
        (int)X86OS_FAT12_RESULT_REQUIRED_CROSSLINKS_REPAIRED : result;
}

static int fat12_directory_crosslinks_are_empty_same_parent(
        const fat12_check_layout_t *layout) {
    uint32_t last_cluster = layout->cluster_count + 1U;
    if (fat12_empty_directory_crosslink_overflow != 0U ||
        fat12_empty_directory_crosslink_count < 2U ||
        layout->sectors_per_cluster != 1U) return 0;
    for (uint32_t cluster = 0U; cluster <= last_cluster; ++cluster) {
        fat12_regular_references[cluster] = 0U;
        fat12_clone_remaining[cluster] = 0U;
    }
    for (uint32_t index = 0U;
         index < fat12_empty_directory_crosslink_count; ++index) {
        const fat12_empty_directory_crosslink_t *directory =
            &fat12_empty_directory_crosslinks[index];
        uint32_t cluster = directory->start_cluster;
        uint32_t parent_tag = (uint32_t)directory->parent_cluster + 1U;
        if (cluster < 2U || cluster > last_cluster ||
            fat12_regular_references[cluster] == UINT16_MAX) return 0;
        ++fat12_regular_references[cluster];
        if (fat12_clone_remaining[cluster] == 0U)
            fat12_clone_remaining[cluster] = (uint16_t)parent_tag;
        else if (fat12_clone_remaining[cluster] != parent_tag)
            return 0;
    }
    int found = 0;
    for (uint32_t cluster = 2U; cluster <= last_cluster; ++cluster) {
        if (fat12_cluster_required[cluster] <= 1U) continue;
        found = 1;
        if (fat12_regular_references[cluster] !=
            fat12_cluster_required[cluster]) return 0;
    }
    return found;
}

static int fat12_plan_empty_directory_clones(
        const fat12_check_layout_t *layout) {
    uint32_t bytes = layout->fat_sectors * X86OS_STORAGE_BLOCK_SIZE;
    uint32_t last_cluster = layout->cluster_count + 1U;
    fat12_copy_bytes(fat12_repair_fat, fat12_copies[0], bytes);
    fat12_clone_cluster_count = 0U;
    for (uint32_t cluster = 0U; cluster <= last_cluster; ++cluster)
        fat12_clone_claimed[cluster] = 0U;
    for (uint32_t index = 0U;
         index < fat12_empty_directory_crosslink_count; ++index) {
        fat12_empty_directory_crosslink_t *directory =
            &fat12_empty_directory_crosslinks[index];
        uint32_t source = directory->start_cluster;
        directory->replacement_start = 0U;
        if (fat12_clone_claimed[source] == 0U) {
            fat12_clone_claimed[source] = 1U;
            continue;
        }
        if (fat12_clone_cluster_count >= FAT12_MAX_CLONE_CLUSTERS)
            return -28;
        uint32_t destination = 0U;
        for (uint32_t free_cluster = 2U;
             free_cluster <= last_cluster; ++free_cluster) {
            if (fat12_cluster_references[free_cluster] == 0U &&
                fat12_entry(fat12_repair_fat, free_cluster) == 0U) {
                destination = free_cluster;
                break;
            }
        }
        if (destination == 0U) return -28;
        uint32_t clone_index = fat12_clone_cluster_count++;
        fat12_clone_source[clone_index] = (uint16_t)source;
        fat12_clone_destination[clone_index] = (uint16_t)destination;
        directory->replacement_start = (uint16_t)destination;
        fat12_set_entry(fat12_repair_fat, destination, 0x0FFFU);
    }
    return fat12_clone_cluster_count == 0U ? -84 : 0;
}

static int fat12_collect_empty_directory_parent_sectors(void) {
    fat12_directory_repair_sector_count = 0U;
    for (uint32_t index = 0U;
         index < fat12_empty_directory_crosslink_count; ++index) {
        const fat12_empty_directory_crosslink_t *directory =
            &fat12_empty_directory_crosslinks[index];
        if (directory->replacement_start == 0U) continue;
        int found = 0;
        for (uint32_t known = 0U;
             known < fat12_directory_repair_sector_count; ++known) {
            if (fat12_directory_repair_sectors[known] ==
                directory->parent_directory_sector) {
                found = 1;
                break;
            }
        }
        if (found) continue;
        if (fat12_directory_repair_sector_count >=
            FAT12_MAX_DIRECTORY_REPAIR_SECTORS) return -28;
        fat12_directory_repair_sectors[
            fat12_directory_repair_sector_count++] =
            directory->parent_directory_sector;
    }
    return fat12_directory_repair_sector_count == 0U ? -84 : 0;
}

static int fat12_update_empty_directory_parent_sector(uint32_t sector,
        uint8_t *data, int apply) {
    uint32_t matches = 0U;
    for (uint32_t index = 0U;
         index < fat12_empty_directory_crosslink_count; ++index) {
        const fat12_empty_directory_crosslink_t *directory =
            &fat12_empty_directory_crosslinks[index];
        if (directory->replacement_start == 0U ||
            directory->parent_directory_sector != sector) continue;
        uint32_t offset = directory->parent_entry_offset;
        if ((offset & 31U) != 0U ||
            offset > X86OS_STORAGE_BLOCK_SIZE - 32U) return -84;
        uint8_t *entry = data + offset;
        uint32_t attributes = entry[11U];
        if (entry[0] == 0U || entry[0] == 0xE5U ||
            fat12_dot_entry_kind(entry) != 0 || attributes == 0x0FU ||
            (attributes & 0xC0U) != 0U ||
            (attributes & 0x18U) != 0x10U ||
            get16(entry + 20U) != 0U ||
            get16(entry + 26U) != directory->start_cluster ||
            get32(entry + 28U) != 0U) return -84;
        if (apply) put16(entry + 26U, directory->replacement_start);
        ++matches;
    }
    return matches == 0U ? -84 : 0;
}

static int fat12_prepare_empty_directory_clone_data(uint32_t resource,
        const fat12_check_layout_t *layout, uint32_t clone_index,
        uint8_t *data) {
    if (clone_index >= fat12_clone_cluster_count || data == 0) return -84;
    uint32_t source = fat12_clone_source[clone_index];
    uint32_t destination = fat12_clone_destination[clone_index];
    const fat12_empty_directory_crosslink_t *directory = 0;
    for (uint32_t index = 0U;
         index < fat12_empty_directory_crosslink_count; ++index) {
        if (fat12_empty_directory_crosslinks[index].replacement_start ==
            destination) {
            directory = &fat12_empty_directory_crosslinks[index];
            break;
        }
    }
    if (directory == 0 || directory->start_cluster != source) return -84;
    uint32_t source_sector = layout->data_start + source - 2U;
    if (x86os_storage_block_read(resource, source_sector, data) != 0 ||
        !fat12_empty_directory_entry_valid(data, 1U, source) ||
        !fat12_empty_directory_entry_valid(data + 32U, 2U,
                                           directory->parent_cluster) ||
        data[64U] != 0U) return -84;
    put16(data + 26U, destination);
    return 0;
}

static int fat12_repair_empty_directory_crosslinks(uint32_t resource) {
    fat12_check_layout_t layout;
    int diagnosis = fat12_check_volume(resource, &layout);
    if (diagnosis != (int)X86OS_FAT12_RESULT_CHAIN_CROSSLINK ||
        !layout.reist_layout ||
        !fat12_directory_crosslinks_are_empty_same_parent(&layout))
        return -84;

    uint32_t token = 0U;
    if (x86os_storage_maintenance_acquire(resource, 0U, &token) != 0 ||
        token == 0U) return -30;

    int result = fat12_check_volume(resource, &layout);
    if (result != diagnosis || !layout.reist_layout ||
        !fat12_directory_crosslinks_are_empty_same_parent(&layout))
        result = -84;
    if (result == diagnosis)
        result = fat12_plan_empty_directory_clones(&layout);
    if (result == 0)
        result = fat12_collect_empty_directory_parent_sectors();

    uint32_t changed_fat_sectors = 0U;
    for (uint32_t copy = 0U; result == 0 && copy < 2U; ++copy) {
        for (uint32_t sector = 0U; sector < layout.fat_sectors; ++sector) {
            const uint8_t *replacement = fat12_repair_fat +
                sector * X86OS_STORAGE_BLOCK_SIZE;
            const uint8_t *old = fat12_copies[copy] +
                sector * X86OS_STORAGE_BLOCK_SIZE;
            if (!format_equal(replacement, old, X86OS_STORAGE_BLOCK_SIZE))
                ++changed_fat_sectors;
        }
    }
    if (result == 0 && (changed_fat_sectors == 0U ||
        fat12_clone_cluster_count > FORMAT_FAT12_JOURNAL_ENTRIES ||
        changed_fat_sectors > FORMAT_FAT12_JOURNAL_ENTRIES -
            fat12_clone_cluster_count ||
        fat12_directory_repair_sector_count >
            FORMAT_FAT12_JOURNAL_ENTRIES - fat12_clone_cluster_count -
                changed_fat_sectors)) result = -28;

    format_journal_header_t journal;
    if (result == 0 &&
        fat12_load_clean_journal(resource, &layout, &journal) != 0)
        result = -84;
    if (result == 0) {
        fat12_prepare_journal_header(&journal, layout.volume_id,
            journal.sequence + 1U, FAT12_JOURNAL_ACTIVE, 0U);
        result = fat12_write_journal_header(resource, &journal);
    }

    uint8_t sector_data[X86OS_STORAGE_BLOCK_SIZE];
    for (uint32_t index = 0U; result == 0 &&
         index < fat12_clone_cluster_count; ++index) {
        uint32_t target = layout.data_start +
                          fat12_clone_destination[index] - 2U;
        if (x86os_storage_block_read(resource, target, sector_data) != 0)
            result = -5;
        if (result == 0)
            result = fat12_record_old_sector(resource, &journal, target,
                                             sector_data);
        if (result == 0 &&
            x86os_storage_maintenance_renew(resource, token, 0U) != 0)
            result = -30;
    }
    if (result == 0 && x86os_storage_block_flush(resource) != 0) result = -5;
    if (result == 0 &&
        x86os_storage_maintenance_renew(resource, token, 0U) != 0)
        result = -30;
    for (uint32_t copy = 0U; result == 0 && copy < 2U; ++copy) {
        for (uint32_t sector = 0U; result == 0 &&
             sector < layout.fat_sectors; ++sector) {
            const uint8_t *replacement = fat12_repair_fat +
                sector * X86OS_STORAGE_BLOCK_SIZE;
            const uint8_t *old = fat12_copies[copy] +
                sector * X86OS_STORAGE_BLOCK_SIZE;
            if (format_equal(replacement, old, X86OS_STORAGE_BLOCK_SIZE))
                continue;
            uint32_t target = layout.reserved_sectors +
                copy * layout.fat_sectors + sector;
            result = fat12_record_old_mirror(resource, &journal, copy,
                                             sector, target);
            if (result == 0 &&
                x86os_storage_maintenance_renew(resource, token, 0U) != 0)
                result = -30;
        }
    }
    if (result == 0 && x86os_storage_block_flush(resource) != 0) result = -5;
    if (result == 0 &&
        x86os_storage_maintenance_renew(resource, token, 0U) != 0)
        result = -30;
    for (uint32_t index = 0U; result == 0 &&
         index < fat12_directory_repair_sector_count; ++index) {
        uint32_t sector = fat12_directory_repair_sectors[index];
        if (x86os_storage_block_read(resource, sector, sector_data) != 0 ||
            fat12_update_empty_directory_parent_sector(
                sector, sector_data, 0) != 0) result = -84;
        if (result == 0)
            result = fat12_record_old_sector(resource, &journal, sector,
                                             sector_data);
        if (result == 0 &&
            x86os_storage_maintenance_renew(resource, token, 0U) != 0)
            result = -30;
    }

    for (uint32_t index = 0U; result == 0 &&
         index < fat12_clone_cluster_count; ++index) {
        uint32_t target = layout.data_start +
                          fat12_clone_destination[index] - 2U;
        if (fat12_prepare_empty_directory_clone_data(resource, &layout,
                index, sector_data) != 0 ||
            format_write(resource, target, sector_data) != 0) result = -84;
        if (result == 0 &&
            x86os_storage_maintenance_renew(resource, token, 0U) != 0)
            result = -30;
    }
    if (result == 0 && x86os_storage_block_flush(resource) != 0) result = -5;
    if (result == 0 &&
        x86os_storage_maintenance_renew(resource, token, 0U) != 0)
        result = -30;
    for (uint32_t copy = 0U; result == 0 && copy < 2U; ++copy) {
        for (uint32_t sector = 0U; result == 0 &&
             sector < layout.fat_sectors; ++sector) {
            const uint8_t *replacement = fat12_repair_fat +
                sector * X86OS_STORAGE_BLOCK_SIZE;
            const uint8_t *old = fat12_copies[copy] +
                sector * X86OS_STORAGE_BLOCK_SIZE;
            if (format_equal(replacement, old, X86OS_STORAGE_BLOCK_SIZE))
                continue;
            uint32_t target = layout.reserved_sectors +
                copy * layout.fat_sectors + sector;
            result = format_write(resource, target, replacement);
            if (result == 0 &&
                x86os_storage_maintenance_renew(resource, token, 0U) != 0)
                result = -30;
        }
    }
    if (result == 0 && x86os_storage_block_flush(resource) != 0) result = -5;
    if (result == 0 &&
        x86os_storage_maintenance_renew(resource, token, 0U) != 0)
        result = -30;
    for (uint32_t index = 0U; result == 0 &&
         index < fat12_directory_repair_sector_count; ++index) {
        uint32_t sector = fat12_directory_repair_sectors[index];
        if (x86os_storage_block_read(resource, sector, sector_data) != 0 ||
            fat12_update_empty_directory_parent_sector(
                sector, sector_data, 1) != 0 ||
            format_write(resource, sector, sector_data) != 0) result = -84;
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
    return result == 0 ? diagnosis |
        (int)X86OS_FAT12_RESULT_DIRECTORY_CROSSLINKS_REPAIRED : result;
}

static uint8_t fat12_lfn_checksum(const uint8_t *short_entry) {
    uint8_t checksum = 0U;
    for (uint32_t index = 0U; index < 11U; ++index)
        checksum = (uint8_t)(((checksum & 1U) << 7U) |
                            (checksum >> 1U)) + short_entry[index];
    return checksum;
}

static int fat12_previous_directory_slot(
        const fat12_check_layout_t *layout, uint32_t sector,
        uint32_t offset, uint32_t parent_cluster,
        uint32_t *previous_sector, uint16_t *previous_offset) {
    if (layout == 0 || previous_sector == 0 || previous_offset == 0 ||
        offset > X86OS_STORAGE_BLOCK_SIZE - 32U || (offset & 31U) != 0U)
        return -84;
    if (offset >= 32U) {
        *previous_sector = sector;
        *previous_offset = (uint16_t)(offset - 32U);
        return 1;
    }
    if (parent_cluster == 0U) {
        uint32_t root_start = layout->reserved_sectors +
                              2U * layout->fat_sectors;
        if (sector < root_start ||
            sector >= root_start + layout->root_sectors) return -84;
        if (sector == root_start) return 0;
        *previous_sector = sector - 1U;
        *previous_offset = X86OS_STORAGE_BLOCK_SIZE - 32U;
        return 1;
    }

    uint32_t last_cluster = layout->cluster_count + 1U;
    uint32_t cluster = parent_cluster;
    uint32_t prior = UINT32_MAX;
    for (uint32_t steps = 0U; steps < layout->cluster_count; ++steps) {
        if (cluster < 2U || cluster > last_cluster) return -84;
        uint32_t first = layout->data_start +
            (cluster - 2U) * layout->sectors_per_cluster;
        for (uint32_t index = 0U;
             index < layout->sectors_per_cluster; ++index) {
            uint32_t current = first + index;
            if (current == sector) {
                if (prior == UINT32_MAX) return 0;
                *previous_sector = prior;
                *previous_offset = X86OS_STORAGE_BLOCK_SIZE - 32U;
                return 1;
            }
            prior = current;
        }
        uint32_t next = fat12_entry(fat12_copies[0], cluster);
        if (fat12_is_eoc(next)) break;
        if (next == 0U || next == 1U || next == 0x0FF7U ||
            next < 2U || next > last_cluster) return -84;
        cluster = next;
    }
    return -84;
}

static int fat12_collect_bound_lfn(uint32_t resource,
        const fat12_check_layout_t *layout,
        fat12_directory_topology_repair_t *repair,
        const uint8_t *short_entry) {
    uint32_t sector = repair->parent_directory_sector;
    uint32_t offset = repair->parent_entry_offset;
    uint8_t expected_checksum = fat12_lfn_checksum(short_entry);
    repair->lfn_count = 0U;
    for (;;) {
        uint32_t previous_sector = 0U;
        uint16_t previous_offset = 0U;
        int previous = fat12_previous_directory_slot(layout, sector, offset,
            repair->parent_cluster, &previous_sector, &previous_offset);
        if (previous < 0) return previous;
        if (previous == 0) break;
        uint8_t data[X86OS_STORAGE_BLOCK_SIZE];
        if (x86os_storage_block_read(resource, previous_sector, data) != 0)
            return -5;
        const uint8_t *entry = data + previous_offset;
        if (entry[0] == 0U || entry[0] == 0xE5U || entry[11U] != 0x0FU)
            break;
        if (repair->lfn_count >= FAT12_MAX_LFN_SLOTS) return -28;
        fat12_directory_slot_t *slot =
            &repair->lfn[repair->lfn_count++];
        slot->sector = previous_sector;
        slot->offset = previous_offset;
        slot->crc32 = format_crc32(entry, 32U);
        sector = previous_sector;
        offset = previous_offset;
    }
    for (uint32_t index = 0U; index < repair->lfn_count; ++index) {
        uint8_t data[X86OS_STORAGE_BLOCK_SIZE];
        const fat12_directory_slot_t *slot = &repair->lfn[index];
        if (x86os_storage_block_read(resource, slot->sector, data) != 0)
            return -5;
        const uint8_t *entry = data + slot->offset;
        uint32_t ordinal = entry[0];
        uint32_t expected_ordinal = index + 1U;
        int last = expected_ordinal == repair->lfn_count;
        if ((ordinal & 0x1FU) != expected_ordinal ||
            (ordinal & 0xA0U) != 0U ||
            ((ordinal & 0x40U) != 0U) != last ||
            entry[11U] != 0x0FU || entry[12U] != 0U ||
            entry[13U] != expected_checksum || get16(entry + 26U) != 0U)
            return -84;
    }
    return 0;
}

static int fat12_read_directory_header(uint32_t resource,
        const fat12_check_layout_t *layout, uint32_t start_cluster,
        uint16_t *dot_parent) {
    if (start_cluster < 2U ||
        start_cluster > layout->cluster_count + 1U || dot_parent == 0)
        return -84;
    uint32_t sector = layout->data_start +
        (start_cluster - 2U) * layout->sectors_per_cluster;
    uint8_t data[X86OS_STORAGE_BLOCK_SIZE];
    if (x86os_storage_block_read(resource, sector, data) != 0) return -5;
    uint32_t parent = get16(data + 32U + 26U);
    if (parent != 0U &&
        (parent < 2U || parent > layout->cluster_count + 1U)) return -84;
    if (!fat12_empty_directory_entry_valid(data, 1U, start_cluster) ||
        !fat12_empty_directory_entry_valid(data + 32U, 2U, parent))
        return -84;
    *dot_parent = (uint16_t)parent;
    return 0;
}

static int fat12_read_topology_parent_entry(uint32_t resource,
        const fat12_directory_work_t *work, uint8_t *sector_data,
        const uint8_t **entry_out) {
    if (work->parent_entry_offset > X86OS_STORAGE_BLOCK_SIZE - 32U ||
        (work->parent_entry_offset & 31U) != 0U ||
        x86os_storage_block_read(resource, work->parent_directory_sector,
                                 sector_data) != 0) return -5;
    const uint8_t *entry = sector_data + work->parent_entry_offset;
    uint32_t attributes = entry[11U];
    if (entry[0] == 0U || entry[0] == 0xE5U ||
        fat12_dot_entry_kind(entry) != 0 || attributes == 0x0FU ||
        (attributes & 0xC0U) != 0U ||
        (attributes & 0x18U) != 0x10U ||
        get16(entry + 20U) != 0U ||
        get16(entry + 26U) != work->start_cluster ||
        get32(entry + 28U) != 0U) return -84;
    if (entry_out != 0) *entry_out = entry;
    return 0;
}

static int fat12_plan_directory_topology(uint32_t resource,
        const fat12_check_layout_t *layout, int diagnosis) {
    uint32_t last_cluster = layout->cluster_count + 1U;
    fat12_directory_topology_repair_count = 0U;
    for (uint32_t cluster = 0U; cluster <= last_cluster; ++cluster)
        fat12_clone_claimed[cluster] = 0U;
    uint32_t expected_invalid = 0U;

    for (uint32_t first = 0U; first < fat12_directory_count; ++first) {
        uint32_t start = fat12_directory_queue[first].start_cluster;
        if ((fat12_clone_claimed[start] & 1U) != 0U) continue;
        fat12_clone_claimed[start] |= 1U;
        uint32_t aliases = 0U;
        uint32_t matching_parent = 0U;
        uint32_t canonical = UINT32_MAX;
        uint32_t first_parent = fat12_directory_queue[first].parent_cluster;
        int same_parent = 1;
        uint16_t dot_parent = 0U;
        if (fat12_read_directory_header(resource, layout, start,
                                        &dot_parent) != 0) return -84;
        for (uint32_t index = first; index < fat12_directory_count; ++index) {
            const fat12_directory_work_t *work =
                &fat12_directory_queue[index];
            if (work->start_cluster != start) continue;
            ++aliases;
            if (work->parent_cluster != first_parent) same_parent = 0;
            if (work->parent_cluster == dot_parent) {
                ++matching_parent;
                canonical = index;
            }
        }
        if (aliases < 2U) continue;
        if (same_parent && first_parent == dot_parent)
            canonical = first;
        else if (matching_parent != 1U)
            return -84;
        if (fat12_directory_queue[first].parent_cluster != dot_parent)
            ++expected_invalid;

        for (uint32_t index = first; index < fat12_directory_count; ++index) {
            const fat12_directory_work_t *work =
                &fat12_directory_queue[index];
            if (work->start_cluster != start) continue;
            uint8_t parent_data[X86OS_STORAGE_BLOCK_SIZE];
            const uint8_t *parent_entry = 0;
            if (fat12_read_topology_parent_entry(resource, work, parent_data,
                                                  &parent_entry) != 0)
                return -84;
            if (index == canonical) continue;
            if (fat12_directory_topology_repair_count >=
                FAT12_MAX_DIRECTORY_TOPOLOGY_REPAIRS) return -28;
            fat12_directory_topology_repair_t *repair =
                &fat12_directory_topology_repairs[
                    fat12_directory_topology_repair_count++];
            repair->parent_directory_sector = work->parent_directory_sector;
            repair->short_crc32 = format_crc32(parent_entry, 32U);
            repair->parent_entry_offset = work->parent_entry_offset;
            repair->start_cluster = work->start_cluster;
            repair->parent_cluster = work->parent_cluster;
            repair->lfn_count = 0U;
            int lfn = fat12_collect_bound_lfn(resource, layout, repair,
                                               parent_entry);
            if (lfn != 0) return lfn;
        }

        uint32_t cluster = start;
        for (uint32_t steps = 0U; steps < layout->cluster_count; ++steps) {
            if (cluster < 2U || cluster > last_cluster ||
                fat12_cluster_references[cluster] != aliases ||
                fat12_cluster_required[cluster] != aliases ||
                (fat12_clone_claimed[cluster] & 2U) != 0U) return -84;
            fat12_clone_claimed[cluster] |= 2U;
            uint32_t next = fat12_entry(fat12_copies[0], cluster);
            if (fat12_is_eoc(next)) break;
            if (next == 0U || next == 1U || next == 0x0FF7U ||
                next < 2U || next > last_cluster) return -84;
            cluster = next;
            if (steps + 1U == layout->cluster_count) return -84;
        }
    }
    for (uint32_t cluster = 2U; cluster <= last_cluster; ++cluster)
        if (fat12_cluster_required[cluster] > 1U &&
            (fat12_clone_claimed[cluster] & 2U) == 0U) return -84;
    uint32_t expected_diagnosis = X86OS_FAT12_RESULT_CHAIN_CROSSLINK |
        (expected_invalid != 0U ? X86OS_FAT12_RESULT_DIRECTORY_INVALID : 0U);
    if (fat12_directory_topology_repair_count == 0U ||
        fat12_directory_invalid_issue_count != expected_invalid ||
        diagnosis != (int)expected_diagnosis) return -84;
    return 0;
}

static int fat12_collect_directory_topology_sectors(void) {
    fat12_directory_repair_sector_count = 0U;
    for (uint32_t index = 0U;
         index < fat12_directory_topology_repair_count; ++index) {
        const fat12_directory_topology_repair_t *repair =
            &fat12_directory_topology_repairs[index];
        for (uint32_t slot_index = 0U;
             slot_index <= repair->lfn_count; ++slot_index) {
            uint32_t sector = slot_index == 0U
                ? repair->parent_directory_sector
                : repair->lfn[slot_index - 1U].sector;
            int known = 0;
            for (uint32_t known_index = 0U;
                 known_index < fat12_directory_repair_sector_count;
                 ++known_index)
                if (fat12_directory_repair_sectors[known_index] == sector) {
                    known = 1;
                    break;
                }
            if (known) continue;
            if (fat12_directory_repair_sector_count >=
                FAT12_MAX_DIRECTORY_REPAIR_SECTORS) return -28;
            fat12_directory_repair_sectors[
                fat12_directory_repair_sector_count++] = sector;
        }
    }
    return fat12_directory_repair_sector_count == 0U ? -84 : 0;
}

static int fat12_update_directory_topology_sector(uint32_t sector,
        uint8_t *data, int apply) {
    uint32_t matches = 0U;
    for (uint32_t index = 0U;
         index < fat12_directory_topology_repair_count; ++index) {
        const fat12_directory_topology_repair_t *repair =
            &fat12_directory_topology_repairs[index];
        if (repair->parent_directory_sector == sector) {
            uint8_t *entry = data + repair->parent_entry_offset;
            uint32_t attributes = entry[11U];
            if (format_crc32(entry, 32U) != repair->short_crc32 ||
                entry[0] == 0U || entry[0] == 0xE5U ||
                fat12_dot_entry_kind(entry) != 0 || attributes == 0x0FU ||
                (attributes & 0xC0U) != 0U ||
                (attributes & 0x18U) != 0x10U ||
                get16(entry + 20U) != 0U ||
                get16(entry + 26U) != repair->start_cluster ||
                get32(entry + 28U) != 0U) return -84;
            if (apply) entry[0] = 0xE5U;
            ++matches;
        }
        for (uint32_t lfn_index = 0U;
             lfn_index < repair->lfn_count; ++lfn_index) {
            const fat12_directory_slot_t *slot = &repair->lfn[lfn_index];
            if (slot->sector != sector) continue;
            uint8_t *entry = data + slot->offset;
            if (entry[0] == 0U || entry[0] == 0xE5U ||
                entry[11U] != 0x0FU ||
                format_crc32(entry, 32U) != slot->crc32) return -84;
            if (apply) entry[0] = 0xE5U;
            ++matches;
        }
    }
    return matches == 0U ? -84 : 0;
}

static int fat12_repair_directory_topology(uint32_t resource) {
    fat12_check_layout_t layout;
    int diagnosis = fat12_check_volume(resource, &layout);
    if (diagnosis < 0 || !layout.reist_layout ||
        fat12_plan_directory_topology(resource, &layout, diagnosis) != 0)
        return -84;

    uint32_t token = 0U;
    if (x86os_storage_maintenance_acquire(resource, 0U, &token) != 0 ||
        token == 0U) return -30;
    int result = fat12_check_volume(resource, &layout);
    if (result != diagnosis || !layout.reist_layout)
        result = -84;
    if (result == diagnosis)
        result = fat12_plan_directory_topology(resource, &layout, diagnosis);
    if (result == 0)
        result = fat12_collect_directory_topology_sectors();
    if (result == 0 &&
        fat12_directory_repair_sector_count > FORMAT_FAT12_JOURNAL_ENTRIES)
        result = -28;

    format_journal_header_t journal;
    if (result == 0 &&
        fat12_load_clean_journal(resource, &layout, &journal) != 0)
        result = -84;
    if (result == 0) {
        fat12_prepare_journal_header(&journal, layout.volume_id,
            journal.sequence + 1U, FAT12_JOURNAL_ACTIVE, 0U);
        result = fat12_write_journal_header(resource, &journal);
    }

    uint8_t sector_data[X86OS_STORAGE_BLOCK_SIZE];
    for (uint32_t index = 0U; result == 0;
         ++index) {
        if (index >= fat12_directory_repair_sector_count) break;
        uint32_t sector = fat12_directory_repair_sectors[index];
        if (x86os_storage_block_read(resource, sector, sector_data) != 0 ||
            fat12_update_directory_topology_sector(sector, sector_data, 0) != 0)
            result = -84;
        if (result == 0)
            result = fat12_record_old_sector(resource, &journal, sector,
                                             sector_data);
        if (result == 0 &&
            x86os_storage_maintenance_renew(resource, token, 0U) != 0)
            result = -30;
    }
    if (result == 0 && x86os_storage_block_flush(resource) != 0) result = -5;
    if (result == 0 &&
        x86os_storage_maintenance_renew(resource, token, 0U) != 0)
        result = -30;

    for (uint32_t index = 0U; result == 0;
         ++index) {
        if (index >= fat12_directory_repair_sector_count) break;
        uint32_t sector = fat12_directory_repair_sectors[index];
        if (x86os_storage_block_read(resource, sector, sector_data) != 0 ||
            fat12_update_directory_topology_sector(sector, sector_data, 1) != 0 ||
            format_write(resource, sector, sector_data) != 0) result = -84;
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
    return result == 0 ? diagnosis |
        (int)X86OS_FAT12_RESULT_DIRECTORY_TOPOLOGY_REPAIRED : result;
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

static int fat12_apply_directory_loop_repairs(
        const fat12_check_layout_t *layout) {
    uint32_t bytes = layout->fat_sectors * X86OS_STORAGE_BLOCK_SIZE;
    fat12_copy_bytes(fat12_repair_fat, fat12_copies[0], bytes);
    uint32_t last_cluster = layout->cluster_count + 1U;
    for (uint32_t repair_index = 0U;
         repair_index < fat12_directory_loop_repair_count; ++repair_index) {
        const fat12_directory_loop_repair_t *repair =
            &fat12_directory_loop_repairs[repair_index];
        if (++fat12_seen_generation == 0U) {
            for (uint32_t cluster = 0U; cluster <= last_cluster; ++cluster)
                fat12_chain_seen[cluster] = 0U;
            fat12_seen_generation = 1U;
        }

        uint32_t cluster = repair->start_cluster;
        int changed = 0;
        for (uint32_t steps = 0U; steps < layout->cluster_count; ++steps) {
            if (cluster < 2U || cluster > last_cluster ||
                fat12_chain_seen[cluster] == fat12_seen_generation)
                return -84;
            fat12_chain_seen[cluster] = fat12_seen_generation;
            uint32_t next = fat12_entry(fat12_repair_fat, cluster);
            if (cluster == repair->cut_cluster) {
                if (next != repair->tail_cluster || next < 2U ||
                    next > last_cluster ||
                    fat12_chain_seen[next] != fat12_seen_generation)
                    return -84;
                fat12_set_entry(fat12_repair_fat, cluster, 0x0FFFU);
                changed = 1;
                break;
            }
            if (next < 2U || next > last_cluster || next == 0x0FF7U ||
                fat12_is_eoc(next)) return -84;
            cluster = next;
        }
        if (!changed) return -84;
    }
    return 0;
}

static int fat12_repair_directory_loops(uint32_t resource) {
    fat12_check_layout_t layout;
    int diagnosis = fat12_check_volume(resource, &layout);
    if (diagnosis != (int)X86OS_FAT12_RESULT_CHAIN_LOOP ||
        !layout.reist_layout || fat12_directory_loop_repair_count == 0U ||
        fat12_loop_issue_count != fat12_directory_loop_repair_count)
        return -84;

    uint32_t token = 0U;
    if (x86os_storage_maintenance_acquire(resource, 0U, &token) != 0 ||
        token == 0U) return -30;

    int result = fat12_check_volume(resource, &layout);
    if (result != diagnosis || !layout.reist_layout ||
        fat12_directory_loop_repair_count == 0U ||
        fat12_loop_issue_count != fat12_directory_loop_repair_count)
        result = -84;
    if (result == diagnosis)
        result = fat12_apply_directory_loop_repairs(&layout);

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
        ? diagnosis |
          (int)X86OS_FAT12_RESULT_DIRECTORY_LOOPS_REPAIRED : result;
}

static int fat12_apply_short_loop_repairs(
        const fat12_check_layout_t *layout) {
    uint32_t bytes = layout->fat_sectors * X86OS_STORAGE_BLOCK_SIZE;
    fat12_copy_bytes(fat12_repair_fat, fat12_copies[0], bytes);
    uint32_t last_cluster = layout->cluster_count + 1U;
    for (uint32_t repair_index = 0U;
         repair_index < fat12_short_loop_repair_count; ++repair_index) {
        const fat12_short_loop_repair_t *repair =
            &fat12_short_loop_repairs[repair_index];
        if (++fat12_seen_generation == 0U) {
            for (uint32_t cluster = 0U; cluster <= last_cluster; ++cluster)
                fat12_chain_seen[cluster] = 0U;
            fat12_seen_generation = 1U;
        }

        uint32_t cluster = repair->start_cluster;
        int changed = 0;
        for (uint32_t index = 0U; index < repair->unique_clusters; ++index) {
            if (cluster < 2U || cluster > last_cluster ||
                fat12_chain_seen[cluster] == fat12_seen_generation)
                return -84;
            fat12_chain_seen[cluster] = fat12_seen_generation;
            uint32_t next = fat12_entry(fat12_repair_fat, cluster);
            if (index + 1U == repair->unique_clusters) {
                if (cluster != repair->cut_cluster ||
                    next != repair->tail_cluster || next < 2U ||
                    next > last_cluster ||
                    fat12_chain_seen[next] != fat12_seen_generation)
                    return -84;
                fat12_set_entry(fat12_repair_fat, cluster, 0x0FFFU);
                changed = 1;
                break;
            }
            if (next < 2U || next > last_cluster || next == 0x0FF7U ||
                fat12_is_eoc(next)) return -84;
            cluster = next;
        }
        if (!changed) return -84;
    }
    return 0;
}

static int fat12_collect_short_loop_directory_sectors(
        const fat12_check_layout_t *layout) {
    fat12_directory_repair_sector_count = 0U;
    for (uint32_t index = 0U;
         index < fat12_short_loop_repair_count; ++index) {
        uint32_t sector = fat12_short_loop_repairs[index].directory_sector;
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

static int fat12_update_short_loop_directory_sector(uint32_t sector,
                                                     uint8_t *data,
                                                     int apply) {
    uint32_t matches = 0U;
    for (uint32_t index = 0U;
         index < fat12_short_loop_repair_count; ++index) {
        const fat12_short_loop_repair_t *repair =
            &fat12_short_loop_repairs[index];
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

static int fat12_repair_short_loops(uint32_t resource) {
    fat12_check_layout_t layout;
    uint32_t diagnosis_flags = X86OS_FAT12_RESULT_CHAIN_LOOP |
                               X86OS_FAT12_RESULT_CHAIN_SHORT;
    int diagnosis = fat12_check_volume(resource, &layout);
    if (diagnosis != (int)diagnosis_flags || !layout.reist_layout ||
        fat12_short_loop_repair_count == 0U ||
        fat12_loop_issue_count != fat12_short_loop_repair_count ||
        fat12_short_issue_count != fat12_short_loop_repair_count)
        return -84;

    uint32_t token = 0U;
    if (x86os_storage_maintenance_acquire(resource, 0U, &token) != 0 ||
        token == 0U) return -30;

    int result = fat12_check_volume(resource, &layout);
    if (result != diagnosis || !layout.reist_layout ||
        fat12_short_loop_repair_count == 0U ||
        fat12_loop_issue_count != fat12_short_loop_repair_count ||
        fat12_short_issue_count != fat12_short_loop_repair_count)
        result = -84;
    if (result == diagnosis)
        result = fat12_apply_short_loop_repairs(&layout);
    if (result == 0)
        result = fat12_collect_short_loop_directory_sectors(&layout);

    format_journal_header_t journal;
    if (result == 0 && fat12_load_clean_journal(resource, &layout, &journal) != 0)
        result = -84;
    if (result == 0) {
        fat12_prepare_journal_header(&journal, layout.volume_id,
            journal.sequence + 1U, FAT12_JOURNAL_ACTIVE, 0U);
        result = fat12_write_journal_header(resource, &journal);
    }

    uint32_t changed_fat_sectors = 0U;
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
            ++changed_fat_sectors;
        }
    }
    if (result == 0 && changed_fat_sectors == 0U) result = -84;

    uint8_t directory_data[X86OS_STORAGE_BLOCK_SIZE];
    for (uint32_t index = 0U;
         result == 0 && index < fat12_directory_repair_sector_count; ++index) {
        uint32_t sector = fat12_directory_repair_sectors[index];
        if (x86os_storage_block_read(resource, sector, directory_data) != 0)
            result = -5;
        if (result == 0)
            result = fat12_update_short_loop_directory_sector(
                sector, directory_data, 0);
        if (result == 0)
            result = fat12_record_old_sector(resource, &journal, sector,
                                             directory_data);
        if (result == 0 &&
            x86os_storage_maintenance_renew(resource, token, 0U) != 0)
            result = -30;
    }

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

    for (uint32_t index = 0U;
         result == 0 && index < fat12_directory_repair_sector_count; ++index) {
        uint32_t sector = fat12_directory_repair_sectors[index];
        if (x86os_storage_block_read(resource, sector, directory_data) != 0)
            result = -5;
        if (result == 0)
            result = fat12_update_short_loop_directory_sector(
                sector, directory_data, 1);
        if (result == 0)
            result = format_write(resource, sector, directory_data);
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
        ? diagnosis | (int)X86OS_FAT12_RESULT_SHORT_LOOPS_REPAIRED : result;
}

static int fat12_collect_directory_size_sectors(
        const fat12_check_layout_t *layout) {
    fat12_directory_repair_sector_count = 0U;
    for (uint32_t index = 0U;
         index < fat12_directory_size_repair_count; ++index) {
        uint32_t sector = fat12_directory_size_repairs[index].directory_sector;
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

static int fat12_update_directory_size_sector(uint32_t sector,
                                               uint8_t *data, int apply) {
    uint32_t matches = 0U;
    for (uint32_t index = 0U;
         index < fat12_directory_size_repair_count; ++index) {
        const fat12_directory_size_repair_t *repair =
            &fat12_directory_size_repairs[index];
        if (repair->directory_sector != sector) continue;
        uint32_t offset = repair->entry_offset;
        if ((offset & 31U) != 0U ||
            offset > X86OS_STORAGE_BLOCK_SIZE - 32U) return -84;
        uint8_t *entry = data + offset;
        uint32_t attributes = entry[11U];
        if (entry[0] == 0U || entry[0] == 0xE5U || attributes == 0x0FU ||
            (attributes & 0xC0U) != 0U || (attributes & 0x18U) != 0x10U ||
            get16(entry + 20U) != 0U ||
            get16(entry + 26U) != repair->start_cluster ||
            get32(entry + 28U) != repair->original_size ||
            repair->original_size == 0U)
            return -84;
        if (apply) put32(entry + 28U, 0U);
        ++matches;
    }
    return matches == 0U ? -84 : 0;
}

static int fat12_repair_directory_sizes(uint32_t resource) {
    fat12_check_layout_t layout;
    int diagnosis = fat12_check_volume(resource, &layout);
    if (diagnosis != (int)X86OS_FAT12_RESULT_DIRECTORY_INVALID ||
        !layout.reist_layout || fat12_directory_size_repair_count == 0U ||
        fat12_directory_invalid_issue_count !=
            fat12_directory_size_repair_count)
        return -84;

    uint32_t token = 0U;
    if (x86os_storage_maintenance_acquire(resource, 0U, &token) != 0 ||
        token == 0U) return -30;

    int result = fat12_check_volume(resource, &layout);
    if (result != diagnosis || !layout.reist_layout ||
        fat12_directory_size_repair_count == 0U ||
        fat12_directory_invalid_issue_count !=
            fat12_directory_size_repair_count)
        result = -84;
    if (result == diagnosis)
        result = fat12_collect_directory_size_sectors(&layout);

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
            result = fat12_update_directory_size_sector(sector, sector_data, 0);
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
            result = fat12_update_directory_size_sector(sector, sector_data, 1);
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
        ? diagnosis | (int)X86OS_FAT12_RESULT_DIRECTORY_SIZE_REPAIRED : result;
}

static int fat12_collect_volume_label_sectors(
        const fat12_check_layout_t *layout) {
    fat12_directory_repair_sector_count = 0U;
    for (uint32_t index = 0U;
         index < fat12_volume_label_repair_count; ++index) {
        uint32_t sector = fat12_volume_label_repairs[index].directory_sector;
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

static int fat12_update_volume_label_sector(uint32_t sector,
                                             uint8_t *data, int apply) {
    uint32_t matches = 0U;
    for (uint32_t index = 0U;
         index < fat12_volume_label_repair_count; ++index) {
        const fat12_volume_label_repair_t *repair =
            &fat12_volume_label_repairs[index];
        if (repair->directory_sector != sector) continue;
        uint32_t offset = repair->entry_offset;
        if ((offset & 31U) != 0U ||
            offset > X86OS_STORAGE_BLOCK_SIZE - 32U) return -84;
        uint8_t *entry = data + offset;
        uint32_t attributes = entry[11U];
        if (entry[0] == 0U || entry[0] == 0xE5U || attributes == 0x0FU ||
            (attributes & 0xC0U) != 0U || (attributes & 0x18U) != 0x08U ||
            get16(entry + 20U) != 0U ||
            get16(entry + 26U) != repair->original_start_cluster ||
            get32(entry + 28U) != repair->original_size ||
            (repair->original_start_cluster == 0U &&
             repair->original_size == 0U))
            return -84;
        if (apply) {
            put16(entry + 26U, 0U);
            put32(entry + 28U, 0U);
        }
        ++matches;
    }
    return matches == 0U ? -84 : 0;
}

static int fat12_repair_volume_labels(uint32_t resource) {
    fat12_check_layout_t layout;
    int diagnosis = fat12_check_volume(resource, &layout);
    if (diagnosis != (int)X86OS_FAT12_RESULT_DIRECTORY_INVALID ||
        !layout.reist_layout || fat12_volume_label_repair_count == 0U ||
        fat12_directory_invalid_issue_count !=
            fat12_volume_label_repair_count)
        return -84;

    uint32_t token = 0U;
    if (x86os_storage_maintenance_acquire(resource, 0U, &token) != 0 ||
        token == 0U) return -30;

    int result = fat12_check_volume(resource, &layout);
    if (result != diagnosis || !layout.reist_layout ||
        fat12_volume_label_repair_count == 0U ||
        fat12_directory_invalid_issue_count !=
            fat12_volume_label_repair_count)
        result = -84;
    if (result == diagnosis)
        result = fat12_collect_volume_label_sectors(&layout);

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
            result = fat12_update_volume_label_sector(sector, sector_data, 0);
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
            result = fat12_update_volume_label_sector(sector, sector_data, 1);
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
        ? diagnosis | (int)X86OS_FAT12_RESULT_VOLUME_LABEL_REPAIRED : result;
}

static int fat12_apply_zero_file_repairs(
        const fat12_check_layout_t *layout) {
    uint32_t bytes = layout->fat_sectors * X86OS_STORAGE_BLOCK_SIZE;
    fat12_copy_bytes(fat12_repair_fat, fat12_copies[0], bytes);
    uint32_t last_cluster = layout->cluster_count + 1U;
    for (uint32_t repair_index = 0U;
         repair_index < fat12_zero_file_repair_count; ++repair_index) {
        const fat12_zero_file_repair_t *repair =
            &fat12_zero_file_repairs[repair_index];
        if (++fat12_seen_generation == 0U) {
            for (uint32_t cluster = 0U; cluster <= last_cluster; ++cluster)
                fat12_chain_seen[cluster] = 0U;
            fat12_seen_generation = 1U;
        }
        uint32_t cluster = repair->start_cluster;
        uint16_t owner = 0U;
        for (uint32_t index = 0U; index < repair->cluster_count; ++index) {
            if (cluster < 2U || cluster > last_cluster ||
                fat12_chain_seen[cluster] == fat12_seen_generation ||
                fat12_cluster_references[cluster] != 1U ||
                fat12_cluster_required[cluster] != 0U ||
                fat12_cluster_owner[cluster] == 0U)
                return -84;
            fat12_chain_seen[cluster] = fat12_seen_generation;
            if (owner == 0U) owner = fat12_cluster_owner[cluster];
            if (fat12_cluster_owner[cluster] != owner) return -84;
            uint32_t next = fat12_entry(fat12_repair_fat, cluster);
            if (index + 1U == repair->cluster_count) {
                if (!fat12_is_eoc(next)) return -84;
            } else if (next < 2U || next > last_cluster ||
                       next == 0x0FF7U || fat12_is_eoc(next)) {
                return -84;
            }
            fat12_set_entry(fat12_repair_fat, cluster, 0U);
            cluster = next;
        }
    }
    return 0;
}

static int fat12_collect_zero_file_directory_sectors(
        const fat12_check_layout_t *layout) {
    fat12_directory_repair_sector_count = 0U;
    for (uint32_t index = 0U;
         index < fat12_zero_file_repair_count; ++index) {
        uint32_t sector = fat12_zero_file_repairs[index].directory_sector;
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

static int fat12_update_zero_file_directory_sector(uint32_t sector,
                                                    uint8_t *data,
                                                    int apply) {
    uint32_t matches = 0U;
    for (uint32_t index = 0U;
         index < fat12_zero_file_repair_count; ++index) {
        const fat12_zero_file_repair_t *repair =
            &fat12_zero_file_repairs[index];
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
            get32(entry + 28U) != 0U || repair->cluster_count == 0U)
            return -84;
        if (apply) put16(entry + 26U, 0U);
        ++matches;
    }
    return matches == 0U ? -84 : 0;
}

static int fat12_repair_zero_files(uint32_t resource) {
    fat12_check_layout_t layout;
    uint32_t diagnosis_flags = X86OS_FAT12_RESULT_CHAIN_EXCESS |
                               X86OS_FAT12_RESULT_DIRECTORY_INVALID;
    int diagnosis = fat12_check_volume(resource, &layout);
    if (diagnosis != (int)diagnosis_flags || !layout.reist_layout ||
        fat12_zero_file_repair_count == 0U ||
        fat12_excess_issue_count != fat12_zero_file_repair_count ||
        fat12_directory_invalid_issue_count != fat12_zero_file_repair_count)
        return -84;

    uint32_t token = 0U;
    if (x86os_storage_maintenance_acquire(resource, 0U, &token) != 0 ||
        token == 0U) return -30;

    int result = fat12_check_volume(resource, &layout);
    if (result != diagnosis || !layout.reist_layout ||
        fat12_zero_file_repair_count == 0U ||
        fat12_excess_issue_count != fat12_zero_file_repair_count ||
        fat12_directory_invalid_issue_count != fat12_zero_file_repair_count)
        result = -84;
    if (result == diagnosis) result = fat12_apply_zero_file_repairs(&layout);
    if (result == 0)
        result = fat12_collect_zero_file_directory_sectors(&layout);

    uint32_t changed_fat_sectors = 0U;
    for (uint32_t copy = 0U; result == 0 && copy < 2U; ++copy) {
        for (uint32_t sector = 0U; sector < layout.fat_sectors; ++sector) {
            const uint8_t *replacement = fat12_repair_fat +
                sector * X86OS_STORAGE_BLOCK_SIZE;
            const uint8_t *old = fat12_copies[copy] +
                sector * X86OS_STORAGE_BLOCK_SIZE;
            if (!format_equal(replacement, old, X86OS_STORAGE_BLOCK_SIZE))
                ++changed_fat_sectors;
        }
    }
    if (result == 0 && (changed_fat_sectors == 0U ||
        changed_fat_sectors > FORMAT_FAT12_JOURNAL_ENTRIES -
            fat12_directory_repair_sector_count))
        result = -28;

    format_journal_header_t journal;
    if (result == 0 && fat12_load_clean_journal(resource, &layout, &journal) != 0)
        result = -84;
    if (result == 0) {
        fat12_prepare_journal_header(&journal, layout.volume_id,
            journal.sequence + 1U, FAT12_JOURNAL_ACTIVE, 0U);
        result = fat12_write_journal_header(resource, &journal);
    }

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
        }
    }

    uint8_t directory_data[X86OS_STORAGE_BLOCK_SIZE];
    for (uint32_t index = 0U; result == 0 &&
         index < fat12_directory_repair_sector_count; ++index) {
        uint32_t sector = fat12_directory_repair_sectors[index];
        if (x86os_storage_block_read(resource, sector, directory_data) != 0)
            result = -5;
        if (result == 0)
            result = fat12_update_zero_file_directory_sector(
                sector, directory_data, 0);
        if (result == 0)
            result = fat12_record_old_sector(resource, &journal, sector,
                                             directory_data);
        if (result == 0 &&
            x86os_storage_maintenance_renew(resource, token, 0U) != 0)
            result = -30;
    }

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

    for (uint32_t index = 0U; result == 0 &&
         index < fat12_directory_repair_sector_count; ++index) {
        uint32_t sector = fat12_directory_repair_sectors[index];
        if (x86os_storage_block_read(resource, sector, directory_data) != 0)
            result = -5;
        if (result == 0)
            result = fat12_update_zero_file_directory_sector(
                sector, directory_data, 1);
        if (result == 0)
            result = format_write(resource, sector, directory_data);
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
        ? diagnosis | (int)X86OS_FAT12_RESULT_ZERO_FILES_REPAIRED : result;
}

static int fat12_collect_zero_start_directory_sectors(
        const fat12_check_layout_t *layout) {
    fat12_directory_repair_sector_count = 0U;
    for (uint32_t index = 0U;
         index < fat12_zero_start_repair_count; ++index) {
        uint32_t sector = fat12_zero_start_repairs[index].directory_sector;
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

static int fat12_update_zero_start_directory_sector(uint32_t sector,
                                                     uint8_t *data,
                                                     int apply) {
    uint32_t matches = 0U;
    for (uint32_t index = 0U;
         index < fat12_zero_start_repair_count; ++index) {
        const fat12_zero_start_repair_t *repair =
            &fat12_zero_start_repairs[index];
        if (repair->directory_sector != sector) continue;
        uint32_t offset = repair->entry_offset;
        if ((offset & 31U) != 0U ||
            offset > X86OS_STORAGE_BLOCK_SIZE - 32U) return -84;
        uint8_t *entry = data + offset;
        uint32_t attributes = entry[11U];
        if (entry[0] == 0U || entry[0] == 0xE5U || attributes == 0x0FU ||
            (attributes & 0xC0U) != 0U || (attributes & 0x18U) != 0U ||
            get16(entry + 20U) != 0U || get16(entry + 26U) != 0U ||
            get32(entry + 28U) != repair->original_size ||
            repair->original_size == 0U)
            return -84;
        if (apply) put32(entry + 28U, 0U);
        ++matches;
    }
    return matches == 0U ? -84 : 0;
}

static int fat12_repair_zero_start_files(uint32_t resource) {
    fat12_check_layout_t layout;
    int diagnosis = fat12_check_volume(resource, &layout);
    if (diagnosis != (int)X86OS_FAT12_RESULT_CHAIN_SHORT ||
        !layout.reist_layout || fat12_zero_start_repair_count == 0U ||
        fat12_short_issue_count != fat12_zero_start_repair_count)
        return -84;

    uint32_t token = 0U;
    if (x86os_storage_maintenance_acquire(resource, 0U, &token) != 0 ||
        token == 0U) return -30;

    int result = fat12_check_volume(resource, &layout);
    if (result != diagnosis || !layout.reist_layout ||
        fat12_zero_start_repair_count == 0U ||
        fat12_short_issue_count != fat12_zero_start_repair_count)
        result = -84;
    if (result == diagnosis)
        result = fat12_collect_zero_start_directory_sectors(&layout);

    format_journal_header_t journal;
    if (result == 0 && fat12_load_clean_journal(resource, &layout, &journal) != 0)
        result = -84;
    if (result == 0) {
        fat12_prepare_journal_header(&journal, layout.volume_id,
            journal.sequence + 1U, FAT12_JOURNAL_ACTIVE, 0U);
        result = fat12_write_journal_header(resource, &journal);
    }

    uint8_t sector_data[X86OS_STORAGE_BLOCK_SIZE];
    for (uint32_t index = 0U; result == 0 &&
         index < fat12_directory_repair_sector_count; ++index) {
        uint32_t sector = fat12_directory_repair_sectors[index];
        if (x86os_storage_block_read(resource, sector, sector_data) != 0)
            result = -5;
        if (result == 0)
            result = fat12_update_zero_start_directory_sector(
                sector, sector_data, 0);
        if (result == 0)
            result = fat12_record_old_sector(resource, &journal, sector,
                                             sector_data);
        if (result == 0 &&
            x86os_storage_maintenance_renew(resource, token, 0U) != 0)
            result = -30;
    }

    for (uint32_t index = 0U; result == 0 &&
         index < fat12_directory_repair_sector_count; ++index) {
        uint32_t sector = fat12_directory_repair_sectors[index];
        if (x86os_storage_block_read(resource, sector, sector_data) != 0)
            result = -5;
        if (result == 0)
            result = fat12_update_zero_start_directory_sector(
                sector, sector_data, 1);
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
        ? diagnosis |
          (int)X86OS_FAT12_RESULT_ZERO_START_FILES_REPAIRED : result;
}

static int fat12_collect_dot_size_sectors(
        const fat12_check_layout_t *layout) {
    fat12_directory_repair_sector_count = 0U;
    for (uint32_t index = 0U;
         index < fat12_dot_size_repair_count; ++index) {
        uint32_t sector = fat12_dot_size_repairs[index].directory_sector;
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

static int fat12_update_dot_size_sector(uint32_t sector,
                                        uint8_t *data, int apply) {
    uint32_t matches = 0U;
    for (uint32_t index = 0U;
         index < fat12_dot_size_repair_count; ++index) {
        const fat12_dot_size_repair_t *repair =
            &fat12_dot_size_repairs[index];
        if (repair->directory_sector != sector) continue;
        uint32_t offset = repair->entry_offset;
        if ((offset & 31U) != 0U ||
            offset > X86OS_STORAGE_BLOCK_SIZE - 32U) return -84;
        uint8_t *entry = data + offset;
        uint32_t attributes = entry[11U];
        int dot_kind = fat12_dot_entry_kind(entry);
        if (entry[0] == 0U || entry[0] == 0xE5U || attributes == 0x0FU ||
            (attributes & 0xC0U) != 0U || (attributes & 0x18U) != 0x10U ||
            get16(entry + 20U) != 0U ||
            get16(entry + 26U) != repair->expected_start_cluster ||
            get32(entry + 28U) != repair->original_size ||
            repair->original_size == 0U || dot_kind != repair->dot_kind)
            return -84;
        if (apply) put32(entry + 28U, 0U);
        ++matches;
    }
    return matches == 0U ? -84 : 0;
}

static int fat12_repair_dot_sizes(uint32_t resource) {
    fat12_check_layout_t layout;
    int diagnosis = fat12_check_volume(resource, &layout);
    if (diagnosis != (int)X86OS_FAT12_RESULT_DIRECTORY_INVALID ||
        !layout.reist_layout || fat12_dot_size_repair_count == 0U ||
        fat12_directory_invalid_issue_count != fat12_dot_size_repair_count)
        return -84;

    uint32_t token = 0U;
    if (x86os_storage_maintenance_acquire(resource, 0U, &token) != 0 ||
        token == 0U) return -30;

    int result = fat12_check_volume(resource, &layout);
    if (result != diagnosis || !layout.reist_layout ||
        fat12_dot_size_repair_count == 0U ||
        fat12_directory_invalid_issue_count != fat12_dot_size_repair_count)
        result = -84;
    if (result == diagnosis)
        result = fat12_collect_dot_size_sectors(&layout);

    format_journal_header_t journal;
    if (result == 0 && fat12_load_clean_journal(resource, &layout, &journal) != 0)
        result = -84;
    if (result == 0) {
        fat12_prepare_journal_header(&journal, layout.volume_id,
            journal.sequence + 1U, FAT12_JOURNAL_ACTIVE, 0U);
        result = fat12_write_journal_header(resource, &journal);
    }

    uint8_t sector_data[X86OS_STORAGE_BLOCK_SIZE];
    for (uint32_t index = 0U; result == 0 &&
         index < fat12_directory_repair_sector_count; ++index) {
        uint32_t sector = fat12_directory_repair_sectors[index];
        if (x86os_storage_block_read(resource, sector, sector_data) != 0)
            result = -5;
        if (result == 0)
            result = fat12_update_dot_size_sector(sector, sector_data, 0);
        if (result == 0)
            result = fat12_record_old_sector(resource, &journal, sector,
                                             sector_data);
        if (result == 0 &&
            x86os_storage_maintenance_renew(resource, token, 0U) != 0)
            result = -30;
    }

    for (uint32_t index = 0U; result == 0 &&
         index < fat12_directory_repair_sector_count; ++index) {
        uint32_t sector = fat12_directory_repair_sectors[index];
        if (x86os_storage_block_read(resource, sector, sector_data) != 0)
            result = -5;
        if (result == 0)
            result = fat12_update_dot_size_sector(sector, sector_data, 1);
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
        ? diagnosis | (int)X86OS_FAT12_RESULT_DOT_SIZE_REPAIRED : result;
}

static int fat12_collect_dot_cluster_sectors(
        const fat12_check_layout_t *layout) {
    fat12_directory_repair_sector_count = 0U;
    for (uint32_t index = 0U;
         index < fat12_dot_cluster_repair_count; ++index) {
        uint32_t sector = fat12_dot_cluster_repairs[index].directory_sector;
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

static int fat12_update_dot_cluster_sector(uint32_t sector,
                                           uint8_t *data, int apply) {
    uint32_t matches = 0U;
    for (uint32_t index = 0U;
         index < fat12_dot_cluster_repair_count; ++index) {
        const fat12_dot_cluster_repair_t *repair =
            &fat12_dot_cluster_repairs[index];
        if (repair->directory_sector != sector) continue;
        uint32_t offset = repair->entry_offset;
        if ((offset & 31U) != 0U ||
            offset > X86OS_STORAGE_BLOCK_SIZE - 32U) return -84;
        uint8_t *entry = data + offset;
        uint32_t attributes = entry[11U];
        int dot_kind = fat12_dot_entry_kind(entry);
        if (entry[0] == 0U || entry[0] == 0xE5U || attributes == 0x0FU ||
            (attributes & 0xC0U) != 0U || (attributes & 0x18U) != 0x10U ||
            get16(entry + 20U) != 0U ||
            get16(entry + 26U) != repair->original_start_cluster ||
            get32(entry + 28U) != 0U ||
            repair->original_start_cluster == repair->expected_start_cluster ||
            dot_kind != repair->dot_kind)
            return -84;
        if (apply)
            put16(entry + 26U, repair->expected_start_cluster);
        ++matches;
    }
    return matches == 0U ? -84 : 0;
}

static int fat12_repair_dot_clusters(uint32_t resource) {
    fat12_check_layout_t layout;
    int diagnosis = fat12_check_volume(resource, &layout);
    if (diagnosis != (int)X86OS_FAT12_RESULT_DIRECTORY_INVALID ||
        !layout.reist_layout || fat12_dot_cluster_repair_count == 0U ||
        fat12_directory_invalid_issue_count != fat12_dot_cluster_repair_count)
        return -84;

    uint32_t token = 0U;
    if (x86os_storage_maintenance_acquire(resource, 0U, &token) != 0 ||
        token == 0U) return -30;

    int result = fat12_check_volume(resource, &layout);
    if (result != diagnosis || !layout.reist_layout ||
        fat12_dot_cluster_repair_count == 0U ||
        fat12_directory_invalid_issue_count != fat12_dot_cluster_repair_count)
        result = -84;
    if (result == diagnosis)
        result = fat12_collect_dot_cluster_sectors(&layout);

    format_journal_header_t journal;
    if (result == 0 && fat12_load_clean_journal(resource, &layout, &journal) != 0)
        result = -84;
    if (result == 0) {
        fat12_prepare_journal_header(&journal, layout.volume_id,
            journal.sequence + 1U, FAT12_JOURNAL_ACTIVE, 0U);
        result = fat12_write_journal_header(resource, &journal);
    }

    uint8_t sector_data[X86OS_STORAGE_BLOCK_SIZE];
    for (uint32_t index = 0U; result == 0 &&
         index < fat12_directory_repair_sector_count; ++index) {
        uint32_t sector = fat12_directory_repair_sectors[index];
        if (x86os_storage_block_read(resource, sector, sector_data) != 0)
            result = -5;
        if (result == 0)
            result = fat12_update_dot_cluster_sector(sector, sector_data, 0);
        if (result == 0)
            result = fat12_record_old_sector(resource, &journal, sector,
                                             sector_data);
        if (result == 0 &&
            x86os_storage_maintenance_renew(resource, token, 0U) != 0)
            result = -30;
    }

    for (uint32_t index = 0U; result == 0 &&
         index < fat12_directory_repair_sector_count; ++index) {
        uint32_t sector = fat12_directory_repair_sectors[index];
        if (x86os_storage_block_read(resource, sector, sector_data) != 0)
            result = -5;
        if (result == 0)
            result = fat12_update_dot_cluster_sector(sector, sector_data, 1);
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
        ? diagnosis | (int)X86OS_FAT12_RESULT_DOT_CLUSTER_REPAIRED : result;
}

static void fat12_make_orphan_name(uint8_t *name, uint32_t start_cluster) {
    name[0] = 'F'; name[1] = 'I'; name[2] = 'L'; name[3] = 'E';
    name[4] = (uint8_t)('0' + (start_cluster / 1000U) % 10U);
    name[5] = (uint8_t)('0' + (start_cluster / 100U) % 10U);
    name[6] = (uint8_t)('0' + (start_cluster / 10U) % 10U);
    name[7] = (uint8_t)('0' + start_cluster % 10U);
    name[8] = 'C'; name[9] = 'H'; name[10] = 'K';
}

static int fat12_plan_orphan_chains(const fat12_check_layout_t *layout) {
    uint32_t last_cluster = layout->cluster_count + 1U;
    fat12_orphan_chain_count = 0U;
    for (uint32_t cluster = 0U; cluster <= last_cluster; ++cluster) {
        fat12_regular_references[cluster] = 0U;
        fat12_clone_remaining[cluster] = 0U;
        fat12_clone_claimed[cluster] = 0U;
    }
    for (uint32_t cluster = 2U; cluster <= last_cluster; ++cluster) {
        uint32_t value = fat12_entry(fat12_copies[0], cluster);
        if (value != 0U && value != 0x0FF7U &&
            fat12_cluster_owner[cluster] == 0U)
            fat12_regular_references[cluster] = 1U;
    }
    for (uint32_t cluster = 2U; cluster <= last_cluster; ++cluster) {
        if (fat12_regular_references[cluster] == 0U) continue;
        uint32_t next = fat12_entry(fat12_copies[0], cluster);
        if (fat12_is_eoc(next)) continue;
        if (next < 2U || next > last_cluster || next == 0x0FF7U ||
            fat12_regular_references[next] == 0U ||
            fat12_clone_remaining[next] != 0U) return -84;
        fat12_clone_remaining[next] = 1U;
    }
    for (uint32_t head = 2U; head <= last_cluster; ++head) {
        if (fat12_regular_references[head] == 0U ||
            fat12_clone_remaining[head] != 0U) continue;
        if (fat12_orphan_chain_count >= FAT12_MAX_ORPHAN_CHAINS) return -28;
        uint32_t cluster = head;
        uint32_t count = 0U;
        int normal_end = 0;
        for (uint32_t steps = 0U; steps < layout->cluster_count; ++steps) {
            if (cluster < 2U || cluster > last_cluster ||
                fat12_regular_references[cluster] == 0U ||
                fat12_clone_claimed[cluster] != 0U) return -84;
            fat12_clone_claimed[cluster] = 1U;
            ++count;
            uint32_t next = fat12_entry(fat12_copies[0], cluster);
            if (fat12_is_eoc(next)) {
                normal_end = 1;
                break;
            }
            if (next < 2U || next > last_cluster || next == 0x0FF7U ||
                fat12_regular_references[next] == 0U) return -84;
            cluster = next;
        }
        if (!normal_end || count == 0U || count > UINT16_MAX) return -84;
        fat12_orphan_chains[fat12_orphan_chain_count].start_cluster =
            (uint16_t)head;
        fat12_orphan_chains[fat12_orphan_chain_count].cluster_count =
            (uint16_t)count;
        ++fat12_orphan_chain_count;
    }
    for (uint32_t cluster = 2U; cluster <= last_cluster; ++cluster)
        if (fat12_regular_references[cluster] != 0U &&
            fat12_clone_claimed[cluster] == 0U) return -84;
    return fat12_orphan_chain_count == 0U ? -84 : 0;
}

static int fat12_orphan_name_collision(const uint8_t *entry) {
    uint8_t expected[11U];
    for (uint32_t index = 0U; index < fat12_orphan_chain_count; ++index) {
        fat12_make_orphan_name(expected,
            fat12_orphan_chains[index].start_cluster);
        if (format_equal(entry, expected, sizeof(expected))) return 1;
    }
    return 0;
}

static fat12_salvage_sector_t *fat12_salvage_sector(uint32_t resource,
        uint32_t sector) {
    for (uint32_t index = 0U; index < fat12_salvage_sector_count; ++index)
        if (fat12_salvage_sectors[index].sector == sector)
            return &fat12_salvage_sectors[index];
    if (fat12_salvage_sector_count >= FAT12_MAX_SALVAGE_SECTORS) return 0;
    fat12_salvage_sector_t *planned =
        &fat12_salvage_sectors[fat12_salvage_sector_count++];
    planned->sector = sector;
    planned->new_directory_data = 0U;
    if (x86os_storage_block_read(resource, sector, planned->data) != 0) {
        --fat12_salvage_sector_count;
        return 0;
    }
    planned->original_crc32 = format_crc32(planned->data,
                                           sizeof(planned->data));
    return planned;
}

static int fat12_find_found_directory(uint32_t resource,
        const fat12_check_layout_t *layout, uint16_t *found_start,
        fat12_salvage_slot_t *root_slot) {
    static const uint8_t found_name[11U] = {
        'F','O','U','N','D',' ',' ',' ','0','0','0'
    };
    uint32_t root_start = layout->reserved_sectors +
                          2U * layout->fat_sectors;
    found_start[0] = 0U;
    root_slot->sector = UINT32_MAX;
    root_slot->offset = 0U;
    int end = 0;
    int previous_active_lfn = 0;
    for (uint32_t sector_index = 0U;
         sector_index < layout->root_sectors && !end; ++sector_index) {
        uint32_t sector = root_start + sector_index;
        uint8_t data[X86OS_STORAGE_BLOCK_SIZE];
        if (x86os_storage_block_read(resource, sector, data) != 0) return -5;
        for (uint32_t offset = 0U; offset < X86OS_STORAGE_BLOCK_SIZE;
             offset += 32U) {
            const uint8_t *entry = data + offset;
            if (entry[0] == 0U) {
                root_slot->sector = sector;
                root_slot->offset = (uint16_t)offset;
                end = 1;
                break;
            }
            if (entry[0] == 0xE5U) {
                if (root_slot->sector == UINT32_MAX &&
                    !previous_active_lfn) {
                    root_slot->sector = sector;
                    root_slot->offset = (uint16_t)offset;
                }
                previous_active_lfn = 0;
                continue;
            }
            if (!format_equal(entry, found_name, sizeof(found_name))) {
                previous_active_lfn = entry[11U] == 0x0FU;
                continue;
            }
            uint32_t attributes = entry[11U];
            uint32_t start = get16(entry + 26U);
            if (found_start[0] != 0U || attributes == 0x0FU ||
                (attributes & 0xC0U) != 0U ||
                (attributes & 0x18U) != 0x10U ||
                get16(entry + 20U) != 0U || get32(entry + 28U) != 0U ||
                start < 2U || start > layout->cluster_count + 1U)
                return -84;
            found_start[0] = (uint16_t)start;
            previous_active_lfn = 0;
        }
    }
    return found_start[0] == 0U && root_slot->sector == UINT32_MAX ? -28 : 0;
}

static int fat12_collect_found_slots(uint32_t resource,
        const fat12_check_layout_t *layout, uint32_t found_start,
        uint16_t *last_cluster, uint32_t *deleted_count,
        uint32_t *end_count) {
    uint16_t dot_parent = 0U;
    if (fat12_read_directory_header(resource, layout, found_start,
                                    &dot_parent) != 0 || dot_parent != 0U)
        return -84;
    deleted_count[0] = 0U;
    end_count[0] = 0U;
    uint32_t cluster = found_start;
    uint32_t last = layout->cluster_count + 1U;
    int after_end = 0;
    int previous_active_lfn = 0;
    for (uint32_t steps = 0U; steps < layout->cluster_count; ++steps) {
        if (cluster < 2U || cluster > last) return -84;
        uint32_t first_sector = layout->data_start +
            (cluster - 2U) * layout->sectors_per_cluster;
        for (uint32_t sector_index = 0U;
             sector_index < layout->sectors_per_cluster; ++sector_index) {
            uint32_t sector = first_sector + sector_index;
            uint8_t data[X86OS_STORAGE_BLOCK_SIZE];
            if (x86os_storage_block_read(resource, sector, data) != 0)
                return -5;
            for (uint32_t offset = 0U; offset < X86OS_STORAGE_BLOCK_SIZE;
                 offset += 32U) {
                if (steps == 0U && sector_index == 0U && offset < 64U)
                    continue;
                const uint8_t *entry = data + offset;
                if (!after_end && entry[0] == 0U) {
                    after_end = 1;
                    previous_active_lfn = 0;
                }
                if (after_end) {
                    if (end_count[0] < FAT12_MAX_SALVAGE_SLOTS) {
                        fat12_salvage_end_slots[end_count[0]].sector = sector;
                        fat12_salvage_end_slots[end_count[0]].offset =
                            (uint16_t)offset;
                        ++end_count[0];
                    }
                    continue;
                }
                if (entry[0] == 0xE5U) {
                    if (!previous_active_lfn &&
                        deleted_count[0] < FAT12_MAX_ORPHAN_CHAINS) {
                        fat12_salvage_deleted_slots[deleted_count[0]].sector =
                            sector;
                        fat12_salvage_deleted_slots[deleted_count[0]].offset =
                            (uint16_t)offset;
                        ++deleted_count[0];
                    }
                    previous_active_lfn = 0;
                    continue;
                }
                if (entry[11U] != 0x0FU &&
                    fat12_orphan_name_collision(entry)) return -84;
                previous_active_lfn = entry[11U] == 0x0FU;
            }
        }
        last_cluster[0] = (uint16_t)cluster;
        uint32_t next = fat12_entry(fat12_copies[0], cluster);
        if (fat12_is_eoc(next)) return 0;
        if (next < 2U || next > last || next == 0x0FF7U) return -84;
        cluster = next;
    }
    return -84;
}

static int fat12_allocate_salvage_cluster(
        const fat12_check_layout_t *layout, uint16_t *cluster_out) {
    uint32_t last = layout->cluster_count + 1U;
    for (uint32_t cluster = 2U; cluster <= last; ++cluster) {
        if (fat12_entry(fat12_repair_fat, cluster) != 0U ||
            fat12_cluster_owner[cluster] != 0U ||
            fat12_regular_references[cluster] != 0U) continue;
        fat12_set_entry(fat12_repair_fat, cluster, 0x0FFFU);
        fat12_clone_claimed[cluster] |= 2U;
        cluster_out[0] = (uint16_t)cluster;
        return 0;
    }
    return -28;
}

static int fat12_append_new_found_cluster(uint32_t resource,
        const fat12_check_layout_t *layout, uint32_t cluster,
        int initialize_dot, uint32_t *end_count) {
    uint32_t first_sector = layout->data_start +
        (cluster - 2U) * layout->sectors_per_cluster;
    for (uint32_t sector_index = 0U;
         sector_index < layout->sectors_per_cluster; ++sector_index) {
        fat12_salvage_sector_t *planned = fat12_salvage_sector(
            resource, first_sector + sector_index);
        if (planned == 0) return -28;
        format_fill(planned->data, 0U, sizeof(planned->data));
        planned->new_directory_data = 1U;
        uint32_t first_offset = initialize_dot && sector_index == 0U ? 64U : 0U;
        for (uint32_t offset = first_offset;
             offset < X86OS_STORAGE_BLOCK_SIZE; offset += 32U) {
            if (end_count[0] >= FAT12_MAX_SALVAGE_SLOTS) break;
            fat12_salvage_end_slots[end_count[0]].sector =
                first_sector + sector_index;
            fat12_salvage_end_slots[end_count[0]].offset = (uint16_t)offset;
            ++end_count[0];
        }
    }
    if (initialize_dot) {
        fat12_salvage_sector_t *planned = fat12_salvage_sector(
            resource, first_sector);
        if (planned == 0) return -28;
        uint8_t *dot = planned->data;
        uint8_t *dotdot = planned->data + 32U;
        for (uint32_t index = 0U; index < 11U; ++index) {
            dot[index] = ' ';
            dotdot[index] = ' ';
        }
        dot[0] = '.';
        dotdot[0] = '.'; dotdot[1] = '.';
        dot[11U] = 0x10U; dotdot[11U] = 0x10U;
        put16(dot + 26U, cluster);
        put16(dotdot + 26U, 0U);
    }
    return 0;
}

static int fat12_plan_orphan_salvage(uint32_t resource,
        const fat12_check_layout_t *layout) {
    int chain_result = fat12_plan_orphan_chains(layout);
    if (chain_result != 0) return chain_result;
    uint32_t fat_bytes = layout->fat_sectors * X86OS_STORAGE_BLOCK_SIZE;
    fat12_copy_bytes(fat12_repair_fat, fat12_copies[0], fat_bytes);
    fat12_salvage_sector_count = 0U;

    uint16_t found_start = 0U;
    fat12_salvage_slot_t root_slot;
    int result = fat12_find_found_directory(resource, layout, &found_start,
                                             &root_slot);
    if (result != 0) return result;
    int create_found = found_start == 0U;
    uint16_t last_cluster = 0U;
    uint32_t deleted_count = 0U;
    uint32_t end_count = 0U;
    if (found_start != 0U) {
        result = fat12_collect_found_slots(resource, layout, found_start,
            &last_cluster, &deleted_count, &end_count);
        if (result != 0) return result;
    }
    uint32_t deleted_used = deleted_count < fat12_orphan_chain_count
        ? deleted_count : fat12_orphan_chain_count;
    uint32_t remaining = fat12_orphan_chain_count - deleted_used;
    while (found_start == 0U || end_count < remaining + 1U) {
        uint16_t new_cluster = 0U;
        result = fat12_allocate_salvage_cluster(layout, &new_cluster);
        if (result != 0) return result;
        int initialize_dot = found_start == 0U;
        if (last_cluster != 0U)
            fat12_set_entry(fat12_repair_fat, last_cluster, new_cluster);
        else
            found_start = new_cluster;
        last_cluster = new_cluster;
        result = fat12_append_new_found_cluster(resource, layout, new_cluster,
                                                 initialize_dot, &end_count);
        if (result != 0) return result;
    }
    if (create_found) {
        fat12_salvage_sector_t *root = fat12_salvage_sector(resource,
                                                            root_slot.sector);
        if (root == 0) return -28;
        uint8_t *entry = root->data + root_slot.offset;
        if (entry[0] != 0U && entry[0] != 0xE5U) return -84;
        int replaced_end = entry[0] == 0U;
        format_fill(entry, 0U, 32U);
        static const uint8_t found_name[11U] = {
            'F','O','U','N','D',' ',' ',' ','0','0','0'
        };
        fat12_copy_bytes(entry, found_name, sizeof(found_name));
        entry[11U] = 0x10U;
        put16(entry + 26U, found_start);
        if (replaced_end) {
            uint32_t next_sector = root_slot.sector;
            uint32_t next_offset = root_slot.offset + 32U;
            if (next_offset >= X86OS_STORAGE_BLOCK_SIZE) {
                next_offset = 0U;
                ++next_sector;
            }
            uint32_t root_start = layout->reserved_sectors +
                                  2U * layout->fat_sectors;
            if (next_sector < root_start + layout->root_sectors) {
                fat12_salvage_sector_t *next = fat12_salvage_sector(
                    resource, next_sector);
                if (next == 0) return -28;
                next->data[next_offset] = 0U;
            }
        }
    }

    for (uint32_t index = 0U; index < deleted_used; ++index)
        fat12_salvage_targets[index] = fat12_salvage_deleted_slots[index];
    for (uint32_t index = 0U; index < remaining; ++index)
        fat12_salvage_targets[deleted_used + index] =
            fat12_salvage_end_slots[index];
    for (uint32_t index = 0U; index < fat12_orphan_chain_count; ++index) {
        const fat12_salvage_slot_t *slot = &fat12_salvage_targets[index];
        fat12_salvage_sector_t *planned = fat12_salvage_sector(resource,
                                                               slot->sector);
        if (planned == 0) return -28;
        uint8_t *entry = planned->data + slot->offset;
        format_fill(entry, 0U, 32U);
        fat12_make_orphan_name(entry,
            fat12_orphan_chains[index].start_cluster);
        entry[11U] = 0x20U;
        put16(entry + 26U, fat12_orphan_chains[index].start_cluster);
        uint32_t size = (uint32_t)fat12_orphan_chains[index].cluster_count *
            layout->sectors_per_cluster * X86OS_STORAGE_BLOCK_SIZE;
        put32(entry + 28U, size);
    }
    if (remaining != 0U) {
        const fat12_salvage_slot_t *end =
            &fat12_salvage_end_slots[remaining];
        fat12_salvage_sector_t *planned = fat12_salvage_sector(resource,
                                                               end->sector);
        if (planned == 0) return -28;
        planned->data[end->offset] = 0U;
    }
    return 0;
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

static int fat12_salvage_orphans(uint32_t resource) {
    fat12_check_layout_t layout;
    int diagnosis = fat12_check_volume(resource, &layout);
    if (diagnosis != (int)X86OS_FAT12_RESULT_ORPHAN_CLUSTER ||
        !layout.reist_layout ||
        fat12_plan_orphan_salvage(resource, &layout) != 0) return -84;

    uint32_t token = 0U;
    if (x86os_storage_maintenance_acquire(resource, 0U, &token) != 0 ||
        token == 0U) return -30;
    int result = fat12_check_volume(resource, &layout);
    if (result != diagnosis || !layout.reist_layout)
        result = -84;
    if (result == diagnosis)
        result = fat12_plan_orphan_salvage(resource, &layout);

    uint32_t changed_fat_sectors = 0U;
    for (uint32_t copy = 0U; result == 0 && copy < 2U; ++copy)
        for (uint32_t sector = 0U; sector < layout.fat_sectors; ++sector) {
            const uint8_t *replacement = fat12_repair_fat +
                sector * X86OS_STORAGE_BLOCK_SIZE;
            const uint8_t *old = fat12_copies[copy] +
                sector * X86OS_STORAGE_BLOCK_SIZE;
            if (!format_equal(replacement, old, X86OS_STORAGE_BLOCK_SIZE))
                ++changed_fat_sectors;
        }
    for (uint32_t cluster = 2U; result == 0 &&
         cluster <= layout.cluster_count + 1U; ++cluster)
        if (fat12_regular_references[cluster] != 0U &&
            fat12_entry(fat12_repair_fat, cluster) !=
                fat12_entry(fat12_copies[0], cluster)) result = -84;
    if (result == 0 && (fat12_salvage_sector_count == 0U ||
        fat12_salvage_sector_count + changed_fat_sectors >
            FORMAT_FAT12_JOURNAL_ENTRIES)) result = -28;

    format_journal_header_t journal;
    if (result == 0 &&
        fat12_load_clean_journal(resource, &layout, &journal) != 0)
        result = -84;
    if (result == 0) {
        fat12_prepare_journal_header(&journal, layout.volume_id,
            journal.sequence + 1U, FAT12_JOURNAL_ACTIVE, 0U);
        result = fat12_write_journal_header(resource, &journal);
    }

    uint8_t old_sector[X86OS_STORAGE_BLOCK_SIZE];
    for (uint32_t index = 0U; result == 0 &&
         index < fat12_salvage_sector_count; ++index) {
        fat12_salvage_sector_t *planned = &fat12_salvage_sectors[index];
        if (x86os_storage_block_read(resource, planned->sector,
                                     old_sector) != 0 ||
            format_crc32(old_sector, sizeof(old_sector)) !=
                planned->original_crc32) result = -84;
        if (result == 0)
            result = fat12_record_old_sector(resource, &journal,
                                             planned->sector, old_sector);
        if (result == 0 &&
            x86os_storage_maintenance_renew(resource, token, 0U) != 0)
            result = -30;
    }
    for (uint32_t copy = 0U; result == 0 && copy < 2U; ++copy)
        for (uint32_t sector = 0U; result == 0 &&
             sector < layout.fat_sectors; ++sector) {
            const uint8_t *replacement = fat12_repair_fat +
                sector * X86OS_STORAGE_BLOCK_SIZE;
            const uint8_t *old = fat12_copies[copy] +
                sector * X86OS_STORAGE_BLOCK_SIZE;
            if (format_equal(replacement, old, X86OS_STORAGE_BLOCK_SIZE))
                continue;
            uint32_t target = layout.reserved_sectors +
                copy * layout.fat_sectors + sector;
            result = fat12_record_old_mirror(resource, &journal, copy,
                                             sector, target);
            if (result == 0 &&
                x86os_storage_maintenance_renew(resource, token, 0U) != 0)
                result = -30;
        }
    if (result == 0 && x86os_storage_block_flush(resource) != 0) result = -5;

    for (uint32_t index = 0U; result == 0 &&
         index < fat12_salvage_sector_count; ++index) {
        const fat12_salvage_sector_t *planned = &fat12_salvage_sectors[index];
        if (planned->new_directory_data == 0U) continue;
        result = format_write(resource, planned->sector, planned->data);
        if (result == 0 &&
            x86os_storage_maintenance_renew(resource, token, 0U) != 0)
            result = -30;
    }
    if (result == 0 && x86os_storage_block_flush(resource) != 0) result = -5;
    for (uint32_t copy = 0U; result == 0 && copy < 2U; ++copy)
        for (uint32_t sector = 0U; result == 0 &&
             sector < layout.fat_sectors; ++sector) {
            const uint8_t *replacement = fat12_repair_fat +
                sector * X86OS_STORAGE_BLOCK_SIZE;
            const uint8_t *old = fat12_copies[copy] +
                sector * X86OS_STORAGE_BLOCK_SIZE;
            if (format_equal(replacement, old, X86OS_STORAGE_BLOCK_SIZE))
                continue;
            uint32_t target = layout.reserved_sectors +
                copy * layout.fat_sectors + sector;
            result = format_write(resource, target, replacement);
            if (result == 0 &&
                x86os_storage_maintenance_renew(resource, token, 0U) != 0)
                result = -30;
        }
    if (result == 0 && x86os_storage_block_flush(resource) != 0) result = -5;
    for (uint32_t index = 0U; result == 0 &&
         index < fat12_salvage_sector_count; ++index) {
        const fat12_salvage_sector_t *planned = &fat12_salvage_sectors[index];
        if (planned->new_directory_data != 0U) continue;
        result = format_write(resource, planned->sector, planned->data);
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
    return result == 0 ? diagnosis |
        (int)X86OS_FAT12_RESULT_ORPHANS_SALVAGED : result;
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

static void fat12_prepare_remap_header(format_remap_header_t *header,
        uint32_t volume_id, uint64_t sequence, uint32_t entry_count,
        const format_remap_entry_t *entries) {
    *header = (format_remap_header_t){
        .magic = FAT12_REMAP_MAGIC,
        .version = FAT12_REMAP_VERSION,
        .entry_size = sizeof(format_remap_entry_t),
        .media_fingerprint = volume_id,
        .sequence = sequence,
        .entry_count = entry_count,
        .crc32 = 0U,
    };
    header->crc32 = fat12_remap_crc(header, entries);
}

static int fat12_write_remap_header(uint32_t resource, uint32_t sector,
        const format_remap_header_t *header) {
    uint8_t data[X86OS_STORAGE_BLOCK_SIZE];
    format_fill(data, 0U, sizeof(data));
    fat12_copy_bytes(data, header, sizeof(*header));
    return format_write(resource, sector, data);
}

static int fat12_record_bad_sector(uint32_t resource, uint32_t bad_sector) {
    fat12_check_layout_t layout;
    int diagnosis = fat12_check_volume(resource, &layout);
    if (diagnosis != 0 || !layout.reist_layout ||
        bad_sector < layout.reserved_sectors ||
        bad_sector >= layout.data_start) return -22;

    uint32_t token = 0U;
    if (x86os_storage_maintenance_acquire(resource, 0U, &token) != 0 ||
        token == 0U) return -30;
    int result = fat12_check_volume(resource, &layout);
    fat12_remap_state_t remap;
    if (result == 0) result = fat12_load_remap(resource, &layout, &remap);
    for (uint32_t index = 0U;
         result == 0 && index < remap.header.entry_count; ++index)
        if (remap.entries[index].bad_sector == bad_sector) result = -17;
    if (result == 0 &&
        (remap.header.entry_count >= FORMAT_FAT12_REMAP_SPARES ||
         remap.header.sequence == UINT64_MAX)) result = -28;

    uint32_t replacement = 0U;
    uint32_t spare_start = FORMAT_FAT12_REMAP_BASE + 3U;
    for (uint32_t candidate = spare_start;
         result == 0 && candidate < spare_start + FORMAT_FAT12_REMAP_SPARES;
         ++candidate) {
        int used = 0;
        for (uint32_t index = 0U; index < remap.header.entry_count; ++index)
            if (remap.entries[index].replacement_sector == candidate) used = 1;
        if (!used) { replacement = candidate; break; }
    }
    if (result == 0 && replacement == 0U) result = -28;

    uint8_t recovered[X86OS_STORAGE_BLOCK_SIZE];
    uint8_t confirmation[X86OS_STORAGE_BLOCK_SIZE];
    if (result == 0 &&
        (x86os_storage_block_read(resource, bad_sector, recovered) != 0 ||
         x86os_storage_block_read(resource, bad_sector, confirmation) != 0 ||
         !format_equal(recovered, confirmation, sizeof(recovered))))
        result = -5;
    if (result == 0) result = format_write(resource, replacement, recovered);
    if (result == 0 && x86os_storage_block_flush(resource) != 0) result = -5;
    if (result == 0 &&
        x86os_storage_maintenance_renew(resource, token, 0U) != 0)
        result = -30;

    uint32_t entry_index = remap.header.entry_count;
    if (result == 0) {
        remap.entries[entry_index] = (format_remap_entry_t){
            .bad_sector = bad_sector,
            .replacement_sector = replacement,
            .sequence = remap.header.sequence + 1U,
        };
        uint8_t table[X86OS_STORAGE_BLOCK_SIZE];
        format_fill(table, 0U, sizeof(table));
        fat12_copy_bytes(table, remap.entries,
            (entry_index + 1U) * sizeof(remap.entries[0]));
        result = format_write(resource, FORMAT_FAT12_REMAP_BASE + 2U, table);
    }
    if (result == 0 && x86os_storage_block_flush(resource) != 0) result = -5;
    if (result == 0 &&
        x86os_storage_maintenance_renew(resource, token, 0U) != 0)
        result = -30;
    if (result == 0) {
        fat12_prepare_remap_header(&remap.header, layout.volume_id,
            remap.header.sequence + 1U, entry_index + 1U, remap.entries);
        result = fat12_write_remap_header(resource,
            FORMAT_FAT12_REMAP_BASE, &remap.header);
        if (result == 0)
            result = fat12_write_remap_header(resource,
                FORMAT_FAT12_REMAP_BASE + 1U, &remap.header);
    }
    if (result == 0 && x86os_storage_block_flush(resource) != 0) result = -5;
    if (result == 0 &&
        x86os_storage_maintenance_renew(resource, token, 0U) != 0)
        result = -30;
    if (result == 0) {
        fat12_remap_state_t verified;
        result = fat12_load_remap(resource, &layout, &verified);
        if (result == 0 &&
            (verified.header.sequence != remap.header.sequence ||
             verified.header.entry_count != entry_index + 1U ||
             verified.entries[entry_index].bad_sector != bad_sector ||
             verified.entries[entry_index].replacement_sector != replacement))
            result = -84;
    }
    if (x86os_storage_maintenance_release(resource, token) != 0 && result == 0)
        result = -5;
    return result == 0
        ? (int)X86OS_FAT12_RESULT_BAD_SECTOR_REMAPPED : result;
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

_Static_assert(sizeof(x86os_vfs_shadow_frame_t) == X86OS_STORAGE_BLOCK_SIZE,
               "VFS shadow frame must fill one storage payload");

static int vfs_shadow_stat(x86os_vfs_shadow_frame_t *frame) {
    if (frame == 0 || frame->version != X86OS_VFS_SHADOW_FRAME_VERSION ||
        frame->struct_size != sizeof(*frame) ||
        frame->operation != X86OS_VFS_SHADOW_STAT || frame->flags != 0U ||
        frame->path_length == 0U ||
        frame->path_length >= X86OS_VFS_SHADOW_PATH_CAPACITY ||
        frame->path[0] != '/' || frame->path[frame->path_length] != '\0')
        return -22;
    for (uint32_t index = 0U; index < frame->path_length; ++index)
        if (frame->path[index] == '\0') return -22;
    for (uint32_t index = 0U; index < 5U; ++index)
        if (frame->reserved[index] != 0U) return -22;

    x86os_file_info_t info;
    uint8_t *info_bytes = (uint8_t *)&info;
    for (uint32_t index = 0U; index < sizeof(info); ++index)
        info_bytes[index] = 0U;
    int status = (int)x86os_syscall(
        X86OS_SYS_STAT, (uintptr_t)frame->path, (uintptr_t)&info, 0U);
    frame->result = status;
    for (uint32_t index = 0U; index < sizeof(frame->info.name); ++index)
        frame->info.name[index] = status == 0 ? info.name[index] : '\0';
    frame->info.type = status == 0 ? info.type : 0U;
    frame->info.size = status == 0 ? info.size : 0U;
    frame->info.create_time = status == 0 ? info.create_time : 0U;
    frame->info.modify_time = status == 0 ? info.modify_time : 0U;
    frame->info.access_time = status == 0 ? info.access_time : 0U;
    return 0;
}

int main(void) {
    int bind = x86os_storage_bind();
    if (bind != 0) {
        x86os_puts("STORAGE SERVICE_BIND_FAILED code=");
        x86os_print_number(bind);
        x86os_puts("\nSTORAGE is an internal service. Use svcctl list/status.\n");
        return 1;
    }
    uint32_t boot_ack_deadline =
        x86os_uptime_ms() + BOOT_STATUS_ACK_TIMEOUT_MS;
    uint8_t boot_ack_active = 1U;
    for (;;) {
        boot_success_ack_poll(boot_ack_deadline, &boot_ack_active);
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
        if (request.operation == X86OS_STORAGE_VFS_SHADOW_STAT &&
            request.length == X86OS_STORAGE_BLOCK_SIZE) {
            x86os_vfs_shadow_frame_t frame;
            uint8_t *frame_bytes = (uint8_t *)&frame;
            for (uint32_t index = 0U; index < sizeof(frame); ++index)
                frame_bytes[index] = data[index];
            result = vfs_shadow_stat(&frame);
            if (result == 0)
                for (uint32_t index = 0U; index < sizeof(frame); ++index)
                    data[index] = frame_bytes[index];
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
        if (request.operation == X86OS_STORAGE_SALVAGE_FAT12_ORPHANS &&
            request.length == 0U)
            result = fat12_salvage_orphans(request.resource);
        if (request.operation == X86OS_STORAGE_REPAIR_FAT12_LOOPS &&
            request.length == 0U)
            result = fat12_repair_loops(request.resource);
        if (request.operation == X86OS_STORAGE_REPAIR_FAT12_DIRECTORY_LOOPS &&
            request.length == 0U)
            result = fat12_repair_directory_loops(request.resource);
        if (request.operation == X86OS_STORAGE_REPAIR_FAT12_SHORT_LOOPS &&
            request.length == 0U)
            result = fat12_repair_short_loops(request.resource);
        if (request.operation == X86OS_STORAGE_REPAIR_FAT12_CROSSLINKS &&
            request.length == 0U)
            result = fat12_repair_crosslinks(request.resource);
        if (request.operation == X86OS_STORAGE_REPAIR_FAT12_DIRECTORY_SIZE &&
            request.length == 0U)
            result = fat12_repair_directory_sizes(request.resource);
        if (request.operation == X86OS_STORAGE_REPAIR_FAT12_VOLUME_LABEL &&
            request.length == 0U)
            result = fat12_repair_volume_labels(request.resource);
        if (request.operation == X86OS_STORAGE_REPAIR_FAT12_ZERO_FILES &&
            request.length == 0U)
            result = fat12_repair_zero_files(request.resource);
        if (request.operation ==
                X86OS_STORAGE_REPAIR_FAT12_ZERO_START_FILES &&
            request.length == 0U)
            result = fat12_repair_zero_start_files(request.resource);
        if (request.operation == X86OS_STORAGE_REPAIR_FAT12_DOT_SIZE &&
            request.length == 0U)
            result = fat12_repair_dot_sizes(request.resource);
        if (request.operation == X86OS_STORAGE_REPAIR_FAT12_DOT_CLUSTER &&
            request.length == 0U)
            result = fat12_repair_dot_clusters(request.resource);
        if (request.operation ==
                X86OS_STORAGE_REPAIR_FAT12_REQUIRED_CROSSLINKS &&
            request.length == 0U)
            result = fat12_repair_required_crosslinks(request.resource);
        if (request.operation ==
                X86OS_STORAGE_REPAIR_FAT12_DIRECTORY_CROSSLINKS &&
            request.length == 0U)
            result = fat12_repair_empty_directory_crosslinks(request.resource);
        if (request.operation ==
                X86OS_STORAGE_REPAIR_FAT12_DIRECTORY_TOPOLOGY &&
            request.length == 0U)
            result = fat12_repair_directory_topology(request.resource);
        if (request.operation == X86OS_STORAGE_RECORD_FAT12_BAD_SECTOR &&
            request.length == 0U)
            result = fat12_record_bad_sector(request.resource,
                                             request.offset);
        if (x86os_storage_complete(request.handle, result, data) != 0)
            return 3;
    }
}
