#ifndef RTL8139_H
#define RTL8139_H

#include <stdint.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

#define RTL8139_VENDOR_ID  0x10EC
#define RTL8139_DEVICE_ID  0x8139

#define RTL8139_IO_BASE 0x10 // IO Base Address Register offset
#define RTL8139_CMD     0x37 // Command Register
#define RTL8139_RCR     0x44 // Receive Configuration Register
#define RTL8139_ISR     0x3E // Interrupt Status Register
#define RTL8139_IMR     0x3C // Interrupt Mask Register
#define RTL8139_RBSTART 0x30 // Receive Buffer Start Address
#define RTL8139_TSAD0   0x20 // Transmit Start Address of descriptor 0
#define RTL8139_TSD0    0x10 // Transmit Status of descriptor 0


// Ethernet Frame Header Struktur
typedef struct {
    uint8_t dest_mac[6];     // Ziel-MAC-Adresse
    uint8_t src_mac[6];      // Quell-MAC-Adresse
    uint16_t ethertype;      // Protokolltyp (z. B. IPv4, ARP)
} ethernet_header_t;



void initialize_rtl8139(uint8_t bus, uint8_t device, uint8_t function);
uint32_t pci_read(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
void rtl8139_send_packet(void* data, uint16_t len);
void rtl8139_receive_packet();
void test_loopback();
void rtl8139_init();
int find_rtl8139();

#endif  // RTL8139_H