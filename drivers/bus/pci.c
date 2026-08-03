#include "pci.h"

#include "drivers/char/io.h"
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

// Liest aus dem PCI-Konfigurationsraum
uint32_t pci_read(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t address = (1 << 31) |
                       ((uint32_t)bus << 16) |
                       ((uint32_t)device << 11) |
                       ((uint32_t)function << 8) |
                       (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

// Schreibt in den PCI-Konfigurationsraum
void pci_write(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset,
               uint8_t size, uint32_t value) {
    uint32_t address = (1U << 31) |
                       ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) |
                       ((uint32_t)function << 8) |
                       ((uint32_t)(offset & 0xFC));
    outl(PCI_CONFIG_ADDRESS, address);
    switch (size) {
        case 1: outb(PCI_CONFIG_DATA + (offset & 3), (uint8_t)value); break;
        case 2: outw(PCI_CONFIG_DATA + (offset & 2), (uint16_t)value); break;
        case 4: outl(PCI_CONFIG_DATA, value); break;
        default: printf("Fehler: Ungültige Schreibgröße (%d)\n", size); break;
    }
}

// Aktiviert Bus-Mastering
void pci_set_bus_master(uint8_t bus, uint8_t slot, uint8_t function, uint8_t enable) {
    printf("Setting Bus-Mastering for device %u:%u.%u\n", bus, slot, function);

    // Read the current value of the PCI Command register
    uint16_t command = pci_read_config_word(bus, slot, function, PCI_COMMAND);

    if (enable) {
        // Enable Bus Mastering if not already enabled
        if (!(command & PCI_COMMAND_BUS_MASTER)) {
            command |= PCI_COMMAND_BUS_MASTER;
            pci_write_config_word(bus, slot, function, PCI_COMMAND, command);
            printf("++++ Bus Mastering Enabled ++++\n");
        } else {
            printf("Bus Mastering already enabled.\n");
        }
    } else {
        // Disable Bus Mastering if currently enabled
        if (command & PCI_COMMAND_BUS_MASTER) {
            command &= ~PCI_COMMAND_BUS_MASTER;
            pci_write_config_word(bus, slot, function, PCI_COMMAND, command);
            printf("---- Bus Mastering Disabled ----\n");
        } else {
            printf("Bus Mastering already disabled.\n");
        }
    }
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
    // Construct the configuration address
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (function << 8) | (offset & 0xFC) | 0x80000000);

    // Write the address to the PCI configuration address port (0xCF8)
    outl(0xCF8, address);

    // Write the 16-bit value to the PCI configuration data port (0xCFC)
    uint32_t current_value = inl(0xCFC); // Preserve the other half of the dword.
    if (offset & 0x2) {
        current_value = (current_value & 0x0000FFFFu) | ((uint32_t)value << 16);
    } else {
        current_value = (current_value & 0xFFFF0000u) | value;
    }

    outl(0xCFC, current_value);
}

// Function to read a 32-bit value from the PCI configuration space
uint32_t pci_read_config_dword(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (function << 8) | (offset & 0xFC) | 0x80000000);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
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

    // Read BARs
    for (int i = 0; i < 6; i++) {
        dev.bar[i] = pci_read_config_dword(bus, slot, function, 0x10 + i * 4);
    }

    // Save the device to the list
    if (pci_device_count < MAX_PCI_DEVICES) {
        pci_devices[pci_device_count++] = dev;
    }

    // Traverse PCI-to-PCI bridges immediately; the full-bus fallback in
    // pci_init also covers unusual multi-root firmware layouts.
    if (dev.class_code == 0x06 && dev.subclass_code == 0x04) {
        uint8_t secondary_bus =
            pci_read_config_byte(bus, slot, function, 0x19);
        if (secondary_bus != bus) pci_scan_bus(secondary_bus);
    }

    // Check for multifunction devices
    if ((function == 0) && (dev.header_type & 0x80)) {
        for (uint8_t func = 1; func < 8; func++) {
            pci_scan_function(bus, slot, func);
        }
    }
}

// Scan a specific slot on the PCI bus
void pci_scan_slot(uint8_t bus, uint8_t slot) {
    pci_scan_function(bus, slot, 0);
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

    // Start with the conventional root and then cover additional roots which
    // are not represented by a discoverable PCI-to-PCI bridge.
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

// Stub for mapping physical memory (platform-specific implementation required)
volatile uint32_t *map_mmio(uint64_t physical_address) {
    // Replace with actual memory mapping mechanism for your system.
    //return (volatile uint32_t *)(physical_address | 0xFFFF800000000000); // Simplified example
    return (volatile uint32_t *)(uintptr_t)physical_address;
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

void pci_register_driver(uint16_t vendor_id, uint16_t device_id, int (*probe)(pci_device_t *)) {
    if (pci_driver_count < MAX_PCI_DRIVERS) {
        pci_drivers[pci_driver_count].vendor_id = vendor_id;
        pci_drivers[pci_driver_count].device_id = device_id;
        pci_drivers[pci_driver_count].probe = probe;
        pci_driver_count++;
    }
}

void pci_probe_drivers(void) {
    for (size_t i = 0; i < pci_device_count; i++) {
        pci_device_t *dev = &pci_devices[i];
        for (size_t j = 0; j < pci_driver_count; j++) {
            if (dev->vendor_id == pci_drivers[j].vendor_id &&
                dev->device_id == pci_drivers[j].device_id) {
                pci_drivers[j].probe(dev);
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
