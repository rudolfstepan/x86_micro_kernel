#ifndef ETHERNET_H
#define ETHERNET_H


#define ETHERTYPE_TEST 0x88B5 // Beispiel-Ethertype für Testdaten
#define MAX_PACKET_SIZE 1518  // Maximale Größe eines Ethernet-Frames

// Ethernet-Protokolltypen
#define ETHERTYPE_IPV4 0x0800
#define ETHERTYPE_ARP  0x0806

#include <stdint.h>


// Struktur für den Ethernet-Header
typedef struct {
    uint8_t dest_mac[6]; // Ziel-MAC-Adresse
    uint8_t src_mac[6];  // Quell-MAC-Adresse
    uint16_t ethertype;  // Ethertype
} __attribute__((packed)) ethernet_header_t;


uint16_t htons(uint16_t hostshort);
void handle_ethernet_frame(const uint8_t* frame, uint16_t length);


#endif // ETHERNET_H