#ifndef AHCI_H
#define AHCI_H

#include <stddef.h>
#include <stdint.h>

#include "drivers/bus/pci.h"

#define AHCI_PCI_CLASS 0x01U
#define AHCI_PCI_SUBCLASS 0x06U
#define AHCI_PCI_PROG_IF 0x01U
#define AHCI_MAX_PORTS 32U
#define AHCI_COMMAND_LIST_SIZE 1024U
#define AHCI_RECEIVED_FIS_SIZE 256U
#define AHCI_COMMAND_TABLE_SIZE 256U

typedef struct {
    uint64_t abar;
    volatile uint32_t *mmio;
    uint32_t capability;
    uint32_t implemented_ports;
    uint32_t sata_ports;
    uint32_t version;
    uint32_t dma_ready_ports;
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

#endif
