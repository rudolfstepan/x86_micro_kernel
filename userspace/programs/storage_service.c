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
        if (x86os_storage_complete(request.handle, result, data) != 0)
            return 3;
    }
}
