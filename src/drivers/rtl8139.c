#include "rtl8139.h"
#include "drivers/io/io.h"
#include "memory.h"
#include <stdint.h>
#include "toolchain/stdio.h"
#include "toolchain/stdlib.h"
#include "kernel/sys.h"

// Globale Variablen
volatile uint32_t rtl8139_io_base = 0; // IO-Base-Adresse
volatile void* rx_buffer; // Empfangspuffer

// PCI-Konstanten
#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC

// PIC-Konstanten
#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1

// PCI-Befehlsregister-Flags
#define PCI_COMMAND 0x04
#define PCI_COMMAND_BUS_MASTER 0x04

// RTL8139-spezifische Konstanten
#define RTL8139_VENDOR_ID 0x10EC
#define RTL8139_DEVICE_ID 0x8139
#define RTL8139_IO_BASE   0x10

// Ethernet-Protokolltypen
#define ETHERTYPE_IPV4 0x0800
#define ETHERTYPE_ARP  0x0806

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

// Hilfsfunktion: Unmaskiert einen IRQ
void unmask_irq(uint8_t irq) {
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    uint8_t value = inb(port);
    value &= ~(1 << (irq % 8));
    outb(port, value);
}

// Liest aus dem PCI-Konfigurationsraum
uint32_t pci_read(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t address = (1 << 31) |
                       ((uint32_t)bus << 16) |
                       ((uint32_t)device << 11) |
                       ((uint32_t)function << 8) |
                       (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

// Schreibt in den PCI-Konfigurationsraum
void pci_write(uint8_t bus, uint8_t slot, uint8_t offset, uint8_t size, uint32_t value) {
    uint32_t address = (1U << 31) |
                       ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) |
                       ((uint32_t)(offset & 0xFC));
    outl(PCI_CONFIG_ADDRESS, address);
    switch (size) {
        case 1: outb(PCI_CONFIG_DATA + (offset & 3), (uint8_t)value); break;
        case 2: outw(PCI_CONFIG_DATA + (offset & 2), (uint16_t)value); break;
        case 4: outl(PCI_CONFIG_DATA, value); break;
        default: printf("Fehler: Ungültige Schreibgröße (%d)\n", size); break;
    }
}

// Aktiviert Bus-Mastering
void enable_bus_master(uint8_t bus, uint8_t slot) {
    uint16_t command = pci_read(bus, slot, PCI_COMMAND, 2);
    if (!(command & PCI_COMMAND_BUS_MASTER)) {
        command |= PCI_COMMAND_BUS_MASTER;
        pci_write(bus, slot, PCI_COMMAND, 2, command);
        printf("Bus Mastering aktiviert.\n");
    }
}

// Initialisiert den RX-Puffer
void initialize_rx_buffer() {
    rx_buffer = (void*)malloc((8192*8)+16+1500);
    if (!rx_buffer) {
        printf("Fehler: RX-Puffer konnte nicht allokiert werden.\n");
        return;
    }

    // lösche den Puffer
    memset(rx_buffer, 0, (8192*8)+16+1500);

    // Schreibe die physische Adresse des RX-Puffers in RBSTART
    outl((unsigned short)(rtl8139_io_base + 0x30), rx_buffer);
    printf("RX-Puffer initialisiert: Virtuell = %p, Physisch = 0x%08X\n", rx_buffer, (uint32_t)rx_buffer);
}

// Initialisiert die RTL8139-Netzwerkkarte
void rtl8139_init() {
    printf("Initialisiere RTL8139 Netzwerkkarte...\n");
    outb((unsigned short)(rtl8139_io_base + 0x37), 0x10); // Reset Command
    while (inb((unsigned short)(rtl8139_io_base + 0x37)) & 0x10); // Warten auf Reset
    initialize_rx_buffer();
    outl((unsigned short)(rtl8139_io_base + 0x44), 0xf | (1 << 7)); // Akzeptiere alle Pakete
    outw((unsigned short)(rtl8139_io_base + 0x3C), 0x0005);   // Interrupts aktivieren
    outb((unsigned short)(rtl8139_io_base + 0x37), 0x0C);     // RX und TX aktivieren

    printf("RTL8139 initialisiert.\n");
}

// Sendet ein Ethernet-Paket
void rtl8139_send_packet(void* data, uint16_t len) {
    static uint8_t current_tx_buffer = 0;
    if (len > 1500) {
        printf("Fehler: Paket zu groß (%u Bytes).\n", len);
        return;
    }
    uint32_t tsd_status = inl((unsigned short)(rtl8139_io_base + 0x10 + (current_tx_buffer * 4)));
    if (tsd_status & 0x8000) {
        printf("Sendepuffer %u ist noch nicht frei.\n", current_tx_buffer);
        return;
    }
    memcpy((void*)(rtl8139_io_base + 0x20 + (current_tx_buffer * 4)), data, len);
    outl((unsigned short)(rtl8139_io_base + 0x10 + (current_tx_buffer * 4)), len);
    current_tx_buffer = (current_tx_buffer + 1) % 4;
    printf("Paket gesendet über Puffer %u.\n", (current_tx_buffer + 3) % 4);
}

// Empfängt ein Ethernet-Paket
void rtl8139_receive_packet() {
    static uint16_t rx_offset = 0;

    while (!(inb((unsigned short)(rtl8139_io_base + 0x37)) & 0x01)) {
        uint16_t status = *(volatile uint16_t*)(rx_buffer + rx_offset);
        uint16_t length = *(volatile uint16_t*)(rx_buffer + rx_offset + 2);
        if (!(status & 0x01)) {
            printf("Ungültiges Paket. Status: 0x%04X\n", status);
            break;
        }
        uint8_t* packet = rx_buffer + rx_offset + 4;
        handle_ethernet_frame(packet, length);
        rx_offset = (rx_offset + length + 4 + 3) & ~3;
        outw((unsigned short)(rtl8139_io_base + 0x38), rx_offset - 16);
    }

    hex_dump(rx_buffer, 64);
}

// Interrupt-Handler
void rtl8139_interrupt_handler(uint8_t isr, uint64_t error, uint64_t irq) {

    printf("RTL8139 Interrupt: ISR = 0x%02X, Error = 0x%X, IRQ = %u\n", isr, error, irq);

    uint16_t status = inw(rtl8139_io_base + 0x3e);
	outw(rtl8139_io_base + 0x3E, 0x05);
	if(status & 0x04) {
		// Sent
        printf("TX OK: Paket gesendet.\n");
	}
	if (status & 0x01) {
		// Received
		rtl8139_receive_packet();
	}
}

void rtl8139_get_mac_address(uint8_t* mac) {
    for (int i = 0; i < 6; i++) {
        mac[i] = inb((unsigned short)(rtl8139_io_base + i));
    }
}

void print_mac_address() {
    uint8_t mac[6];
    rtl8139_get_mac_address(mac);

    printf("MAC-Adresse: %02X:%02X:%02X:%02X:%02X:%02X\n",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// Findet die RTL8139-Karte im PCI-Bus
int find_rtl8139() {
    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t device = 0; device < 32; ++device) {
            uint32_t id = pci_read(bus, device, 0, 0);
            if ((id & 0xFFFF) == RTL8139_VENDOR_ID && ((id >> 16) & 0xFFFF) == RTL8139_DEVICE_ID) {
                uint32_t bar0 = pci_read(bus, device, 0, RTL8139_IO_BASE);
                rtl8139_io_base = (volatile uintptr_t)(bar0 & ~0x3);

                // IRQ-Nummer auslesen
                uint32_t irq_pin = pci_read(bus, device, 0, 0x3D) >> 8;
                uint32_t irq_line = pci_read(bus, device, 0, 0x3D) & 0xFF;
                printf("RTL8139 gefunden: Bus %u, Gerät %u, IRQ %u\n", bus, device, irq_line);

                // IRQ-Nummer einstellen
                // outb((unsigned short)(PIC1_DATA), 0xFF & ~(1 << irq_line));
                // outb((unsigned short)(PIC2_DATA), 0xFF);
                enable_bus_master(bus, device);

                // register interrupt handler
                register_interrupt_handler(irq_line, rtl8139_interrupt_handler);

                print_mac_address();

                return 0;
            }
        }
    }
    return -1;
}


void test_loopback() {

    char test_packet[64] = "Loopback Test Packet";
    printf("Sende Loopback-Paket: %s\n", test_packet);

    // Setze Loopback-Modus
    outl((unsigned short)(rtl8139_io_base + 0x40), 0x00060000);

    // Sende das Paket
    rtl8139_send_packet(test_packet, sizeof(test_packet));

    // printf("Loopback-Test fehlgeschlagen: Kein Paket empfangen.\n");
}