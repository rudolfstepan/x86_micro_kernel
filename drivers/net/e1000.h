#ifndef E1000_H
#define E1000_H

#include <stdint.h>
#include <stddef.h>


// Define the size of the transmit and receive rings
struct e1000_tx_desc {
    volatile uint64_t buffer_addr; // Address of the data buffer
    volatile uint16_t length;      // Length of the data buffer
    volatile uint8_t  cso;         // Checksum offset
    volatile uint8_t  cmd;         // Command field (RS, IC, EOP, etc.)
    volatile uint8_t  status;      // Descriptor status (DD bit)
    volatile uint8_t  css;         // Checksum start
    volatile uint16_t special;     // Special field
} __attribute__((packed));

struct e1000_rx_desc {
    volatile uint64_t buffer_addr; // Address of the data buffer
    volatile uint16_t length;      // Length of the received data
    volatile uint16_t checksum;    // Packet checksum
    volatile uint8_t  status;      // Descriptor status (DD, EOP bits)
    volatile uint8_t  errors;      // Errors during reception
    volatile uint16_t special;     // Special field
} __attribute__((packed));

void e1000_detect();
void e1000_send_test_packet();

#endif // E1000_H