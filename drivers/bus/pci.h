/**
 * @file drivers/bus/pci.h
 * @brief PCI-Geräte-, BAR- und Treiberregistrierungsvertrag.
 *
 * Layer: Ring-0 block and bus driver.
 * Contract: Ressourcen, LBA-Bereiche und Backendbesitz werden vor jedem Seiteneffekt validiert.
 * Safety: Konfigurationszugriffe prüfen Busadresse und bewahren erkannte Geräteidentität.
 */
#ifndef PCI_H
#define PCI_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>


// PCI-Konstanten
#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

// PCI-Befehlsregister-Flags
#define PCI_COMMAND 0x04
#define PCI_COMMAND_IO 0x01
#define PCI_COMMAND_MEMORY 0x02
#define PCI_COMMAND_BUS_MASTER 0x04
#define PCI_COMMAND_INTERRUPT_DISABLE 0x0400U
#define PCI_STATUS_CAPABILITIES_LIST 0x0010U
#define PCI_CAPABILITY_PCIE 0x10U
#define PCI_PCIE_DEVICE_CAP_FLR (1U << 28U)
#define PCI_PCIE_DEVICE_CONTROL_FLR (1U << 15U)
#define PCI_IRQ_INVALID 0xFF
#define PCI_LEGACY_IRQ_COUNT 16

enum {
    PCI_OWNER_UNBOUND = 0U,
    PCI_OWNER_KERNEL = 1U,
    PCI_OWNER_DRIVER_DOMAIN = 2U,
};

enum {
    PCI_BAR_INFO_MMIO = 1U << 0U,
    PCI_BAR_INFO_PIO = 1U << 1U,
    PCI_BAR_INFO_64BIT = 1U << 2U,
    PCI_BAR_INFO_PREFETCHABLE = 1U << 3U,
};

typedef struct {
    uint32_t index;
    uint32_t flags;
    uint32_t base_low;
    uint32_t base_high;
    uint32_t size_low;
    uint32_t size_high;
} pci_bar_info_t;

_Static_assert(sizeof(pci_bar_info_t) == 24U, "PCI BAR info ABI changed");

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
    uint8_t owner;              // Exclusive kernel/domain binding state
} pci_device_t;
#pragma pack(pop)


typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    const char *name;
    int (*probe)(pci_device_t *dev);
} pci_driver_t;

extern pci_device_t pci_devices[];
extern size_t pci_device_count;


uint8_t pci_get_irq(uint8_t bus, uint8_t device, uint8_t function);
void pci_set_irq(uint8_t bus, uint8_t device, uint8_t function, uint8_t irq);
uint32_t pci_read(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
void pci_write(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset,
               uint8_t size, uint32_t value);
void pci_set_bus_master(uint8_t bus, uint8_t slot, uint8_t function, uint8_t enable);
bool pci_set_bus_master_verified(const pci_device_t *device, bool enabled);
bool pci_set_intx_disabled_verified(const pci_device_t *device, bool disabled);
/** Perform PCIe Function Level Reset with a monotonic total deadline. */
bool pci_function_reset_verified(const pci_device_t *device,
                                 uint64_t deadline_ms);
const pci_device_t *pci_find_location(uint32_t pci_location);
uint32_t pci_location(const pci_device_t *device);
bool pci_claim_for_driver_domain(uint32_t pci_location, uint16_t vendor_id,
                                 uint16_t device_id, uint8_t class_code,
                                 uint8_t subclass_code, uint8_t prog_if);
/** Size one BAR with decoding disabled and verified configuration restore. */
bool pci_describe_bar(const pci_device_t *device, uint32_t bar_index,
                      pci_bar_info_t *info);
uint32_t get_io_base(uint8_t bus, uint8_t device, uint8_t function);

void pci_init(void);
void pci_scan_bus(uint8_t bus);
void pci_scan_function(uint8_t bus, uint8_t slot, uint8_t function);
void pci_scan_slot(uint8_t bus, uint8_t slot);
void pci_write_config_word(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint16_t value);
uint8_t pci_read_config_byte(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset);
uint16_t pci_read_config_word(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset);
uint32_t pci_read_config_dword(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset);

void pci_enable_device(pci_device_t *dev);
uint32_t pci_read_bar(pci_device_t *dev, uint8_t bar_index);
volatile uint32_t *map_mmio(uint64_t physical_address);
volatile uint32_t *map_mmio_region(uint64_t physical_address, size_t length);
uint8_t pci_configure_irq(pci_device_t *dev);
bool pci_irq_is_valid(uint8_t irq);
void pci_register_driver(uint16_t vendor_id, uint16_t device_id, int (*probe)(pci_device_t *));
void pci_register_driver_named(uint16_t vendor_id, uint16_t device_id,
                               const char *name,
                               int (*probe)(pci_device_t *));
void pci_probe_drivers(void);
int pci_device_exists(uint16_t vendor_id, uint16_t device_id);


#endif // PCI_H
