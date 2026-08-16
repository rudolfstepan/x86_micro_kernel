#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "fs/fat12/fat12_remap.h"

#define DISK_SECTORS 64U

enum remap_barrier_id {
    REMAP_BARRIER_TABLE = 1,
    REMAP_BARRIER_PRIMARY_HEADER,
    REMAP_BARRIER_MIRROR_HEADER,
    REMAP_BARRIER_COUNT = REMAP_BARRIER_MIRROR_HEADER
};

typedef struct {
    uint32_t writes;
    uint32_t fail_write;
    uint32_t partial_write;
    uint32_t remove_write;
    uint32_t fail_read_sector;
    bool removed;
} fault_io_t;

static unsigned char disk[DISK_SECTORS][FAT12_REMAP_SECTOR_SIZE];
static uint32_t independent_progress;

static bool read_sector(void *context, uint32_t sector, void *buffer) {
    fault_io_t *fault = context;
    if (sector >= DISK_SECTORS || (fault != NULL && fault->removed) ||
        (fault != NULL && fault->fail_read_sector == sector)) return false;
    memcpy(buffer, disk[sector], FAT12_REMAP_SECTOR_SIZE);
    return true;
}

static bool write_sector(void *context, uint32_t sector, const void *buffer) {
    fault_io_t *fault = context;
    if (sector >= DISK_SECTORS || (fault != NULL && fault->removed))
        return false;
    if (fault != NULL) {
        ++fault->writes;
        if (fault->remove_write == fault->writes) {
            fault->removed = true;
            return false;
        }
        if (fault->fail_write == fault->writes) return false;
        if (fault->partial_write == fault->writes) {
            memcpy(disk[sector], buffer, FAT12_REMAP_SECTOR_SIZE / 2U);
            disk[sector][FAT12_REMAP_SECTOR_SIZE - 1U] ^= 0xA5U;
            return true;
        }
    }
    memcpy(disk[sector], buffer, FAT12_REMAP_SECTOR_SIZE);
    return true;
}

static bool seed_empty_table(fat12_remap_table_t *table) {
    if (!fat12_remap_format(table, 20U, 21U, 22U, 0x12345678U)) return false;
    memset(disk, 0, sizeof(disk));
    memcpy(disk[20], &table->header, sizeof(table->header));
    memcpy(disk[21], &table->header, sizeof(table->header));
    return true;
}

static int run_fault(uint32_t barrier, bool partial, bool remove) {
    fat12_remap_table_t table;
    if (!seed_empty_table(&table)) return 1;
    fault_io_t fault = {
        .fail_write = !partial && !remove ? barrier : 0U,
        .partial_write = partial ? barrier : 0U,
        .remove_write = remove ? barrier : 0U,
        .fail_read_sector = UINT32_MAX,
    };
    if (fat12_remap_add(&table, 40U, 50U, read_sector, write_sector, &fault) ||
        fault.writes < barrier) return 2;

    /* A fresh load may expose the complete old table or the complete new
     * table if the primary publication reached stable storage first. */
    fault_io_t healthy = {.fail_read_sector = UINT32_MAX};
    fat12_remap_table_t loaded;
    if (!fat12_remap_format(&loaded, 20U, 21U, 22U, 0x12345678U)) return 3;
    bool load_ok = fat12_remap_load(&loaded, read_sector, &healthy);
    if (load_ok) {
        uint32_t replacement = 0U;
        bool found = fat12_remap_lookup(&loaded, 40U, &replacement);
        if (found && replacement != 50U) return 4;
    }
    if (fault.writes > REMAP_BARRIER_COUNT) return 5;
    ++independent_progress;
    return 0;
}

static int baseline_and_corruption(void) {
    fat12_remap_table_t table;
    if (!seed_empty_table(&table)) return 1;
    fault_io_t healthy = {.fail_read_sector = UINT32_MAX};
    if (!fat12_remap_add(&table, 40U, 50U, read_sector, write_sector,
                         &healthy) ||
        fat12_remap_add(&table, 41U, 50U, read_sector, write_sector,
                        &healthy)) return 2;
    fat12_remap_table_t loaded;
    uint32_t replacement = 0U;
    if (!fat12_remap_format(&loaded, 20U, 21U, 22U, 0x12345678U) ||
        !fat12_remap_load(&loaded, read_sector, &healthy) ||
        !fat12_remap_lookup(&loaded, 40U, &replacement) || replacement != 50U)
        return 3;

    disk[20][0] ^= 1U;
    if (!fat12_remap_load(&loaded, read_sector, &healthy) ||
        !fat12_remap_lookup(&loaded, 40U, &replacement) || replacement != 50U)
        return 4;

    disk[22][0] ^= 1U;
    if (fat12_remap_load(&loaded, read_sector, &healthy)) return 5;
    disk[21][0] ^= 1U;
    if (fat12_remap_load(&loaded, read_sector, &healthy)) return 6;
    return 0;
}

int main(void) {
    if (baseline_and_corruption() != 0) return 1;
    for (uint32_t barrier = 1U; barrier <= REMAP_BARRIER_COUNT; ++barrier) {
        if (run_fault(barrier, false, false) != 0) return 10 + (int)barrier;
        if (run_fault(barrier, true, false) != 0) return 20 + (int)barrier;
        if (run_fault(barrier, false, true) != 0) return 30 + (int)barrier;
    }
    return independent_progress == REMAP_BARRIER_COUNT * 3U ? 0 : 50;
}
