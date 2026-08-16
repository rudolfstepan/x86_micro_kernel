#include <stdio.h>
#include <string.h>

#include "fs/fat12/fat12_remap.h"

static unsigned char disk[64][512];

static bool read_sector(void *context, uint32_t sector, void *buffer) {
    (void)context;
    if (sector >= 64U) return false;
    memcpy(buffer, disk[sector], 512U);
    return true;
}

static bool write_sector(void *context, uint32_t sector, const void *buffer) {
    (void)context;
    if (sector >= 64U) return false;
    memcpy(disk[sector], buffer, 512U);
    return true;
}

int main(void) {
    fat12_remap_table_t table;
    uint32_t replacement = 0U;
    if (!fat12_remap_format(&table, 20U, 21U, 22U, 0x12345678U) ||
        !fat12_remap_add(&table, 40U, 50U, read_sector,
                         write_sector, NULL) ||
        fat12_remap_add(&table, 41U, 50U, read_sector,
                        write_sector, NULL)) return 1;
    fat12_remap_table_t loaded;
    if (!fat12_remap_format(&loaded, 20U, 21U, 22U, 0x12345678U) ||
        !fat12_remap_load(&loaded, read_sector, NULL) ||
        !fat12_remap_lookup(&loaded, 40U, &replacement) || replacement != 50U)
        return 2;
    disk[22][0] ^= 1U;
    if (fat12_remap_load(&loaded, read_sector, NULL)) return 3;
    return 0;
}
