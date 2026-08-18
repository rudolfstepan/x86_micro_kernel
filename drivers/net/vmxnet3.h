/**
 * @file drivers/net/vmxnet3.h
 * @brief VMXNET3-Geräte- und Ringvertrag.
 *
 * Layer: Ring-0 network driver and mediation.
 * Contract: Frames und Gerätezustand werden vor Veröffentlichung vollständig validiert.
 * Safety: Descriptor-Ringe, DMA-Adressen und Ownership-Bits müssen konsistent bleiben.
 */
#ifndef VMXNET3_H
#define VMXNET3_H

#include <stdint.h>
#include <stdbool.h>

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
    uint32_t tx_consumer;             // oldest outstanding TX buffer
    uint32_t tx_pending;              // number of buffers owned by the device
    uint32_t rx_producer;             // RX producer index
    bool initialized;
} vmxnet3_device_t;

void vmxnet3_init(vmxnet3_device_t *dev);
void vmxnet3_register_driver(void);
void vmxnet3_setup(void);
void vmxnet3_transmit_packet(vmxnet3_device_t *dev, const uint8_t *data,
                             uint16_t length);
void vmxnet3_receive_packet(vmxnet3_device_t *dev);
void vmxnet3_send_packet(const uint8_t *data, uint16_t length);
void vmxnet3_get_mac_address(vmxnet3_device_t *dev, uint8_t *mac);
void test_vmxnet3(void);

#endif // VMXNET3_H
