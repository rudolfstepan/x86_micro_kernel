#include "pci.h"

#include "drivers/io/io.h"
#include "toolchain/stdio.h"
#include <stdint.h>


uint8_t pci_get_irq(uint8_t bus, uint8_t device, uint8_t function) {
    // Construct the PCI configuration address
    uint32_t address = (1 << 31) | (bus << 16) | (device << 11) | 
                       (function << 8) | (0x3C & 0xFC);

    // Write the address to the PCI configuration address port
    asm volatile ("outl %0, %1" : : "a"(address), "Nd"(PCI_CONFIG_ADDRESS));

    // Read the data from the PCI configuration data port
    uint32_t data;
    asm volatile ("inl %1, %0" : "=a"(data) : "Nd"(PCI_CONFIG_DATA));

    // Extract the IRQ number from the Interrupt Line field (lower byte)
    uint8_t irq = (uint8_t)(data & 0xFF);
    printf("Device IRQ: %u\n", irq);
    return irq;
}

void pci_set_irq(uint8_t bus, uint8_t device, uint8_t function, uint8_t irq) {
    uint32_t address = (1 << 31) | (bus << 16) | (device << 11) | (function << 8) | (0x3C & 0xFC);

    // Write the address to the PCI configuration address port
    asm volatile ("outl %0, %1" : : "a"(address), "Nd"(PCI_CONFIG_ADDRESS));

    // Read the current value
    uint32_t value;
    asm volatile ("inl %1, %0" : "=a"(value) : "Nd"(PCI_CONFIG_DATA));

    // Modify the interrupt line (lower byte)
    value = (value & 0xFFFFFF00) | irq;

    // Write the modified value back
    asm volatile ("outl %0, %1" : : "a"(address), "Nd"(PCI_CONFIG_ADDRESS));
    asm volatile ("outl %0, %1" : : "a"(value), "Nd"(PCI_CONFIG_DATA));
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
void pci_write(uint8_t bus, uint8_t slot, uint8_t offset, uint8_t size, uint32_t value) {
    uint32_t address = (1U << 31) |
                       ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) |
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
void enable_bus_master(uint8_t bus, uint8_t slot) {
    printf("Aktiviere Bus-Mastering für Gerät %u:%u\n", bus, slot);
    uint16_t command = pci_read(bus, slot, PCI_COMMAND, 2);
    if (!(command & PCI_COMMAND_BUS_MASTER)) {
        command |= PCI_COMMAND_BUS_MASTER;
        pci_write(bus, slot, PCI_COMMAND, 2, command);
        printf("++++Bus Mastering aktiviert.++++\n");
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
