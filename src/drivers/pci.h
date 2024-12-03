#ifndef PCI_H
#define PCI_H

#include <stdint.h>


// PCI-Konstanten
#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

// PCI-Befehlsregister-Flags
#define PCI_COMMAND 0x04
#define PCI_COMMAND_BUS_MASTER 0x04


uint8_t pci_get_irq(uint8_t bus, uint8_t device, uint8_t function);
void pci_set_irq(uint8_t bus, uint8_t device, uint8_t function, uint8_t irq);
uint32_t pci_read(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
void pci_write(uint8_t bus, uint8_t slot, uint8_t offset, uint8_t size, uint32_t value);
void pci_set_bus_master(uint8_t bus, uint8_t slot, uint8_t enable);
uint32_t get_io_base(uint8_t bus, uint8_t device, uint8_t function);

#endif // PCI_H