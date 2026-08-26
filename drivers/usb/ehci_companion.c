/**
 * @file drivers/usb/ehci_companion.c
 * @brief EHCI ownership and companion-routing mechanism.
 *
 * This is deliberately not an EHCI transfer driver.  It only quiesces an
 * otherwise unsupported EHCI function and clears CONFIGFLAG so firmware-owned
 * USB-2 ports become visible to OHCI/UHCI companions.
 */
#include "ehci_companion.h"

#include "arch/x86/include/interrupt.h"
#include "drivers/bus/pci.h"
#include "kernel/time/pit.h"
#include "lib/libc/stdio.h"

#include <stddef.h>

#define EHCI_MMIO_BYTES 0x1000U
#define EHCI_CAP_HCCPARAMS 0x08U
#define EHCI_OP_USBCMD 0x00U
#define EHCI_OP_USBSTS 0x04U
#define EHCI_OP_USBINTR 0x08U
#define EHCI_OP_CONFIGFLAG 0x40U
#define EHCI_USBCMD_RUN (1U << 0U)
#define EHCI_USBSTS_HALTED (1U << 12U)
#define EHCI_LEGACY_CAP_ID 0x01U
#define EHCI_HANDOFF_TIMEOUT_MS 1000U
#define EHCI_HALT_TIMEOUT_MS 100U

static ehci_companion_diagnostics_t companion_diagnostics = {
    .version = EHCI_COMPANION_DIAGNOSTICS_VERSION,
    .struct_size = sizeof(ehci_companion_diagnostics_t),
};

static bool wait_next_tick(uint64_t deadline) {
    uint64_t now = pit_monotonic_ms();
    if (now >= deadline || !irq_enabled() || irq_in_context()) return false;
    cpu_halt();
    return true;
}

static bool wait_config_byte_clear(const pci_device_t *device,
                                   uint8_t offset, uint64_t deadline) {
    while (pci_read_config_byte(device->bus, device->slot, device->function,
                                offset) != 0U) {
        if (!wait_next_tick(deadline)) return false;
    }
    return true;
}

static bool claim_legacy_ownership(const pci_device_t *device,
                                   volatile uint32_t *capability) {
    uint8_t eecp = (uint8_t)((capability[EHCI_CAP_HCCPARAMS >> 2U] >> 8U) &
                             0xFFU);
    for (uint32_t visited = 0U; visited < 48U && eecp >= 0x40U;
         ++visited) {
        uint32_t entry = pci_read_config_dword(
            device->bus, device->slot, device->function, eecp);
        const uint8_t id = (uint8_t)entry;
        const uint8_t next = (uint8_t)(entry >> 8U);
        if (id == EHCI_LEGACY_CAP_ID) {
            pci_write(device->bus, device->slot, device->function,
                      (uint8_t)(eecp + 3U), 1U, 1U);
            uint64_t now = pit_monotonic_ms();
            uint64_t deadline = now > UINT64_MAX - EHCI_HANDOFF_TIMEOUT_MS
                ? UINT64_MAX : now + EHCI_HANDOFF_TIMEOUT_MS;
            if (!wait_config_byte_clear(
                    device, (uint8_t)(eecp + 2U), deadline))
                return false;
            /* Disable legacy EHCI SMI sources after ownership transfer. */
            pci_write(device->bus, device->slot, device->function,
                      (uint8_t)(eecp + 4U), 4U, 0U);
            return true;
        }
        if (next < 0x40U || next == eecp) break;
        eecp = next;
    }
    return true;
}

static bool halt_controller(volatile uint32_t *operational) {
    operational[EHCI_OP_USBINTR >> 2U] = 0U;
    operational[EHCI_OP_USBCMD >> 2U] &= ~EHCI_USBCMD_RUN;
    uint64_t now = pit_monotonic_ms();
    uint64_t deadline = now > UINT64_MAX - EHCI_HALT_TIMEOUT_MS
        ? UINT64_MAX : now + EHCI_HALT_TIMEOUT_MS;
    while ((operational[EHCI_OP_USBSTS >> 2U] & EHCI_USBSTS_HALTED) == 0U) {
        if (!wait_next_tick(deadline)) return false;
    }
    return true;
}

static bool route_controller(pci_device_t *device) {
    uint32_t bar = pci_read_bar(device, 0U);
    if (bar == 0U || bar == UINT32_MAX || (bar & 1U) != 0U ||
        (bar & 0x6U) == 0x4U) {
        ++companion_diagnostics.invalid_bars;
        return false;
    }
    pci_enable_device(device);
    volatile uint32_t *capability = map_mmio_region(
        (uint64_t)(bar & ~0x0FU), EHCI_MMIO_BYTES);
    if (capability == NULL) {
        ++companion_diagnostics.invalid_bars;
        return false;
    }
    const uint8_t cap_length = *(volatile uint8_t *)capability;
    if (cap_length < 0x10U || cap_length > 0x80U ||
        (uint32_t)cap_length + EHCI_OP_CONFIGFLAG + sizeof(uint32_t) >
            EHCI_MMIO_BYTES) {
        ++companion_diagnostics.invalid_bars;
        return false;
    }
    if (!claim_legacy_ownership(device, capability)) {
        ++companion_diagnostics.handoff_failures;
        return false;
    }
    volatile uint32_t *operational = (volatile uint32_t *)(
        (volatile uint8_t *)capability + cap_length);
    if (!halt_controller(operational)) {
        ++companion_diagnostics.halt_failures;
        return false;
    }
    operational[EHCI_OP_CONFIGFLAG >> 2U] = 0U;
    __sync_synchronize();
    if (operational[EHCI_OP_CONFIGFLAG >> 2U] != 0U ||
        !pci_set_bus_master_verified(device, false)) {
        ++companion_diagnostics.handoff_failures;
        return false;
    }
    ++companion_diagnostics.routed;
    printf("USB: EHCI ports routed to companions at %u:%u.%u\n",
           (unsigned)device->bus, (unsigned)device->slot,
           (unsigned)device->function);
    return true;
}

bool ehci_route_ports_to_companions(void) {
    companion_diagnostics = (ehci_companion_diagnostics_t){
        .version = EHCI_COMPANION_DIAGNOSTICS_VERSION,
        .struct_size = sizeof(ehci_companion_diagnostics_t),
    };
    bool has_companion = false;
    for (size_t index = 0U; index < pci_device_count; ++index) {
        const pci_device_t *device = &pci_devices[index];
        if (device->class_code == 0x0CU && device->subclass_code == 0x03U &&
            (device->prog_if == 0x00U || device->prog_if == 0x10U)) {
            has_companion = true;
            break;
        }
    }
    if (!has_companion) return true;

    bool complete = true;
    for (size_t index = 0U; index < pci_device_count; ++index) {
        pci_device_t *device = &pci_devices[index];
        if (device->class_code != 0x0CU || device->subclass_code != 0x03U ||
            device->prog_if != 0x20U)
            continue;
        ++companion_diagnostics.controllers;
        if (!route_controller(device)) complete = false;
    }
    return complete;
}

bool ehci_companion_get_diagnostics(
        ehci_companion_diagnostics_t *diagnostics_out) {
    if (diagnostics_out == NULL) return false;
    *diagnostics_out = companion_diagnostics;
    return true;
}
