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


#endif // PCI_H