/**
 * @file kernel/init/storage_handover.c
 * @brief Synchronisiert Storage-Fences und Replikatzustand beim Handover.
 *
 * Layer: Ring-0 storage handover.
 * Contract: Snapshot und Aktivierung folgen auf beendete oder eingezäunte Writes.
 * Safety: Unklarer Persistenzzustand verhindert beschreibbare Reintegration.
 */
#include "include/kernel/storage_handover.h"

#include <stddef.h>

#include "drivers/block/ata.h"

static bool initialized;
static volatile bool writes_held;

static uint32_t crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t index = 0U; index < length; ++index) {
        crc ^= data[index];
        for (uint32_t bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1U) ^ (0xEDB88320U &
                  (uint32_t)-(int32_t)(crc & 1U));
    }
    return crc ^ 0xFFFFFFFFU;
}

static bool volume_fingerprint(uint32_t *fingerprint_out) {
    if (fingerprint_out == NULL) return false;
    drive_t *drive = ata_get_first_hdd();
    uint8_t sector[SECTOR_SIZE];
    if (drive == NULL || drive->type != DRIVE_TYPE_ATA ||
        !ata_read_sector_fresh(drive->base, 0U, sector, drive->is_master) ||
        sector[510] != 0x55U || sector[511] != 0xAAU) return false;
    uint32_t fingerprint = crc32(sector, sizeof(sector));
    if (fingerprint == 0U) return false;
    *fingerprint_out = fingerprint;
    return true;
}

bool storage_handover_init(void) {
    if (initialized) return false;
    initialized = true;
    writes_held = false;
    return true;
}

bool storage_handover_hold(void) {
    if (!initialized) return false;
    writes_held = true;
    __asm__ volatile("" ::: "memory");
    return true;
}

bool storage_handover_is_held(void) {
    return !initialized || writes_held;
}

bool storage_handover_snapshot(uint32_t source_node, uint64_t epoch,
                               uint64_t sequence,
                               handover_replica_state_t *state_out) {
    uint32_t fingerprint;
    if (!initialized || state_out == NULL || source_node == 0U ||
        epoch == 0U || sequence == 0U ||
        !volume_fingerprint(&fingerprint)) return false;
    *state_out = (handover_replica_state_t) {
        .version = HANDOVER_REPLICA_VERSION,
        .struct_size = sizeof(*state_out),
        .source_node = source_node,
        .service_id = HANDOVER_REPLICA_SERVICE_STORAGE,
        .epoch = epoch,
        .sequence = sequence,
        .value = fingerprint,
    };
    return true;
}

bool storage_handover_validate(const handover_replica_state_t *state) {
    uint32_t fingerprint;
    return initialized && state != NULL &&
        state->version == HANDOVER_REPLICA_VERSION &&
        state->struct_size == sizeof(*state) && state->source_node != 0U &&
        state->service_id == HANDOVER_REPLICA_SERVICE_STORAGE &&
        state->epoch != 0U && state->sequence != 0U &&
        state->reserved == 0U && volume_fingerprint(&fingerprint) &&
        state->value == fingerprint;
}

bool storage_handover_release(const handover_replica_state_t *state) {
    if (!writes_held || !storage_handover_validate(state)) return false;
    writes_held = false;
    __asm__ volatile("" ::: "memory");
    return true;
}
