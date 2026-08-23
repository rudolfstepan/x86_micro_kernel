/**
 * @file kernel/init/boot_health.c
 * @brief Captures and exposes the authenticated-loader boot selection.
 *
 * The kernel never mutates boot-control media. It validates and retains the
 * fixed loader handoff, then resolves its geometry to one discovered boot
 * partition before the bound Ring-3 storage service may read the status.
 */
#include <stdbool.h>
#include <stdint.h>

#include "include/kernel/boot_health.h"
#include "drivers/bus/drives.h"
#include "mm/kmalloc.h"
#include "lib/libc/stdio.h"
#include "lib/libc/string.h"

#define BOOT_HEALTH_MAGIC_0 0x53494552U /* "REIS" */
#define BOOT_HEALTH_MAGIC_1 0x31484254U /* "TBH1" */
#define BOOT_HEALTH_PARTITION_TYPE 0xDAU
#define BOOT_HEALTH_PARTITION_SECTORS 6144U
#define BOOT_HEALTH_ATTEMPT_LIMIT 2U
#define BOOT_HEALTH_INVALID_RESOURCE UINT32_MAX

typedef struct __attribute__((packed)) {
    uint32_t magic_0;
    uint32_t magic_1;
    uint32_t version;
    uint32_t struct_size;
    uint64_t sequence;
    uint32_t partition_lba;
    uint32_t partition_sectors;
    uint8_t selected_slot;
    uint8_t active_slot;
    uint8_t pending_slot;
    uint8_t attempts_remaining;
    uint8_t bios_drive;
    uint8_t attempt_limit;
    uint16_t reserved_0;
    uint32_t successful_mask;
    uint8_t reserved_1[16];
    uint32_t crc32;
} boot_health_handoff_t;

_Static_assert(sizeof(boot_health_handoff_t) == BOOT_HEALTH_HANDOFF_SIZE,
               "boot-health handoff ABI changed");
_Static_assert(sizeof(boot_health_status_t) == 40U,
               "boot-health status ABI changed");

static boot_health_handoff_t captured;
static bool captured_valid;
static bool system_ready;
static uint32_t resolved_resource = BOOT_HEALTH_INVALID_RESOURCE;

