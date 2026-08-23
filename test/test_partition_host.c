/**
 * @file test/test_partition_host.c
 * @brief Hostseitiger Regressionstest für partition.
 *
 * Layer: Host test harness.
 * Contract: Prüft beobachtbares Verhalten und feste Fehlergrenzen ohne Zielhardware.
 * Safety: Testdoubles dürfen Produktionsverträge nicht abschwächen oder Erfolg vortäuschen.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "drivers/block/ata.h"
#include "drivers/block/block_device.h"
#include "drivers/block/partition.h"

#define TEST_SECTORS 256U

short drive_count;
drive_t *current_drive;
drive_t detected_drives[MAX_DRIVES];
static uint8_t disk[TEST_SECTORS][512];

static void put32(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (i * 8U));
}

static void put64(uint8_t *p, uint64_t value) {
    put32(p, (uint32_t)value);
    put32(p + 4, (uint32_t)(value >> 32U));
}

static uint32_t crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return crc ^ 0xFFFFFFFFU;
}

int block_device_read_sector(const drive_t *drive, uint32_t sector,
                             void *buffer) {
    (void)drive;
    if (sector >= TEST_SECTORS || buffer == NULL) return BLOCK_DEVICE_RANGE;
    memcpy(buffer, disk[sector], 512U);
    return BLOCK_DEVICE_OK;
}

int block_device_write_sector(const drive_t *drive, uint32_t sector,
                              const void *buffer) {
    (void)drive;
    if (sector >= TEST_SECTORS || buffer == NULL) return BLOCK_DEVICE_RANGE;
    memcpy(disk[sector], buffer, 512U);
    return BLOCK_DEVICE_OK;
}

int block_device_flush(const drive_t *drive) {
    return drive == NULL ? BLOCK_DEVICE_INVALID : BLOCK_DEVICE_OK;
}

static void make_gpt(void) {
    memset(disk, 0, sizeof(disk));
    memset(detected_drives, 0, sizeof(detected_drives));
    drive_count = 1;
    detected_drives[0].type = DRIVE_TYPE_ATA;
    detected_drives[0].sectors = TEST_SECTORS;
    strcpy(detected_drives[0].name, "hdd0");
    disk[0][446 + 4] = 0xEE;
    put32(disk[0] + 446 + 8, 1U);
    put32(disk[0] + 446 + 12, TEST_SECTORS - 1U);
    disk[0][510] = 0x55;
    disk[0][511] = 0xAA;

    uint8_t *entry = disk[2];
    entry[0] = 0xA2;
    entry[16] = 0x31;
    put64(entry + 32, 40U);
    put64(entry + 40, 79U);

    uint8_t *header = disk[1];
    memcpy(header, "EFI PART", 8U);
    put32(header + 8, 0x00010000U);
    put32(header + 12, 92U);
    put64(header + 24, 1U);
    put64(header + 32, TEST_SECTORS - 1U);
    put64(header + 40, 34U);
    put64(header + 48, TEST_SECTORS - 34U);
    header[56] = 0x55;
    put64(header + 72, 2U);
    put32(header + 80, 4U);
    put32(header + 84, 128U);
    put32(header + 88, crc32(disk[2], 512U));
    put32(header + 16, crc32(header, 92U));
}

int main(void) {
    make_gpt();
    assert(partition_discover() == 1U);
    assert(drive_count == 2);
    assert(detected_drives[1].partition_scheme == PARTITION_SCHEME_GPT);
    assert(detected_drives[1].lba_offset == 40U);
    assert(detected_drives[1].sectors == 40U);

    make_gpt();
    disk[1][16] ^= 1U;
    assert(partition_discover() == 0U && drive_count == 1);

    make_gpt();
    disk[2][0] ^= 1U;
    assert(partition_discover() == 0U && drive_count == 1);
    return 0;
}
