#include "ahci.h"

#include "arch/x86/mm/paging.h"
#include "lib/libc/string.h"
#include "kernel/time/pit.h"

#define AHCI_MAX_CONTROLLERS 4U
#define AHCI_MMIO_SIZE 0x1100U
#define AHCI_RESET_TIMEOUT_MS 1000U
#define AHCI_RESET_MAX_POLLS 1000000U
#define AHCI_GHC_HR (1U << 0)
#define AHCI_GHC_AE (1U << 31)
#define AHCI_PORT_BASE 0x100U
#define AHCI_PORT_STRIDE 0x80U
#define AHCI_PORT_SIG 0x24U
#define AHCI_PORT_SSTS 0x28U
#define AHCI_SIG_ATA 0x00000101U

static ahci_controller_info_t controllers[AHCI_MAX_CONTROLLERS];
static size_t controller_count;

static int ahci_bar5(const pci_device_t *device, uint64_t *address) {
    if (device == NULL || address == NULL) return -22;
    uint32_t low = device->bar[5];
    /* ABAR is BAR5 in a type-0 header, so there is no following BAR available
     * as the high half of a 64-bit address. Accept only a 32-bit memory BAR. */
    if (low == 0U || low == 0xFFFFFFFFU || (low & 1U) != 0U ||
        (low & 0x6U) != 0U) return -95;
    *address = (uint64_t)(low & ~0xFU);
    return *address == 0U ? -95 : 0;
}

static uint32_t ahci_read(volatile uint32_t *mmio, uint32_t offset) {
    return mmio[offset / sizeof(uint32_t)];
}

static void ahci_write(volatile uint32_t *mmio, uint32_t offset,
                       uint32_t value) {
    mmio[offset / sizeof(uint32_t)] = value;
}

static bool ahci_reset(volatile uint32_t *mmio) {
    uint64_t start = pit_monotonic_ms();
    ahci_write(mmio, 0x04U, ahci_read(mmio, 0x04U) | AHCI_GHC_HR);
    for (uint32_t poll = 0U; poll < AHCI_RESET_MAX_POLLS; ++poll) {
        if ((ahci_read(mmio, 0x04U) & AHCI_GHC_HR) == 0U) return true;
        uint64_t now = pit_monotonic_ms();
        if (now < start || now - start >= AHCI_RESET_TIMEOUT_MS) break;
    }
    return false;
}

static bool ahci_port_is_sata(volatile uint32_t *mmio, uint32_t port) {
    uint32_t base = AHCI_PORT_BASE + port * AHCI_PORT_STRIDE;
    uint32_t status = ahci_read(mmio, base + AHCI_PORT_SSTS);
    uint32_t det = status & 0x0FU;
    uint32_t ipm = (status >> 8U) & 0x0FU;
    return det == 3U && ipm == 1U &&
           ahci_read(mmio, base + AHCI_PORT_SIG) == AHCI_SIG_ATA;
}

static bool ahci_initialize_controller(ahci_controller_info_t *controller,
                                       pci_device_t *device) {
    if (controller == NULL || device == NULL || controller->abar > UINT32_MAX)
        return false;
    pci_enable_device(device);
    volatile uint32_t *mmio = map_mmio_region(controller->abar,
                                               AHCI_MMIO_SIZE);
    if (mmio == NULL || !ahci_reset(mmio)) return false;
    ahci_write(mmio, 0x04U, ahci_read(mmio, 0x04U) | AHCI_GHC_AE);
    uint32_t capability = ahci_read(mmio, 0x00U);
    uint32_t port_limit = (capability & 0x1FU) + 1U;
    uint32_t implemented = ahci_read(mmio, 0x0CU);
    uint32_t sata_ports = 0U;
    for (uint32_t port = 0U; port < port_limit && port < 32U; ++port) {
        uint32_t bit = 1U << port;
        if ((implemented & bit) != 0U && ahci_port_is_sata(mmio, port))
            sata_ports |= bit;
    }
    controller->mmio = mmio;
    controller->capability = capability;
    controller->implemented_ports = implemented;
    controller->sata_ports = sata_ports;
    controller->version = ahci_read(mmio, 0x10U);
    controller->port_count = (uint8_t)port_limit;
    controller->valid = 1U;
    return true;
}

size_t ahci_probe_controllers(ahci_controller_info_t *output,
                              size_t capacity) {
    size_t found = 0U;
    if (output == NULL && capacity != 0U) return 0U;
    for (size_t index = 0U; index < pci_device_count; ++index) {
        pci_device_t *device = &pci_devices[index];
        uint64_t abar = 0U;
        if (device->class_code != AHCI_PCI_CLASS ||
            device->subclass_code != AHCI_PCI_SUBCLASS ||
            device->prog_if != AHCI_PCI_PROG_IF ||
            ahci_bar5(device, &abar) != 0) continue;
        if (found < capacity) {
            output[found] = (ahci_controller_info_t){
                .abar = abar, .bus = device->bus, .slot = device->slot,
                .function = device->function, .port_count = 0U, .valid = 0U
            };
        }
        ++found;
    }
    return found;
}

void ahci_init(void) {
    memset(controllers, 0, sizeof(controllers));
    controller_count = ahci_probe_controllers(controllers,
                                               AHCI_MAX_CONTROLLERS);
    if (controller_count > AHCI_MAX_CONTROLLERS)
        controller_count = AHCI_MAX_CONTROLLERS;
    for (size_t index = 0U; index < controller_count; ++index) {
        for (size_t device_index = 0U; device_index < pci_device_count;
             ++device_index) {
            pci_device_t *device = &pci_devices[device_index];
            if (device->bus == controllers[index].bus &&
                device->slot == controllers[index].slot &&
                device->function == controllers[index].function) {
                (void)ahci_initialize_controller(&controllers[index], device);
                break;
            }
        }
    }
}