static uint32_t boot_health_crc32(const void *data, uint32_t length) {
    const uint8_t *bytes = data;
    uint32_t crc = 0xFFFFFFFFU;
    for (uint32_t index = 0U; index < length; ++index) {
        crc ^= bytes[index];
        for (uint32_t bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return crc ^ 0xFFFFFFFFU;
}

static bool bytes_zero(const uint8_t *bytes, uint32_t length) {
    for (uint32_t index = 0U; index < length; ++index)
        if (bytes[index] != 0U) return false;
    return true;
}

static bool handoff_valid(const boot_health_handoff_t *handoff) {
    if (handoff->magic_0 != BOOT_HEALTH_MAGIC_0 ||
        handoff->magic_1 != BOOT_HEALTH_MAGIC_1 ||
        handoff->version != BOOT_HEALTH_HANDOFF_VERSION ||
        handoff->struct_size != sizeof(*handoff) || handoff->sequence == 0U ||
        handoff->partition_lba == 0U ||
        handoff->partition_sectors != BOOT_HEALTH_PARTITION_SECTORS ||
        handoff->selected_slot > BOOT_HEALTH_SLOT_B ||
        handoff->active_slot > BOOT_HEALTH_SLOT_B ||
        (handoff->pending_slot != BOOT_HEALTH_SLOT_NONE &&
         handoff->pending_slot > BOOT_HEALTH_SLOT_B) ||
        handoff->attempt_limit != BOOT_HEALTH_ATTEMPT_LIMIT ||
        handoff->attempts_remaining > handoff->attempt_limit ||
        handoff->bios_drive < 0x80U || handoff->reserved_0 != 0U ||
        !bytes_zero(handoff->reserved_1, sizeof(handoff->reserved_1)) ||
        (handoff->successful_mask & ~0x03U) != 0U ||
        (handoff->successful_mask & (1U << handoff->active_slot)) == 0U)
        return false;
    if (handoff->pending_slot == BOOT_HEALTH_SLOT_NONE) {
        if (handoff->attempts_remaining != 0U) return false;
    } else if (handoff->pending_slot == handoff->active_slot ||
               handoff->selected_slot != handoff->pending_slot) {
        return false;
    }
    boot_health_handoff_t checked = *handoff;
    uint32_t expected = checked.crc32;
    checked.crc32 = 0U;
    return expected == boot_health_crc32(&checked, sizeof(checked));
}

bool boot_health_capture(void) {
    captured_valid = false;
    system_ready = false;
    resolved_resource = BOOT_HEALTH_INVALID_RESOURCE;
    memset(&captured, 0, sizeof(captured));
    if (memory_reserve_region(BOOT_HEALTH_HANDOFF_ADDRESS,
                              BOOT_HEALTH_HANDOFF_SIZE) != 0) return false;

    const volatile boot_health_handoff_t *source =
        (const volatile boot_health_handoff_t *)(uintptr_t)
            BOOT_HEALTH_HANDOFF_ADDRESS;
    uint8_t *destination = (uint8_t *)&captured;
    const volatile uint8_t *bytes = (const volatile uint8_t *)source;
    for (uint32_t index = 0U; index < sizeof(captured); ++index)
        destination[index] = bytes[index];

    if (captured.magic_0 != BOOT_HEALTH_MAGIC_0 ||
        captured.magic_1 != BOOT_HEALTH_MAGIC_1) {
        memset(&captured, 0, sizeof(captured));
        return true;
    }
    if (!handoff_valid(&captured)) {
        memset(&captured, 0, sizeof(captured));
        return false;
    }
    captured_valid = true;
    return true;
}

void boot_health_mark_system_ready(void) {
    resolved_resource = BOOT_HEALTH_INVALID_RESOURCE;
    if (captured_valid) {
        uint32_t matches = 0U;
        for (uint32_t resource = 0U; resource < (uint32_t)drive_count;
             ++resource) {
            const drive_t *drive = &detected_drives[resource];
            if (drive->type != DRIVE_TYPE_PARTITION ||
                drive->partition_scheme != PARTITION_SCHEME_MBR ||
                drive->partition_type != BOOT_HEALTH_PARTITION_TYPE ||
                drive->lba_offset != captured.partition_lba ||
                drive->sectors != captured.partition_sectors) continue;
            resolved_resource = resource;
            ++matches;
        }
        if (matches != 1U) {
            resolved_resource = BOOT_HEALTH_INVALID_RESOURCE;
            printf("REIST_BOOT HANDOFF_RESOURCE_REJECTED matches=%u\n", matches);
        }
    }
    system_ready = true;
}

int boot_health_get_status(boot_health_status_t *status) {
    if (status == NULL) return -22;
    if (!system_ready) return -11;
    if (!captured_valid) return -2;
    if (resolved_resource == BOOT_HEALTH_INVALID_RESOURCE) return -19;
    *status = (boot_health_status_t){
        .version = BOOT_HEALTH_STATUS_VERSION,
        .struct_size = sizeof(*status),
        .flags = BOOT_HEALTH_STATUS_SYSTEM_READY |
            (captured.pending_slot != BOOT_HEALTH_SLOT_NONE
                ? BOOT_HEALTH_STATUS_PENDING_TRIAL : 0U),
        .resource = resolved_resource,
        .sequence = captured.sequence,
        .partition_lba = captured.partition_lba,
        .partition_sectors = captured.partition_sectors,
        .selected_slot = captured.selected_slot,
        .active_slot = captured.active_slot,
        .pending_slot = captured.pending_slot,
        .attempts_remaining = captured.attempts_remaining,
        .reserved = 0U,
    };
    return 0;
}
