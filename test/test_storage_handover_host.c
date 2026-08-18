/**
 * @file test/test_storage_handover_host.c
 * @brief Hostseitiger Regressionstest für storage handover.
 *
 * Layer: Host test harness.
 * Contract: Prüft beobachtbares Verhalten und feste Fehlergrenzen ohne Zielhardware.
 * Safety: Testdoubles dürfen Produktionsverträge nicht abschwächen oder Erfolg vortäuschen.
 */
#include "include/kernel/storage_handover.h"

#include <string.h>

#include "drivers/block/ata.h"

static drive_t fake_drive = {
    .type = DRIVE_TYPE_ATA,
    .base = ATA_PRIMARY_IO,
    .is_master = true,
};
static uint8_t fake_sector[SECTOR_SIZE];
static bool read_succeeds = true;

drive_t *ata_get_first_hdd(void) {
    return &fake_drive;
}

bool ata_read_sector_fresh(unsigned short base, unsigned int lba, void *buffer,
                           bool is_master) {
    if (!read_succeeds || base != fake_drive.base || lba != 0U ||
        is_master != fake_drive.is_master || buffer == NULL) return false;
    memcpy(buffer, fake_sector, sizeof(fake_sector));
    return true;
}

int main(void) {
    fake_sector[0] = 0xEBU;
    fake_sector[510] = 0x55U;
    fake_sector[511] = 0xAAU;
    if (!storage_handover_init() || storage_handover_is_held()) return 1;

    handover_replica_state_t state;
    if (!storage_handover_snapshot(1U, 7U, 9U, &state) ||
        state.service_id != HANDOVER_REPLICA_SERVICE_STORAGE ||
        state.value == 0U || !storage_handover_validate(&state)) return 2;
    if (!storage_handover_hold() || !storage_handover_is_held()) return 3;
    if (storage_handover_init() || !storage_handover_is_held()) return 4;

    handover_replica_state_t invalid = state;
    invalid.value ^= 1U;
    if (storage_handover_release(&invalid) ||
        !storage_handover_is_held()) return 5;
    if (!storage_handover_release(&state) || storage_handover_is_held())
        return 6;
    if (storage_handover_release(&state)) return 7;

    if (!storage_handover_hold()) return 8;
    fake_sector[1] ^= 1U;
    if (storage_handover_validate(&state) ||
        storage_handover_release(&state) ||
        !storage_handover_is_held()) return 9;
    read_succeeds = false;
    if (storage_handover_snapshot(1U, 7U, 10U, &invalid)) return 10;
    return 0;
}
