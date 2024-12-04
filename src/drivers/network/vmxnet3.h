#ifndef VMXNET3_H
#define VMXNET3_H

#include <stdint.h>

#define TX_QUEUE_SIZE 256
#define RX_QUEUE_SIZE 256
#define PACKET_BUFFER_SIZE 2048

typedef struct {
    uint8_t data[PACKET_BUFFER_SIZE]; // Packet data buffer
    uint16_t length;                  // Packet length
} vmxnet3_buffer_t;

typedef struct {
    vmxnet3_buffer_t tx_buffers[TX_QUEUE_SIZE];
    vmxnet3_buffer_t rx_buffers[RX_QUEUE_SIZE];
    volatile uint32_t *mmio_base;     // MMIO base address
    uint32_t irq;                     // IRQ number
    uint32_t tx_producer;             // TX producer index
    uint32_t rx_producer;             // RX producer index
} vmxnet3_device_t;

void vmxnet3_setup();
void test_vmxnet3();

#endif // VMXNET3_H