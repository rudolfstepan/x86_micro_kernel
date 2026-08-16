#ifndef AHCI_H
#define AHCI_H

#include <stddef.h>
#include <stdint.h>

#include "drivers/bus/pci.h"
#include "drivers/bus/drives.h"

#define AHCI_PCI_CLASS 0x01U
#define AHCI_PCI_SUBCLASS 0x06U
#define AHCI_PCI_PROG_IF 0x01U
#define AHCI_MAX_PORTS 32U
#define AHCI_COMMAND_LIST_SIZE 1024U
#define AHCI_RECEIVED_FIS_SIZE 256U
#define AHCI_COMMAND_TABLE_SIZE 256U
#define AHCI_VIRTUAL_BASE 0xA000U

#pragma pack(push, 1)
typedef struct {
    uint16_t flags;
    uint16_t prdt_length;
    uint32_t bytes_transferred;
    uint32_t command_table_base;
    uint32_t command_table_base_upper;
    uint32_t reserved[4];
} ahci_command_header_t;

typedef struct {
    uint8_t fis_type;
    uint8_t flags;
    uint8_t command;
    uint8_t feature_low;
    uint8_t lba0;
    uint8_t lba1;
    uint8_t lba2;
    uint8_t device;
    uint8_t lba3;
    uint8_t lba4;
    uint8_t lba5;
    uint8_t feature_high;
    uint8_t count_low;
    uint8_t count_high;
    uint8_t icc;
    uint8_t control;
    uint8_t reserved[4];
} ahci_fis_reg_h2d_t;

typedef struct {
    uint32_t data_base;
    uint32_t data_base_upper;
    uint32_t reserved;
    uint32_t byte_count_and_interrupt;
} ahci_prdt_entry_t;

typedef struct {
    uint8_t fis[64];
    uint8_t atapi_command[16];
    uint8_t reserved[48];
    ahci_prdt_entry_t prdt[8];
} ahci_command_table_t;
#pragma pack(pop)

_Static_assert(sizeof(ahci_command_header_t) == 32U,
               "AHCI command header ABI size changed");
_Static_assert(sizeof(ahci_fis_reg_h2d_t) == 20U,
               "AHCI H2D FIS ABI size changed");
_Static_assert(sizeof(ahci_prdt_entry_t) == 16U,
               "AHCI PRDT ABI size changed");
_Static_assert(sizeof(ahci_command_table_t) == AHCI_COMMAND_TABLE_SIZE,
               "AHCI command table ABI size changed");

typedef struct {
    uint64_t abar;
    volatile uint32_t *mmio;
    uint32_t capability;
    uint32_t implemented_ports;
    uint32_t sata_ports;
    uint32_t version;
    uint32_t dma_ready_ports;
    uint64_t sector_count[AHCI_MAX_PORTS];
    uint32_t sector_size[AHCI_MAX_PORTS];
    char model[AHCI_MAX_PORTS][41];
    uint32_t identify_valid_ports;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t port_count;
    uint8_t valid;
} ahci_controller_info_t;

/* Probe only: no port or DMA state is published until AHCI initialization
 * validates the controller and its MMIO region. */
size_t ahci_probe_controllers(ahci_controller_info_t *output,
                              size_t capacity);
void ahci_init(void);
/** Packed probe summary: functions, controllers, links, drives. */
uint32_t ahci_probe_diagnostics(void);
bool ahci_read_sector(const drive_t *drive, uint32_t sector, void *buffer);
bool ahci_write_sector(const drive_t *drive, uint32_t sector,
                       const void *buffer);
bool ahci_flush(const drive_t *drive);
void ahci_fence_writes(void);

#endif
