#ifndef RTL8139_H
#define RTL8139_H

#include <stdint.h>

#define ETHERTYPE_TEST 0x88B5 // Beispiel-Ethertype für Testdaten
#define MAX_PACKET_SIZE 1518  // Maximale Größe eines Ethernet-Frames

// Struktur für den Ethernet-Header
typedef struct {
    uint8_t dest_mac[6]; // Ziel-MAC-Adresse
    uint8_t src_mac[6];  // Quell-MAC-Adresse
    uint16_t ethertype;  // Ethertype
} __attribute__((packed)) ethernet_header_t;


void initialize_rtl8139(uint8_t bus, uint8_t device, uint8_t function);
uint32_t pci_read(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
void rtl8139_send_packet(void* data, uint16_t len);
void rtl8139_receive_packet();
void test_loopback();
void rtl8139_init();
int find_rtl8139();

#endif  // RTL8139_H