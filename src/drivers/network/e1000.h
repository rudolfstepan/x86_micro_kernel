#ifndef E1000_H
#define E1000_H

#include <stdint.h>
#include <stddef.h>

// Define constants for PCI configuration space and E1000 registers
#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC
#define E1000_VENDOR_ID    0x8086
#define E1000_DEVICE_ID    0x100E

// E1000 Register Offsets
#define E1000_REG_CTRL     0x0000
#define E1000_REG_STATUS   0x0008
#define E1000_REG_TCTL     0x0400
#define E1000_REG_RCTL     0x0100
#define E1000_REG_TIPG     0x0410
#define E1000_REG_TDBAL    0x03800
#define E1000_REG_TDBAH    0x03804
#define E1000_REG_TDLEN    0x03808
#define E1000_REG_TDH      0x03810
#define E1000_REG_TDT      0x03818
#define E1000_REG_RDBAL    0x02800
#define E1000_REG_RDBAH    0x02804
#define E1000_REG_RDLEN    0x02808
#define E1000_REG_RDH      0x02810
#define E1000_REG_RDT      0x02818
#define E1000_REG_IMS      0x00D0


// Define the size of the transmit and receive rings
struct e1000_tx_desc {
    uint64_t buffer_addr; // Address of the data buffer
    uint16_t length;      // Length of the data buffer
    uint8_t  cso;         // Checksum offset
    uint8_t  cmd;         // Command field (RS, IC, EOP, etc.)
    uint8_t  status;      // Descriptor status (DD bit)
    uint8_t  css;         // Checksum start
    uint16_t special;     // Special field
};

struct e1000_rx_desc {
    uint64_t buffer_addr; // Address of the data buffer
    uint16_t length;      // Length of the received data
    uint16_t checksum;    // Packet checksum
    uint8_t  status;      // Descriptor status (DD, EOP bits)
    uint8_t  errors;      // Errors during reception
    uint16_t special;     // Special field
};

void e1000_detect();
void e1000_send_test_packet();

#endif // E1000_H