#include <stdio.h>
#include <string.h>

#include "fs/fat12/fat12_journal.h"

static unsigned char disk[64][FAT12_JOURNAL_SECTOR_SIZE];
static int corrupt_readback;

static bool read_sector(void *context, uint32_t sector, void *buffer) {
    (void)context;
    if (sector >= 64U) return false;
    memcpy(buffer, disk[sector], FAT12_JOURNAL_SECTOR_SIZE);
    if (corrupt_readback && sector == 40U)
        ((unsigned char *)buffer)[0] ^= 1U;
    return true;
}

static bool write_sector(void *context, uint32_t sector, const void *buffer) {
    (void)context;
    if (sector >= 64U) return false;
    memcpy(disk[sector], buffer, FAT12_JOURNAL_SECTOR_SIZE);
    return true;
}

int main(void) {
    fat12_journal_t journal;
    unsigned char old[FAT12_JOURNAL_SECTOR_SIZE];
    unsigned char replacement[FAT12_JOURNAL_SECTOR_SIZE];
    memset(old, 0x41, sizeof(old));
    memset(replacement, 0x42, sizeof(replacement));
    memcpy(disk[40], old, sizeof(old));
    if (!fat12_journal_format(&journal, 2U, 3U, 4U, 0x12345678U) ||
        !fat12_journal_begin(&journal, 2U, write_sector, NULL) ||
        !fat12_journal_record(&journal, 40U, old, write_sector, NULL))
        return 1;
    memcpy(disk[40], replacement, sizeof(replacement));
    if (!fat12_journal_recover(&journal, read_sector, write_sector, NULL) ||
        memcmp(disk[40], old, sizeof(old)) != 0) return 2;
    memcpy(disk[40], replacement, sizeof(replacement));
    if (!fat12_journal_begin(&journal, 3U, write_sector, NULL) ||
        !fat12_journal_record(&journal, 40U, old, write_sector, NULL))
        return 3;
    corrupt_readback = 1;
    if (fat12_journal_recover(&journal, read_sector, write_sector, NULL))
        return 4;
    return 0;
}
