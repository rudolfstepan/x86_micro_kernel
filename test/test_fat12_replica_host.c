/**
 * @file test/test_fat12_replica_host.c
 * @brief Hostseitiger Regressionstest für fat12 replica.
 *
 * Layer: Host test harness.
 * Contract: Prüft beobachtbares Verhalten und feste Fehlergrenzen ohne Zielhardware.
 * Safety: Testdoubles dürfen Produktionsverträge nicht abschwächen oder Erfolg vortäuschen.
 */
#include <string.h>

#include "fs/fat12/fat12_replica.h"

enum replica_barrier_id {
    REPLICA_BARRIER_PRIMARY_DATA_0 = 1,
    REPLICA_BARRIER_PRIMARY_DATA_1,
    REPLICA_BARRIER_PRIMARY_DATA_2,
    REPLICA_BARRIER_PRIMARY_DATA_3,
    REPLICA_BARRIER_PRIMARY_DATA_4,
    REPLICA_BARRIER_PRIMARY_DATA_5,
    REPLICA_BARRIER_PRIMARY_DATA_6,
    REPLICA_BARRIER_PRIMARY_DATA_7,
    REPLICA_BARRIER_PRIMARY_HEADER,
    REPLICA_BARRIER_MIRROR_DATA_0,
    REPLICA_BARRIER_MIRROR_DATA_1,
    REPLICA_BARRIER_MIRROR_DATA_2,
    REPLICA_BARRIER_MIRROR_DATA_3,
    REPLICA_BARRIER_MIRROR_DATA_4,
    REPLICA_BARRIER_MIRROR_DATA_5,
    REPLICA_BARRIER_MIRROR_DATA_6,
    REPLICA_BARRIER_MIRROR_DATA_7,
    REPLICA_BARRIER_MIRROR_HEADER,
    REPLICA_BARRIER_COUNT = REPLICA_BARRIER_MIRROR_HEADER
};

typedef struct {
    uint32_t writes;
    uint32_t fail_write;
    uint32_t partial_write;
    uint32_t remove_write;
    uint32_t fail_read_sector;
    uint32_t fail_read_sector_2;
    bool removed;
} fault_io_t;

static unsigned char disk[32][FAT12_REPLICA_SECTOR_SIZE];
static const unsigned char old_data[FAT12_REPLICA_MAX_BYTES] = {0x11U, 0x22U};
static const unsigned char new_data[FAT12_REPLICA_MAX_BYTES] = {0xA1U, 0xB2U};
static uint32_t independent_progress;

static bool read_sector(void *context, uint32_t sector, void *buffer) {
    fault_io_t *fault = context;
    if (sector >= 32U || (fault != NULL &&
        (fault->removed || fault->fail_read_sector == sector ||
         fault->fail_read_sector_2 == sector))) return false;
    memcpy(buffer, disk[sector], FAT12_REPLICA_SECTOR_SIZE);
    return true;
}

static bool write_sector(void *context, uint32_t sector, const void *buffer) {
    fault_io_t *fault = context;
    if (sector >= 32U || fault == NULL || fault->removed) return false;
    ++fault->writes;
    if (fault->remove_write == fault->writes) {
        fault->removed = true;
        return false;
    }
    if (fault->fail_write == fault->writes) return false;
    if (fault->partial_write == fault->writes) {
        memcpy(disk[sector], buffer, FAT12_REPLICA_SECTOR_SIZE / 2U);
        disk[sector][FAT12_REPLICA_SECTOR_SIZE - 1U] ^= 0xA5U;
        return true;
    }
    memcpy(disk[sector], buffer, FAT12_REPLICA_SECTOR_SIZE);
    return true;
}

static bool init_replica(fat12_replica_t *replica) {
    return fat12_replica_init(replica, 2U, 11U, 0x12345678U);
}

static bool seed_old(void) {
    fat12_replica_t replica;
    fault_io_t healthy = {0};
    memset(disk, 0, sizeof(disk));
    return init_replica(&replica) &&
           fat12_replica_publish_persistent(&replica, old_data,
               sizeof(old_data), 1U, read_sector, write_sector, &healthy);
}

static int run_fault(uint32_t barrier, bool partial, bool remove) {
    if (!seed_old()) return 1;
    fat12_replica_t replica;
    fault_io_t fault = {
        .fail_write = !partial && !remove ? barrier : 0U,
        .partial_write = partial ? barrier : 0U,
        .remove_write = remove ? barrier : 0U,
    };
    if (!init_replica(&replica) ||
        fat12_replica_publish_persistent(&replica, new_data,
            sizeof(new_data), 2U, read_sector, write_sector, &fault) ||
        fault.writes < barrier || fault.writes > REPLICA_BARRIER_COUNT)
        return 2;

    fat12_replica_t loaded;
    fault_io_t healthy = {0};
    unsigned char output[FAT12_REPLICA_MAX_BYTES] = {0};
    size_t length = 0U;
    if (init_replica(&loaded) && fat12_replica_load(&loaded, read_sector,
            &healthy) && fat12_replica_select(&loaded, output,
            sizeof(output), &length)) {
        if (length != sizeof(old_data) ||
            (memcmp(output, old_data, length) != 0 &&
             memcmp(output, new_data, length) != 0)) return 3;
    }
    ++independent_progress;
    return 0;
}

static int baseline_and_corruption(void) {
    if (!seed_old()) return 1;
    fat12_replica_t loaded;
    fault_io_t healthy = {0};
    unsigned char output[FAT12_REPLICA_MAX_BYTES] = {0};
    size_t length = 0U;
    if (!init_replica(&loaded) || !fat12_replica_load(&loaded, read_sector,
            &healthy) || !fat12_replica_select(&loaded, output,
            sizeof(output), &length) || length != sizeof(old_data) ||
        memcmp(output, old_data, length) != 0) return 2;

    disk[2][0] ^= 1U;
    if (!fat12_replica_load(&loaded, read_sector, &healthy) ||
        !fat12_replica_select(&loaded, output, sizeof(output), &length) ||
        memcmp(output, old_data, length) != 0) return 3;

    disk[3][0] ^= 1U;
    if (!fat12_replica_load(&loaded, read_sector, &healthy) ||
        !fat12_replica_select(&loaded, output, sizeof(output), &length) ||
        memcmp(output, old_data, length) != 0) return 4;

    disk[11][0] ^= 1U;
    if (fat12_replica_load(&loaded, read_sector, &healthy)) return 5;

    if (!seed_old()) return 6;
    fault_io_t bad_data = {.fail_read_sector = 3U};
    if (!fat12_replica_load(&loaded, read_sector, &bad_data) ||
        !fat12_replica_select(&loaded, output, sizeof(output), &length))
        return 7;
    bad_data.fail_read_sector_2 = 12U;
    if (fat12_replica_load(&loaded, read_sector, &bad_data)) return 8;
    return 0;
}

int main(void) {
    int baseline = baseline_and_corruption();
    if (baseline != 0) return baseline;
    for (uint32_t barrier = 1U; barrier <= REPLICA_BARRIER_COUNT; ++barrier) {
        if (run_fault(barrier, false, false) != 0) return 10 + (int)barrier;
        if (run_fault(barrier, true, false) != 0) return 40 + (int)barrier;
        if (run_fault(barrier, false, true) != 0) return 70 + (int)barrier;
    }
    return independent_progress == REPLICA_BARRIER_COUNT * 3U ? 0 : 100;
}
