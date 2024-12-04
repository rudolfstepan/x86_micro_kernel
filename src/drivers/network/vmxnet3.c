#include "vmxnet3.h"

#include "toolchain/stdio.h"
#include "toolchain/stdlib.h"
#include "toolchain/string.h"
#include "kernel/sys.h"
#include "drivers/pci.h"
#include "drivers/io/io.h"

#include <stdint.h>
#include <stddef.h>
#include "drivers/pci.h"      // Your kernel's PCI handling functions
#include "drivers/io/io.h"       // For port I/O and memory-mapped I/O
//#include "net.h"      // Networking stack integration

#define VMXNET3_VENDOR_ID 0x15AD
#define VMXNET3_DEVICE_ID 0x07B0


// Define offsets for VMXNET3 registers (based on the VMXNET3 specification)
#define VMXNET3_CMD        0x000
#define VMXNET3_STATUS     0x008
#define VMXNET3_TXPROD     0x1000
#define VMXNET3_RXPROD     0x2000
#define VMXNET3_INTR_STATUS 0x108


vmxnet3_device_t vmxnet3_device;

void vmxnet3_write_register(vmxnet3_device_t *dev, uint32_t offset, uint32_t value) {
    dev->mmio_base[offset / 4] = value;
}

uint32_t vmxnet3_read_register(vmxnet3_device_t *dev, uint32_t offset) {
    return dev->mmio_base[offset / 4];
}

void vmxnet3_init(vmxnet3_device_t *dev) {
    // Reset the device
    vmxnet3_write_register(dev, VMXNET3_CMD, 0x1);
    while (vmxnet3_read_register(dev, VMXNET3_STATUS) & 0x1);

    // Initialize TX and RX queues
    dev->tx_producer = 0;
    dev->rx_producer = 0;

    // Enable interrupts
    vmxnet3_write_register(dev, VMXNET3_INTR_STATUS, 0xFFFFFFFF); // Clear interrupts
}

void pci_get_mac_address(pci_device_t *pci_dev, uint8_t *mac) {
    uint32_t mac_low = pci_read_config_dword(pci_dev->bus, pci_dev->slot, pci_dev->function, 0x50);
    uint32_t mac_high = pci_read_config_dword(pci_dev->bus, pci_dev->slot, pci_dev->function, 0x54);

    mac[0] = (mac_low >> 0) & 0xFF;
    mac[1] = (mac_low >> 8) & 0xFF;
    mac[2] = (mac_low >> 16) & 0xFF;
    mac[3] = (mac_low >> 24) & 0xFF;
    mac[4] = (mac_high >> 0) & 0xFF;
    mac[5] = (mac_high >> 8) & 0xFF;

    printf("PCI MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void vmxnet3_handle_irq() {
    // Example: Handle an interrupt from the network card
    uint32_t status = vmxnet3_read_register(&vmxnet3_device, 0x108);
    if (status & 0x1) {
        // Handle received packet
        printf("Received packet\n");
    }
    if (status & 0x2) {
        // Handle transmitted packet
        printf("Transmitted packet\n");
    }
}

int vmxnet3_probe(pci_device_t *pci_dev) {
    if (pci_dev->vendor_id == VMXNET3_VENDOR_ID && pci_dev->device_id == VMXNET3_DEVICE_ID) {
        // Enable the device
        pci_enable_device(pci_dev);

        // Map MMIO region
        uint64_t bar0 = pci_read_bar(pci_dev, 0);
        vmxnet3_device.mmio_base = (volatile uint32_t *)map_mmio(bar0);

        // Configure IRQ
        vmxnet3_device.irq = pci_configure_irq(pci_dev);

        // Initialize the device
        vmxnet3_init(&vmxnet3_device);

        // Get MAC address
        uint8_t mac[6];
        pci_get_mac_address(pci_dev, mac);

        printf("VMXNET3 driver initialized\n");

        return 0;
    }
    return -1;
}

void vmxnet3_register_driver() {
    pci_register_driver(VMXNET3_VENDOR_ID, VMXNET3_DEVICE_ID, vmxnet3_probe);
}

void vmxnet3_setup() {
    // Register the VMXNET3 driver
    vmxnet3_register_driver();

    // Enable interrupts
    //enable_interrupts();

    
}

void vmxnet3_transmit_packet(vmxnet3_device_t *dev, const uint8_t *data, uint16_t length) {
    // Check if there's space in the TX queue
    uint32_t next_index = (dev->tx_producer + 1) % TX_QUEUE_SIZE;
    if (next_index == dev->tx_producer) {
        printf("TX queue is full\n");
        return;
    }

    // Copy data to the TX buffer
    memcpy(dev->tx_buffers[dev->tx_producer].data, data, length);
    dev->tx_buffers[dev->tx_producer].length = length;

    // Notify the device
    vmxnet3_write_register(dev, VMXNET3_TXPROD, dev->tx_producer);

    // Update producer index
    dev->tx_producer = next_index;
}

void vmxnet3_receive_packet(vmxnet3_device_t *dev) {
    // Check for received packets
    uint32_t rx_index = dev->rx_producer;
    if (vmxnet3_read_register(dev, VMXNET3_RXPROD) == rx_index) {
        printf("No packets received\n");
        return;
    }

    // Process the received packet
    vmxnet3_buffer_t *rx_buffer = &dev->rx_buffers[rx_index];
    printf("Received packet of length %d: %s\n", rx_buffer->length, rx_buffer->data);

    // Update producer index
    dev->rx_producer = (rx_index + 1) % RX_QUEUE_SIZE;

    // Notify the device
    vmxnet3_write_register(dev, VMXNET3_RXPROD, dev->rx_producer);
}

void vmxnet3_send_packet(const uint8_t *data, uint16_t length) {
    // Example: Transmit a packet using the VMXNET3 device
    vmxnet3_transmit_packet(&vmxnet3_device, data, length);
}

#define VMXNET3_MAC_LO 0x500  // Example offset for low 4 bytes of MAC
#define VMXNET3_MAC_HI 0x504  // Example offset for high 2 bytes of MAC

void vmxnet3_get_mac_address(vmxnet3_device_t *dev, uint8_t *mac) {
    if (!dev || !mac) return;

    // Read MAC address from device registers
    uint32_t mac_low = vmxnet3_read_register(dev, VMXNET3_MAC_LO);
    uint32_t mac_high = vmxnet3_read_register(dev, VMXNET3_MAC_HI);

    // Extract the MAC address bytes
    mac[0] = (mac_low >> 0) & 0xFF;
    mac[1] = (mac_low >> 8) & 0xFF;
    mac[2] = (mac_low >> 16) & 0xFF;
    mac[3] = (mac_low >> 24) & 0xFF;
    mac[4] = (mac_high >> 0) & 0xFF;
    mac[5] = (mac_high >> 8) & 0xFF;

    printf("MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void test_vmxnet3() {


    uint8_t mac[6];
    vmxnet3_get_mac_address(&vmxnet3_device, mac);

    // Print the MAC address
    printf("Retrieved MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // Transmit a test packet
    const uint8_t test_packet[] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // Destination MAC (Broadcast)
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05,  // Source MAC
        0x08, 0x00,                          // EtherType (IPv4)
        0x45, 0x00, 0x00, 0x54,              // Payload
        0x00, 0x01, 0xDE, 0xAD, 0xBE, 0xEF
    };

    vmxnet3_transmit_packet(&vmxnet3_device, test_packet, sizeof(test_packet));
    delay_ms(100); // Allow time for transmission

    // Check for received packets
    vmxnet3_receive_packet(&vmxnet3_device);
}
