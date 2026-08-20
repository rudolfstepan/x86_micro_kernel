/**
 * @file drivers/bus/pci.c
 * @brief PCI-Enumeration und gebundener Treiber-Dispatch.
 *
 * Layer: Ring-0 block and bus driver.
 * Contract: Ressourcen, LBA-Bereiche und Backendbesitz werden vor jedem Seiteneffekt validiert.
 * Safety: Geräte werden erst nach validierter Konfiguration in festen Tabellen publiziert.
 */
#include "pci.h"
#include "include/kernel/panic.h"

#include "drivers/char/io.h"
#include "arch/x86/mm/paging.h"
#include "arch/x86/include/interrupt.h"
#include "kernel/sched/scheduler.h"
#include "kernel/time/pit.h"
#include "lib/libc/stdio.h"
#include <stdint.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

// A simple list to store detected devices
#define MAX_PCI_DEVICES 1024


uint8_t pci_get_irq(uint8_t bus, uint8_t device, uint8_t function) {
    uint8_t irq = pci_read_config_byte(bus, device, function, 0x3C);
    printf("Device IRQ: %u\n", irq);
    return irq;
}

void pci_set_irq(uint8_t bus, uint8_t device, uint8_t function, uint8_t irq) {
    pci_write(bus, device, function, 0x3C, 1, irq);
}

static uint32_t pci_config_address(uint8_t bus, uint8_t device,
                                   uint8_t function, uint8_t offset) {
    return (1U << 31U) |
                       ((uint32_t)bus << 16) |
                       ((uint32_t)device << 11) |
                       ((uint32_t)function << 8) |
                       (offset & 0xFC);
}

