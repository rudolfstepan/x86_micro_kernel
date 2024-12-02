#include "pci.h"

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
