/**
 * @file test/test_fat12_journal_host.c
 * @brief Hostseitiger Regressionstest für fat12 journal.
 *
 * Layer: Host test harness.
 * Contract: Prüft beobachtbares Verhalten und feste Fehlergrenzen ohne Zielhardware.
 * Safety: Testdoubles dürfen Produktionsverträge nicht abschwächen oder Erfolg vortäuschen.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "fs/fat12/fat12_journal.h"

#define DISK_SECTORS 192U

enum journal_barrier_id {
    JOURNAL_BARRIER_BEGIN_PRIMARY = 1,
    JOURNAL_BARRIER_BEGIN_MIRROR,
    JOURNAL_BARRIER_UNDO_DATA,
    JOURNAL_BARRIER_UNDO_METADATA,
    JOURNAL_BARRIER_ACTIVE_PRIMARY,
    JOURNAL_BARRIER_ACTIVE_MIRROR,
    JOURNAL_BARRIER_CLEAN_PRIMARY,
    JOURNAL_BARRIER_CLEAN_MIRROR,
    JOURNAL_BARRIER_COUNT = JOURNAL_BARRIER_CLEAN_MIRROR
};

typedef struct {
    uint32_t writes;
    uint32_t fail_write;
    uint32_t partial_write;
    uint32_t remove_write;
    uint32_t fail_read_sector;
    uint32_t corrupt_read_sector;
    bool removed;
} fault_io_t;

static unsigned char disk[DISK_SECTORS][FAT12_JOURNAL_SECTOR_SIZE];
static uint32_t independent_progress;

static bool read_sector(void *context, uint32_t sector, void *buffer) {
    fault_io_t *fault = context;
    if (sector >= DISK_SECTORS || (fault != NULL && fault->removed) ||
        (fault != NULL && fault->fail_read_sector == sector)) return false;
    memcpy(buffer, disk[sector], FAT12_JOURNAL_SECTOR_SIZE);
    if (fault != NULL && fault->corrupt_read_sector == sector)
        ((unsigned char *)buffer)[0] ^= 1U;
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
            memcpy(disk[sector], buffer, FAT12_JOURNAL_SECTOR_SIZE / 2U);
            disk[sector][FAT12_JOURNAL_SECTOR_SIZE - 1U] ^= 0xA5U;
            return true;
        }
    }
    memcpy(disk[sector], buffer, FAT12_JOURNAL_SECTOR_SIZE);
    return true;
}

static bool sector_is(const unsigned char *expected, uint32_t sector) {
    return memcmp(disk[sector], expected, FAT12_JOURNAL_SECTOR_SIZE) == 0;
}

static int run_fault(uint32_t barrier, bool partial, bool remove) {
    fat12_journal_t journal;
    unsigned char old[FAT12_JOURNAL_SECTOR_SIZE];
    unsigned char replacement[FAT12_JOURNAL_SECTOR_SIZE];
    memset(disk, 0, sizeof(disk));
    memset(old, 0x41, sizeof(old));
    memset(replacement, 0x42, sizeof(replacement));
    memcpy(disk[160], old, sizeof(old));
    fault_io_t fault = {
        .fail_write = !partial && !remove ? barrier : 0U,
        .partial_write = partial ? barrier : 0U,
        .remove_write = remove ? barrier : 0U,
        .fail_read_sector = UINT32_MAX,
        .corrupt_read_sector = UINT32_MAX,
    };
    bool result = fat12_journal_format(&journal, 2U, 3U, 4U, 0x12345678U) &&
        fat12_journal_begin(&journal, 2U, read_sector, write_sector, &fault) &&
        fat12_journal_record(&journal, 160U, old, read_sector,
                             write_sector, &fault);
    if (result) {
        memcpy(disk[160], replacement, sizeof(replacement));
        result = fat12_journal_commit(&journal, read_sector, write_sector,
                                      &fault);
    }
    if (result || fault.writes < barrier ||
        (!sector_is(old, 160U) && !sector_is(replacement, 160U))) return 1;

    /* A fresh mount either recovers a complete old state or refuses the
     * ambiguous medium.  It must never expose mixed bytes. */
    fault_io_t healthy = {
        .fail_read_sector = UINT32_MAX,
        .corrupt_read_sector = UINT32_MAX,
    };
    fat12_journal_t loaded;
    if (!fat12_journal_format(&loaded, 2U, 3U, 4U, 0x12345678U)) return 2;
    bool recovered = fat12_journal_recover(&loaded, read_sector, write_sector,
                                           &healthy);
    if (recovered && !sector_is(old, 160U) && !sector_is(replacement, 160U))
        return 3;
    if (healthy.writes > 4U) return 4;
    ++independent_progress;
    return 0;
}

