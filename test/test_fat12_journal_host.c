#include <stdio.h>
#include <string.h>

#include "fs/fat12/fat12_journal.h"

static unsigned char disk[192][FAT12_JOURNAL_SECTOR_SIZE];
static int corrupt_readback;
static uint32_t corrupt_sector = UINT32_MAX;

static bool read_sector(void *context, uint32_t sector, void *buffer) {
    (void)context;
    if (sector >= 192U) return false;
    memcpy(buffer, disk[sector], FAT12_JOURNAL_SECTOR_SIZE);
    if (sector == corrupt_sector) ((unsigned char *)buffer)[0] ^= 1U;
    if (corrupt_readback && sector == 160U)
        ((unsigned char *)buffer)[0] ^= 1U;
    return true;
}

static bool write_sector(void *context, uint32_t sector, const void *buffer) {
    (void)context;
    if (sector >= 192U) return false;
    memcpy(disk[sector], buffer, FAT12_JOURNAL_SECTOR_SIZE);
    return true;
}

int main(void) {
    fat12_journal_t journal;
    unsigned char old[FAT12_JOURNAL_SECTOR_SIZE];
    unsigned char replacement[FAT12_JOURNAL_SECTOR_SIZE];
    memset(old, 0x41, sizeof(old));
    memset(replacement, 0x42, sizeof(replacement));
    memcpy(disk[160], old, sizeof(old));
    if (!fat12_journal_format(&journal, 2U, 3U, 4U, 0x12345678U) ||
        !fat12_journal_begin(&journal, 2U, read_sector, write_sector, NULL) ||
        !fat12_journal_record(&journal, 160U, old, read_sector,
                              write_sector, NULL))
        return 1;
    memcpy(disk[160], replacement, sizeof(replacement));
    if (!fat12_journal_recover(&journal, read_sector, write_sector, NULL) ||
        memcmp(disk[160], old, sizeof(old)) != 0) return 2;
    memcpy(disk[160], replacement, sizeof(replacement));
    if (!fat12_journal_begin(&journal, 3U, read_sector, write_sector, NULL) ||
        !fat12_journal_record(&journal, 160U, old, read_sector,
                              write_sector, NULL))
        return 3;
    corrupt_readback = 1;
    if (fat12_journal_recover(&journal, read_sector, write_sector, NULL))
        return 4;
    corrupt_readback = 0;
    if (!fat12_journal_format(&journal, 2U, 3U, 4U, 0x12345678U)) return 5;
    if (fat12_journal_begin(&journal, 1U, read_sector, write_sector, NULL))
        return 6;
    corrupt_sector = 2U;
    if (fat12_journal_begin(&journal, 4U, read_sector, write_sector, NULL))
        return 7;
    corrupt_sector = UINT32_MAX;
    if (!fat12_journal_format(&journal, 2U, 3U, 4U, 0x12345678U) ||
        !fat12_journal_begin(&journal, 5U, read_sector, write_sector, NULL))
        return 8;
    fat12_journal_header_t mirror;
    memcpy(&mirror, disk[3], sizeof(mirror));
    mirror.state = FAT12_JOURNAL_CLEAN;
    mirror.crc32 = 0U;
    mirror.crc32 = fat12_journal_crc32(&mirror, sizeof(mirror));
    memcpy(disk[3], &mirror, sizeof(mirror));
    if (fat12_journal_load(&journal, read_sector, NULL)) return 9;
    return 0;
}