static uint32_t pci_read_dword_locked(uint8_t bus, uint8_t device,
                                      uint8_t function, uint8_t offset) {
    uint32_t address = pci_config_address(bus, device, function, offset);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

static void pci_write_dword_locked(uint8_t bus, uint8_t device,
                                   uint8_t function, uint8_t offset,
                                   uint32_t value) {
    outl(PCI_CONFIG_ADDRESS,
         pci_config_address(bus, device, function, offset));
    outl(PCI_CONFIG_DATA, value);
}

static void pci_write_word_locked(uint8_t bus, uint8_t device,
                                  uint8_t function, uint8_t offset,
                                  uint16_t value) {
    outl(PCI_CONFIG_ADDRESS,
         pci_config_address(bus, device, function, offset));
    outw((uint16_t)(PCI_CONFIG_DATA + (offset & 2U)), value);
}

// Liest atomar aus dem globalen PCI-Konfigurationsregisterpaar CF8/CFC.
uint32_t pci_read(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t flags = irq_save();
    uint32_t value = pci_read_dword_locked(bus, device, function, offset);
    irq_restore(flags);
    return value;
}

// Schreibt in den PCI-Konfigurationsraum
void pci_write(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset,
               uint8_t size, uint32_t value) {
    if (size != 1U && size != 2U && size != 4U) {
        printf("Fehler: Ungültige PCI-Schreibgröße (%u)\n", size);
        return;
    }
    uint32_t address = pci_config_address(bus, slot, function, offset);
    uint32_t flags = irq_save();
    outl(PCI_CONFIG_ADDRESS, address);
    switch (size) {
        case 1: outb(PCI_CONFIG_DATA + (offset & 3), (uint8_t)value); break;
        case 2: outw(PCI_CONFIG_DATA + (offset & 2), (uint16_t)value); break;
        case 4: outl(PCI_CONFIG_DATA, value); break;
        default: break;
    }
    irq_restore(flags);
}

static bool pci_update_command_verified(const pci_device_t *device,
                                        uint16_t set_bits,
                                        uint16_t clear_bits) {
    if (device == NULL || device->vendor_id == 0U ||
        device->vendor_id == 0xFFFFU || (set_bits & clear_bits) != 0U)
        return false;
    uint32_t flags = irq_save();
    uint32_t value = pci_read_dword_locked(
        device->bus, device->slot, device->function, PCI_COMMAND);
    uint16_t command = (uint16_t)value;
    command = (uint16_t)((command | set_bits) & (uint16_t)~clear_bits);
    pci_write_word_locked(device->bus, device->slot, device->function,
                          PCI_COMMAND, command);
    uint16_t readback = (uint16_t)pci_read_dword_locked(
        device->bus, device->slot, device->function, PCI_COMMAND);
    irq_restore(flags);
    return (readback & set_bits) == set_bits &&
           (readback & clear_bits) == 0U;
}

bool pci_set_bus_master_verified(const pci_device_t *device, bool enabled) {
    return enabled
        ? pci_update_command_verified(device, PCI_COMMAND_BUS_MASTER, 0U)
        : pci_update_command_verified(device, 0U, PCI_COMMAND_BUS_MASTER);
}

bool pci_set_intx_disabled_verified(const pci_device_t *device, bool disabled) {
    return disabled
        ? pci_update_command_verified(device, PCI_COMMAND_INTERRUPT_DISABLE, 0U)
        : pci_update_command_verified(device, 0U,
                                      PCI_COMMAND_INTERRUPT_DISABLE);
}

static uint8_t pci_find_capability_locked(const pci_device_t *device,
                                          uint8_t capability_id) {
    uint16_t status = (uint16_t)(pci_read_dword_locked(
        device->bus, device->slot, device->function, PCI_COMMAND) >> 16U);
    if ((status & PCI_STATUS_CAPABILITIES_LIST) == 0U) return 0U;
    uint8_t pointer = (uint8_t)pci_read_dword_locked(
        device->bus, device->slot, device->function, 0x34U);
    uint64_t visited = 0U;
    for (uint32_t count = 0U; count < 48U; ++count) {
        pointer &= 0xFCU;
        if (pointer < 0x40U) return 0U;
        uint32_t bit = pointer >> 2U;
        if (bit >= 64U || (visited & (1ULL << bit)) != 0U) return 0U;
        visited |= 1ULL << bit;
        uint32_t capability = pci_read_dword_locked(
            device->bus, device->slot, device->function, pointer);
        if ((uint8_t)capability == capability_id) return pointer;
        pointer = (uint8_t)(capability >> 8U);
        if (pointer == 0U) return 0U;
    }
    return 0U;
}

bool pci_function_reset_verified(const pci_device_t *device,
                                 uint64_t deadline_ms) {
    if (device == NULL || device->owner != PCI_OWNER_DRIVER_DOMAIN ||
        deadline_ms == 0U) return false;
    uint64_t now_ms = pit_monotonic_ms();
    if (now_ms >= deadline_ms || deadline_ms - now_ms < 100U) return false;
    if (!pci_set_bus_master_verified(device, false) ||
        !pci_set_intx_disabled_verified(device, true)) return false;

    uint32_t flags = irq_save();
    uint8_t pcie = pci_find_capability_locked(device, PCI_CAPABILITY_PCIE);
    if (pcie == 0U || pcie > 0xF4U) {
        irq_restore(flags);
        return false;
    }
    uint32_t device_capabilities = pci_read_dword_locked(
        device->bus, device->slot, device->function,
        (uint8_t)(pcie + 4U));
    if ((device_capabilities & PCI_PCIE_DEVICE_CAP_FLR) == 0U) {
        irq_restore(flags);
        return false;
    }
    uint16_t control = (uint16_t)pci_read_dword_locked(
        device->bus, device->slot, device->function, (uint8_t)(pcie + 8U));
    pci_write_word_locked(device->bus, device->slot, device->function,
        (uint8_t)(pcie + 8U),
        (uint16_t)(control | PCI_PCIE_DEVICE_CONTROL_FLR));
    irq_restore(flags);

    uint64_t ready_ms = now_ms + 100U;
    while ((now_ms = pit_monotonic_ms()) < ready_ms) {
        if (now_ms >= deadline_ms) return false;
        if (scheduler_sleep_ms(1U) != 0) (void)scheduler_yield();
    }
    if (pit_monotonic_ms() >= deadline_ms) return false;
    uint32_t identity = pci_read_config_dword(
        device->bus, device->slot, device->function, 0U);
    if ((uint16_t)identity != device->vendor_id ||
        (uint16_t)(identity >> 16U) != device->device_id) return false;
    return pci_set_bus_master_verified(device, false) &&
           pci_set_intx_disabled_verified(device, true);
}

uint32_t pci_location(const pci_device_t *device) {
    if (device == NULL) return UINT32_MAX;
    return ((uint32_t)device->bus << 16U) |
           ((uint32_t)device->slot << 8U) | device->function;
}

const pci_device_t *pci_find_location(uint32_t location) {
    if ((location & 0xFF000000U) != 0U ||
        ((location >> 8U) & 0xFFU) >= 32U ||
        (location & 0xFFU) >= 8U) return NULL;
    for (size_t index = 0U; index < pci_device_count; ++index) {
        if (pci_location(&pci_devices[index]) == location)
            return &pci_devices[index];
    }
    return NULL;
}

bool pci_claim_for_driver_domain(uint32_t location, uint16_t vendor_id,
                                 uint16_t device_id, uint8_t class_code,
                                 uint8_t subclass_code, uint8_t prog_if) {
    uint32_t flags = irq_save();
    for (size_t index = 0U; index < pci_device_count; ++index) {
        pci_device_t *device = &pci_devices[index];
        if (pci_location(device) != location) continue;
        bool valid = device->owner == PCI_OWNER_UNBOUND &&
            device->vendor_id == vendor_id && device->device_id == device_id &&
            device->class_code == class_code &&
            device->subclass_code == subclass_code &&
            device->prog_if == prog_if;
        if (valid) device->owner = PCI_OWNER_DRIVER_DOMAIN;
        irq_restore(flags);
        return valid;
    }
    irq_restore(flags);
    return false;
}

bool pci_describe_bar(const pci_device_t *device, uint32_t bar_index,
                      pci_bar_info_t *info) {
    if (device == NULL || info == NULL || bar_index >= 6U ||
        device->owner != PCI_OWNER_DRIVER_DOMAIN ||
        (device->header_type & 0x7FU) != 0U) return false;
    if (bar_index != 0U) {
        uint32_t previous = device->bar[bar_index - 1U];
        if ((previous & 1U) == 0U && (previous & 6U) == 4U) return false;
    }

    uint8_t offset = (uint8_t)(0x10U + bar_index * 4U);
    uint32_t flags = irq_save();
    uint32_t original_low = pci_read_dword_locked(
        device->bus, device->slot, device->function, offset);
    if (original_low == 0U || original_low == UINT32_MAX) {
        irq_restore(flags);
        return false;
    }
    bool pio = (original_low & 1U) != 0U;
    bool is_64bit = !pio && (original_low & 6U) == 4U;
    if ((!pio && (original_low & 6U) != 0U && !is_64bit) ||
        (is_64bit && bar_index == 5U)) {
        irq_restore(flags);
        return false;
    }
    uint32_t original_high = is_64bit ? pci_read_dword_locked(
        device->bus, device->slot, device->function,
        (uint8_t)(offset + 4U)) : 0U;
    uint16_t original_command = (uint16_t)pci_read_dword_locked(
        device->bus, device->slot, device->function, PCI_COMMAND);
    uint16_t disabled_command = (uint16_t)(original_command &
        (uint16_t)~(PCI_COMMAND_IO | PCI_COMMAND_MEMORY));
    pci_write_word_locked(device->bus, device->slot, device->function,
                          PCI_COMMAND, disabled_command);
    uint16_t disabled_readback = (uint16_t)pci_read_dword_locked(
        device->bus, device->slot, device->function, PCI_COMMAND);
    if ((disabled_readback & (PCI_COMMAND_IO | PCI_COMMAND_MEMORY)) != 0U) {
        pci_write_word_locked(device->bus, device->slot, device->function,
                              PCI_COMMAND, original_command);
        irq_restore(flags);
        return false;
    }

    pci_write_dword_locked(device->bus, device->slot, device->function,
                           offset, UINT32_MAX);
    if (is_64bit)
        pci_write_dword_locked(device->bus, device->slot, device->function,
                               (uint8_t)(offset + 4U), UINT32_MAX);
    uint32_t mask_low = pci_read_dword_locked(
        device->bus, device->slot, device->function, offset);
    uint32_t mask_high = is_64bit ? pci_read_dword_locked(
        device->bus, device->slot, device->function,
        (uint8_t)(offset + 4U)) : 0U;
    pci_write_dword_locked(device->bus, device->slot, device->function,
                           offset, original_low);
    if (is_64bit)
        pci_write_dword_locked(device->bus, device->slot, device->function,
                               (uint8_t)(offset + 4U), original_high);
    pci_write_word_locked(device->bus, device->slot, device->function,
                          PCI_COMMAND, original_command);

    uint32_t restored_low = pci_read_dword_locked(
        device->bus, device->slot, device->function, offset);
    uint32_t restored_high = is_64bit ? pci_read_dword_locked(
        device->bus, device->slot, device->function,
        (uint8_t)(offset + 4U)) : 0U;
    uint16_t restored_command = (uint16_t)pci_read_dword_locked(
        device->bus, device->slot, device->function, PCI_COMMAND);
    irq_restore(flags);
    if (restored_low != original_low || restored_high != original_high ||
        restored_command != original_command) return false;

    uint64_t base = 0U;
    uint64_t size = 0U;
    uint32_t info_flags = pio ? PCI_BAR_INFO_PIO : PCI_BAR_INFO_MMIO;
    if (pio) {
        uint32_t mask = mask_low & ~3U;
        if (mask == 0U) return false;
        base = original_low & ~3U;
        size = (uint32_t)(~mask + 1U);
    } else if (is_64bit) {
        uint64_t mask = ((uint64_t)mask_high << 32U) |
            (uint64_t)(mask_low & ~0x0FU);
        if (mask == 0U) return false;
        base = ((uint64_t)original_high << 32U) |
            (uint64_t)(original_low & ~0x0FU);
        size = ~mask + 1U;
        info_flags |= PCI_BAR_INFO_64BIT;
    } else {
        uint32_t mask = mask_low & ~0x0FU;
        if (mask == 0U) return false;
        base = original_low & ~0x0FU;
        size = (uint32_t)(~mask + 1U);
    }
    if (size == 0U) return false;
    if (!pio && (original_low & 8U) != 0U)
        info_flags |= PCI_BAR_INFO_PREFETCHABLE;
    *info = (pci_bar_info_t){
        .index = bar_index,
        .flags = info_flags,
        .base_low = (uint32_t)base,
        .base_high = (uint32_t)(base >> 32U),
        .size_low = (uint32_t)size,
        .size_high = (uint32_t)(size >> 32U),
    };
    return true;
}

// Aktiviert Bus-Mastering
void pci_set_bus_master(uint8_t bus, uint8_t slot, uint8_t function, uint8_t enable) {
    printf("Setting Bus-Mastering for device %u:%u.%u\n", bus, slot, function);
    uint32_t location = ((uint32_t)bus << 16U) |
                        ((uint32_t)slot << 8U) | function;
    const pci_device_t *device = pci_find_location(location);
    bool updated = device != NULL &&
        pci_set_bus_master_verified(device, enable != 0U);
    printf(updated ? (enable != 0U
        ? "++++ Bus Mastering Enabled ++++\n"
        : "---- Bus Mastering Disabled ----\n")
        : "PCI: Bus Mastering update/readback failed.\n");
}

uint32_t get_io_base(uint8_t bus, uint8_t device, uint8_t function) {
    uint32_t bar0 = pci_read(bus, device, function, 0x10); // BAR0 auslesen

    // Prüfen, ob die Adresse für I/O oder Memory ist
    if (bar0 & 0x01) { // I/O-Bit gesetzt
        return bar0 & ~0x3; // Entferne die unteren Bits für Alignment
    } else {
        printf("Fehler: BAR0 zeigt auf eine Memory-Adresse, keine I/O-Adresse.\n");
        return 0; // Fehler
    }
}

//---------------------------------------------------------------------------------------------


pci_device_t pci_devices[MAX_PCI_DEVICES];
size_t pci_device_count = 0;
static bool pci_bus_scanned[256];


void pci_write_config_word(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint16_t value) {
    uint32_t flags = irq_save();
    uint32_t current_value = pci_read_dword_locked(
        bus, slot, function, offset); // Preserve the other half of the dword.
    if (offset & 0x2) {
        current_value = (current_value & 0x0000FFFFu) | ((uint32_t)value << 16);
    } else {
        current_value = (current_value & 0xFFFF0000u) | value;
    }

    pci_write_dword_locked(bus, slot, function, offset, current_value);
    irq_restore(flags);
}

// Function to read a 32-bit value from the PCI configuration space
uint32_t pci_read_config_dword(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    return pci_read(bus, slot, function, offset);
}

// Function to read a 16-bit value from the PCI configuration space
uint16_t pci_read_config_word(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    uint32_t value = pci_read_config_dword(bus, slot, function, offset);
    return (uint16_t)((value >> ((offset & 2) * 8)) & 0xFFFF);
}

// Function to read an 8-bit value from the PCI configuration space
uint8_t pci_read_config_byte(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    uint32_t value = pci_read_config_dword(bus, slot, function, offset);
    return (uint8_t)((value >> ((offset & 3) * 8)) & 0xFF);
}

// Scan a specific function of a device
void pci_scan_function(uint8_t bus, uint8_t slot, uint8_t function) {
    pci_device_t dev;

    // Read Vendor ID
    dev.vendor_id = pci_read_config_word(bus, slot, function, 0x00);
    if (dev.vendor_id == 0xFFFF) {
        // Device does not exist
        return;
    }

    // Store PCI location so callers can report bus/slot/function
    dev.bus = bus;
    dev.slot = slot;
    dev.function = function;

    // Populate the device information
    dev.device_id = pci_read_config_word(bus, slot, function, 0x02);
    dev.class_code = pci_read_config_byte(bus, slot, function, 0x0B);
    dev.subclass_code = pci_read_config_byte(bus, slot, function, 0x0A);
    dev.prog_if = pci_read_config_byte(bus, slot, function, 0x09);
    dev.revision_id = pci_read_config_byte(bus, slot, function, 0x08);
    dev.header_type = pci_read_config_byte(bus, slot, function, 0x0E);
    dev.irq_line = pci_read_config_byte(bus, slot, function, 0x3C);
    dev.irq_pin = pci_read_config_byte(bus, slot, function, 0x3D);
    dev.owner = PCI_OWNER_UNBOUND;

    // Read BARs
    for (int i = 0; i < 6; i++) {
        dev.bar[i] = pci_read_config_dword(bus, slot, function, 0x10 + i * 4);
    }

    // Save the device to the list
    if (pci_device_count < MAX_PCI_DEVICES) {
        pci_devices[pci_device_count++] = dev;
    }

}

// Scan a specific slot on the PCI bus
void pci_scan_slot(uint8_t bus, uint8_t slot) {
    if (pci_read_config_word(bus, slot, 0U, 0x00U) == 0xFFFFU) return;
    uint8_t header_type = pci_read_config_byte(bus, slot, 0U, 0x0EU);
    pci_scan_function(bus, slot, 0);
    if ((header_type & 0x80U) != 0U) {
        for (uint8_t function = 1U; function < 8U; ++function) {
            pci_scan_function(bus, slot, function);
        }
    }
}

// Scan the PCI bus
void pci_scan_bus(uint8_t bus) {
    if (pci_bus_scanned[bus]) return;
    pci_bus_scanned[bus] = true;
    for (uint8_t slot = 0; slot < 32; slot++) {
        pci_scan_slot(bus, slot);
    }
}

// PCI initialization function
void pci_init(void) {
    pci_device_count = 0; // Reset device count
    for (unsigned int bus = 0; bus < 256; ++bus) {
        pci_bus_scanned[bus] = false;
    }

    /* Cover every representable bus with fixed loop bounds.  This deliberately
     * avoids recursive bridge traversal and gives boot-time stack/WCET a hard
     * upper bound independent of malformed bridge topology. */
    pci_scan_bus(0);
    for (unsigned int bus = 1; bus < 256; ++bus) {
        pci_scan_bus((uint8_t)bus);
    }

    // Print the detected devices
    // for (size_t i = 0; i < pci_device_count; i++) {
    //     pci_device_t *dev = &pci_devices[i];
    //     printf("PCI Device %u: Vendor ID=0x%04X, Device ID=0x%04X, Class=0x%02X, Subclass=0x%02X\n",
    //            i, dev->vendor_id, dev->device_id, dev->class_code, dev->subclass_code);
    // }
}

void pci_enable_device(pci_device_t *dev) {
    if (!dev) return;

    uint16_t decode = 0;
    for (uint8_t index = 0; index < 6; ++index) {
        uint32_t bar = dev->bar[index];
        if (bar == 0 || bar == 0xFFFFFFFFu) continue;
        if (bar & 1u) decode |= PCI_COMMAND_IO;
        else {
            decode |= PCI_COMMAND_MEMORY;
            if ((bar & 0x6u) == 0x4u) ++index;
        }
    }

    uint16_t command = pci_read_config_word(dev->bus, dev->slot,
                                            dev->function, PCI_COMMAND);
    command |= decode;
    /* Bus mastering is enabled separately, after a driver has validated its
     * BARs/IRQ and prepared all DMA descriptors. */
    pci_write_config_word(dev->bus, dev->slot, dev->function, PCI_COMMAND,
                          command);
}

uint32_t pci_read_bar(pci_device_t *dev, uint8_t bar_index) {
    if (bar_index >= 6) return 0; // PCI has up to 6 BARs

    return dev->bar[bar_index];
}

volatile uint32_t *map_mmio(uint64_t physical_address) {
    return map_mmio_region(physical_address, PAGE_SIZE);
}

volatile uint32_t *map_mmio_region(uint64_t physical_address, size_t length) {
    if (physical_address > UINT32_MAX || length == 0 ||
        length > UINT32_MAX - (uint32_t)physical_address) {
        return NULL;
    }
    return (volatile uint32_t*)map_kernel_mmio((uint32_t)physical_address,
                                               length);
}

uint8_t pci_configure_irq(pci_device_t *dev) {
    if (!dev || !pci_irq_is_valid(dev->irq_line)) return PCI_IRQ_INVALID;
    return dev->irq_line;
}

bool pci_irq_is_valid(uint8_t irq) {
    return irq < PCI_LEGACY_IRQ_COUNT;
}

#define MAX_PCI_DRIVERS 32
pci_driver_t pci_drivers[MAX_PCI_DRIVERS];
size_t pci_driver_count = 0;

void pci_register_driver_named(uint16_t vendor_id, uint16_t device_id,
                               const char *name,
                               int (*probe)(pci_device_t *)) {
    if (pci_driver_count < MAX_PCI_DRIVERS) {
        pci_drivers[pci_driver_count].vendor_id = vendor_id;
        pci_drivers[pci_driver_count].device_id = device_id;
        pci_drivers[pci_driver_count].name = name;
        pci_drivers[pci_driver_count].probe = probe;
        pci_driver_count++;
    }
}

void pci_register_driver(uint16_t vendor_id, uint16_t device_id,
                         int (*probe)(pci_device_t *)) {
    pci_register_driver_named(vendor_id, device_id, "unnamed PCI driver",
                              probe);
}

void pci_probe_drivers(void) {
    for (size_t i = 0; i < pci_device_count; i++) {
        pci_device_t *dev = &pci_devices[i];
        if (dev->owner != PCI_OWNER_UNBOUND) continue;
        for (size_t j = 0; j < pci_driver_count; j++) {
            if (dev->vendor_id == pci_drivers[j].vendor_id &&
                dev->device_id == pci_drivers[j].device_id) {
                panic_context_set("driver-init", pci_drivers[j].name,
                                  "PCI probe", "hardware device");
                uint32_t identity = ((uint32_t)dev->vendor_id << 16U) |
                                    dev->device_id;
                uint32_t location = ((uint32_t)dev->bus << 16U) |
                                    ((uint32_t)dev->slot << 8U) |
                                    dev->function;
                panic_context_set_result(0, identity, location);
                int result = pci_drivers[j].probe(dev);
                panic_context_set_result(result, identity, location);
                if (result == 0) dev->owner = PCI_OWNER_KERNEL;
                break;
            }
        }
    }
}

/**
 * Check if a PCI device with specific vendor and device ID exists
 * @param vendor_id PCI vendor ID
 * @param device_id PCI device ID
 * @return 1 if device exists, 0 otherwise
 */
int pci_device_exists(uint16_t vendor_id, uint16_t device_id) {
    for (size_t i = 0; i < pci_device_count; i++) {
        if (pci_devices[i].vendor_id == vendor_id &&
            pci_devices[i].device_id == device_id) {
            return 1;
        }
    }
    return 0;
}
