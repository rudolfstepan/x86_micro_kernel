/**
 * @file include/kernel/boot_health.h
 * @brief Validated read-only BIOS boot-health handoff.
 */
#ifndef KERNEL_BOOT_HEALTH_H
#define KERNEL_BOOT_HEALTH_H

#include <stdbool.h>
#include <stdint.h>

#define BOOT_HEALTH_HANDOFF_ADDRESS 0x00004E00U
#define BOOT_HEALTH_HANDOFF_SIZE 64U
#define BOOT_HEALTH_HANDOFF_VERSION 1U
#define BOOT_HEALTH_STATUS_VERSION 1U
#define BOOT_HEALTH_STATUS_SYSTEM_READY (1U << 0)
#define BOOT_HEALTH_STATUS_PENDING_TRIAL (1U << 1)
#define BOOT_HEALTH_SLOT_A 0U
#define BOOT_HEALTH_SLOT_B 1U
#define BOOT_HEALTH_SLOT_NONE 0xFFU

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t flags;
    uint32_t resource;
    uint64_t sequence;
    uint32_t partition_lba;
    uint32_t partition_sectors;
    uint8_t selected_slot;
    uint8_t active_slot;
    uint8_t pending_slot;
    uint8_t attempts_remaining;
    uint32_t reserved;
} boot_health_status_t;

_Static_assert(sizeof(boot_health_status_t) == 40U,
               "boot-health status ABI changed");

bool boot_health_capture(void);
void boot_health_mark_system_ready(void);
int boot_health_get_status(boot_health_status_t *status);

#endif
