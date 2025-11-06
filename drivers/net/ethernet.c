#include "ethernet.h"

#include "lib/libc/stdio.h"
#include "lib/libc/stdlib.h"


/**
 * Wandelt einen 16-Bit-Wert von Host Byte Order (Little-Endian) 
 * in Network Byte Order (Big-Endian) um.
 *
 * @param hostshort Der 16-Bit-Wert im Host Byte Order.
 * @return Der umgewandelte Wert im Network Byte Order.
 */
uint16_t htons(uint16_t hostshort) {
    return (hostshort >> 8) | (hostshort << 8);
}

void handle_ethernet_frame(const uint8_t* frame, uint16_t length) {
    if (length < sizeof(ethernet_header_t)) {
        printf("Fehler: Frame zu klein (%u Bytes).\n", length);
        return;
    }

    // Ethernet-Header extrahieren
    const ethernet_header_t* header = (const ethernet_header_t*)frame;

    // Konvertiere Ethertype aus Network Byte Order
    uint16_t ethertype = (header->ethertype << 8) | (header->ethertype >> 8);

    // Ausgabe der MAC-Adressen
    printf("Ethernet Frame empfangen:\n");
    printf("  Ziel-MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
           header->dest_mac[0], header->dest_mac[1], header->dest_mac[2],
           header->dest_mac[3], header->dest_mac[4], header->dest_mac[5]);
    printf("  Quell-MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
           header->src_mac[0], header->src_mac[1], header->src_mac[2],
           header->src_mac[3], header->src_mac[4], header->src_mac[5]);
    printf("  Ethertype: 0x%04X\n", ethertype);

    // Protokolltyp behandeln
    switch (ethertype) {
        case ETHERTYPE_IPV4:
            printf("  IPv4-Paket erkannt. Übergabe an den IPv4-Stack...\n");
            // IPv4-Frame an den Netzwerkstack übergeben (falls vorhanden)
            // handle_ipv4(frame + sizeof(ethernet_header_t), length - sizeof(ethernet_header_t));
            break;

        case ETHERTYPE_ARP:
            printf("  ARP-Paket erkannt. Verarbeitung des ARP-Frames...\n");
            // ARP-Frame verarbeiten (falls ARP implementiert ist)
            // handle_arp(frame + sizeof(ethernet_header_t), length - sizeof(ethernet_header_t));
            break;

        default:
            printf("  Unbekannter Protokolltyp: 0x%04X. Frame wird ignoriert.\n", ethertype);
            break;
    }
}