static int baseline_and_corruption(void) {
    fat12_journal_t journal;
    unsigned char old[FAT12_JOURNAL_SECTOR_SIZE];
    unsigned char replacement[FAT12_JOURNAL_SECTOR_SIZE];
    memset(disk, 0, sizeof(disk));
    memset(old, 0x31, sizeof(old));
    memset(replacement, 0x32, sizeof(replacement));
    memcpy(disk[160], old, sizeof(old));
    fault_io_t healthy = {
        .fail_read_sector = UINT32_MAX,
        .corrupt_read_sector = UINT32_MAX,
    };
    if (!fat12_journal_format(&journal, 2U, 3U, 4U, 0x12345678U) ||
        !fat12_journal_begin(&journal, 2U, read_sector, write_sector, &healthy) ||
        !fat12_journal_record(&journal, 160U, old, read_sector,
                              write_sector, &healthy)) return 1;
    memcpy(disk[160], replacement, sizeof(replacement));
    if (!fat12_journal_recover(&journal, read_sector, write_sector, &healthy) ||
        !sector_is(old, 160U)) return 2;

    memset(disk, 0, sizeof(disk));
    memcpy(disk[160], old, sizeof(old));
    healthy.writes = 0U;
    if (!fat12_journal_format(&journal, 2U, 3U, 4U, 0x12345678U) ||
        !fat12_journal_begin(&journal, 3U, read_sector, write_sector, &healthy) ||
        !fat12_journal_record(&journal, 160U, old, read_sector,
                              write_sector, &healthy)) return 3;
    memcpy(disk[160], replacement, sizeof(replacement));
    disk[2][0] ^= 1U;
    if (!fat12_journal_recover(&journal, read_sector, write_sector, &healthy) ||
        !sector_is(old, 160U)) return 4;

    /* Bad undo data, FAT/root-like targets and two corrupt headers all fail
     * closed.  Three target sectors make the classification explicit. */
    const uint32_t targets[] = {160U, 161U, 162U};
    for (uint32_t index = 0U; index < 3U; ++index) {
        memset(disk, 0, sizeof(disk));
        memcpy(disk[targets[index]], old, sizeof(old));
        healthy.writes = 0U;
        if (!fat12_journal_format(&journal, 2U, 3U, 4U, 0x12345678U) ||
            !fat12_journal_begin(&journal, 3U + index, read_sector,
                                 write_sector, &healthy) ||
            !fat12_journal_record(&journal, targets[index], old, read_sector,
                                  write_sector, &healthy)) return 5;
        memcpy(disk[targets[index]], replacement, sizeof(replacement));
        disk[4][0] ^= 1U;
        if (fat12_journal_recover(&journal, read_sector, write_sector,
                                  &healthy)) return 6;
    }

    /* The target classes model a data sector, a FAT sector and a root
     * directory sector.  A failed post-recovery readback must fence each one. */
    for (uint32_t index = 0U; index < 3U; ++index) {
        memset(disk, 0, sizeof(disk));
        memcpy(disk[targets[index]], old, sizeof(old));
        healthy.writes = 0U;
        if (!fat12_journal_format(&journal, 2U, 3U, 4U, 0x12345678U) ||
            !fat12_journal_begin(&journal, 7U + index, read_sector,
                                 write_sector, &healthy) ||
            !fat12_journal_record(&journal, targets[index], old, read_sector,
                                  write_sector, &healthy)) return 7;
        memcpy(disk[targets[index]], replacement, sizeof(replacement));
        fault_io_t bad_target = {
            .fail_read_sector = targets[index],
            .corrupt_read_sector = UINT32_MAX,
        };
        if (fat12_journal_recover(&journal, read_sector, write_sector,
                                  &bad_target)) return 8;
    }

    memset(disk, 0, sizeof(disk));
    healthy.writes = 0U;
    if (!fat12_journal_format(&journal, 2U, 3U, 4U, 0x12345678U) ||
        !fat12_journal_begin(&journal, 9U, read_sector, write_sector, &healthy))
        return 9;
    disk[2][0] ^= 1U;
    disk[3][0] ^= 1U;
    if (fat12_journal_load(&journal, read_sector, &healthy)) return 10;
    return 0;
}

int main(void) {
    if (baseline_and_corruption() != 0) return 1;
    for (uint32_t barrier = 1U; barrier <= JOURNAL_BARRIER_COUNT; ++barrier) {
        if (run_fault(barrier, false, false) != 0) return 10 + (int)barrier;
        if (run_fault(barrier, true, false) != 0) return 20 + (int)barrier;
        if (run_fault(barrier, false, true) != 0) return 30 + (int)barrier;
    }
    return independent_progress == JOURNAL_BARRIER_COUNT * 3U ? 0 : 50;
}
