#ifndef PCI_H
#define PCI_H

#include <stdint.h>


// PCI-Konstanten
#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

// PCI-Befehlsregister-Flags
#define PCI_COMMAND 0x04
#define PCI_COMMAND_BUS_MASTER 0x04

// PCI-Konfigurationsregister-Offsets
#pragma pack(push, 1)
typedef struct {
    uint16_t vendor_id;         // Vendor ID of the PCI device
    uint16_t device_id;         // Device ID of the PCI device
    uint8_t bus;                // PCI bus number
    uint8_t slot;               // PCI slot number
    uint8_t function;           // PCI function number
    uint32_t bar[6];            // Base Address Registers (BARs), up to 6
    uint8_t irq_line;           // Interrupt line (IRQ number)
    uint8_t irq_pin;            // Interrupt pin (optional, A-D)
    uint8_t header_type;        // Header type of the PCI device
    uint8_t class_code;         // Class code (e.g., network controller)
    uint8_t subclass_code;      // Subclass code (e.g., Ethernet controller)
    uint8_t prog_if;            // Programming interface
    uint8_t revision_id;        // Revision ID of the device
} pci_device_t;
#pragma pack(pop)


typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    int (*probe)(pci_device_t *dev);
} pci_driver_t;


uint8_t pci_get_irq(uint8_t bus, uint8_t device, uint8_t function);
void pci_set_irq(uint8_t bus, uint8_t device, uint8_t function, uint8_t irq);
uint32_t pci_read(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
void pci_write(uint8_t bus, uint8_t slot, uint8_t offset, uint8_t size, uint32_t value);
void pci_set_bus_master(uint8_t bus, uint8_t slot, uint8_t enable);
uint32_t get_io_base(uint8_t bus, uint8_t device, uint8_t function);

void pci_init();
void pci_scan_bus(uint8_t bus);
void pci_scan_function(uint8_t bus, uint8_t slot, uint8_t function);
void pci_scan_slot(uint8_t bus, uint8_t slot);
void pci_scan_function(uint8_t bus, uint8_t slot, uint8_t function);
void pci_write_config_word(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint16_t value);
uint8_t pci_read_config_byte(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset);
uint16_t pci_read_config_word(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset);
uint32_t pci_read_config_dword(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset);

void pci_enable_device(pci_device_t *dev);
uint32_t pci_read_bar(pci_device_t *dev, uint8_t bar_index);
volatile uint32_t *map_mmio(uint64_t physical_address);
uint8_t pci_configure_irq(pci_device_t *dev);
void pci_register_driver(uint16_t vendor_id, uint16_t device_id, int (*probe)(pci_device_t *));
void pci_probe_drivers();
int pci_device_exists(uint16_t vendor_id, uint16_t device_id);


#endif // PCI_H