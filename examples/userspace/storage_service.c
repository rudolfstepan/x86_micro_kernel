#include <stdint.h>

#include "x86os.h"

#define FORMAT_FAT12_SECTORS 2880U
#define FORMAT_FAT12_RESERVED 85U
#define FORMAT_FAT12_REMAP_SPARES 8U
#define FORMAT_FAT12_REPLICA_SECTORS 54U
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
    if (sector_number == 2U || sector_number == 3U) {
        format_journal_header_t header = {
            .magic = 0x524A3132U, .version = 2U,
            .header_size = sizeof(format_journal_header_t),
            .media_fingerprint = FORMAT_FAT12_VOLUME_ID, .sequence = 1U,
            .state = 0U, .entry_count = 0U, .crc32 = 0U
        };
        header.crc32 = format_crc32(&header, sizeof(header));
        for (uint32_t index = 0U; index < sizeof(header); ++index)
            sector[index] = ((const uint8_t *)&header)[index];
    } else if (sector_number == 20U || sector_number == 21U) {
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
    if (FORMAT_FAT12_RESERVED < 23U + FORMAT_FAT12_REMAP_SPARES +
                                FORMAT_FAT12_REPLICA_SECTORS)
        return -22;

    /* Write the journal/remap reservation and filesystem contents first. */
    format_fill(sector, 0U, sizeof(sector));
    for (uint32_t index = 1U; index < FORMAT_FAT12_SECTORS; ++index) {
        if (index == FORMAT_FAT12_RESERVED || index == FORMAT_FAT12_DATA_START ||
            index == 2U || index == 3U || index == 20U || index == 21U) {
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
    if (x86os_storage_bind() != 0) return 1;
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
        if (x86os_storage_complete(request.handle, result, data) != 0)
            return 3;
    }
}
