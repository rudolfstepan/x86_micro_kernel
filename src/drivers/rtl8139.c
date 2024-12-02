#include "rtl8139.h"
#include "drivers/io/io.h"
#include "memory.h"
#include <stdint.h>
#include "toolchain/stdio.h"
#include "toolchain/stdlib.h"
#include "kernel/sys.h"
#include <stddef.h>


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

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

// RTL8139-spezifische Konstanten
#define RTL8139_VENDOR_ID  0x10EC
#define RTL8139_DEVICE_ID  0x8139

#define REG_ID0 0x00
#define REG_ID4 0x04
#define REG_TRANSMIT_STATUS0 0x10
#define REG_TRANSMIT_ADDR0 0x20
#define REG_RECEIVE_BUFFER 0x30
#define REG_COMMAND 0x37
#define REG_CUR_READ_ADDR 0x38
#define REG_INTERRUPT_MASK 0x3C
#define REG_INTERRUPT_STATUS 0x3E
#define REG_TRANSMIT_CONFIGURATION 0x40
#define REG_RECEIVE_CONFIGURATION 0x44
// Werte für die Register // Kontrollregister

#define CR_RESET (1 << 4)
#define CR_RECEIVER_ENABLE (1 << 3)
#define CR_TRANSMITTER_ENABLE (1 << 2)
#define CR_BUFFER_IS_EMPTY (1 << 0)
// Transmitter-Konfiguration

#define TCR_IFG_STANDARD (3 << 24)
#define TCR_MXDMA_512 (5 << 8)
#define TCR_MXDMA_1024 (6 << 8)
#define TCR_MXDMA_2048 (7 << 8)
// Receiver-Konfiguration

#define RCR_MXDMA_512 (5 << 8)
#define RCR_MXDMA_1024 (6 << 8)
#define RCR_MXDMA_UNLIMITED (7 << 8)
#define RCR_ACCEPT_BROADCAST (1 << 3)
#define RCR_ACCEPT_MULTICAST (1 << 2)
#define RCR_ACCEPT_PHYS_MATCH (1 << 1)
// Interrupt-Statusregister

#define ISR_RECEIVE_BUFFER_OVERFLOW (1 << 4)
#define ISR_TRANSMIT_OK (1 << 2)
#define ISR_RECEIVE_OK (1 << 0)

// Ethernet-Protokolltypen
#define ETHERTYPE_IPV4 0x0800
#define ETHERTYPE_ARP  0x0806

#define MAX_TX_BUFFERS 4
#define TX_BUFFER_SIZE 2048 // Dynamische Größe für jeden TX-Puffer

static uint8_t* tx_buffers[MAX_TX_BUFFERS] = {NULL}; // Dynamische TX-Puffer
static uint8_t current_tx_buffer = 0;               // Aktueller TX-Puffer


// Globale Variablen
volatile uint32_t rtl8139_io_base = 0; // IO-Base-Adresse

#define RX_BUFFER_SIZE (64 * 1024) // 64 KB für den RX-Puffer
#define REG_RECEIVE_BUFFER 0x30    // Offset für RBSTART-Register

uint8_t* rx_buffer = NULL;


// Gültiger Bereich für RTL8139 (32-Bit-Adressraum)
#define MAX_DMA_ADDRESS 0xFFFFFFFF


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

/**
 * Prüft, ob die Adresse im gültigen Bereich für die RTL8139 liegt.
 *
 * @param address Die zu prüfende Adresse.
 * @return 1, wenn die Adresse gültig ist, sonst 0.
 */
int is_address_valid(uintptr_t address) {
    return (address <= MAX_DMA_ADDRESS);
}

/**
 * Überprüft die RX- und TX-Pufferadressen und gibt Fehler aus, wenn sie ungültig sind.
 */
void check_buffer_addresses(void* rx_buffer, uint8_t** tx_buffers, int tx_buffer_count) {
    uintptr_t rx_address = (uintptr_t)rx_buffer;

    // RX-Puffer überprüfen
    if (!is_address_valid(rx_address)) {
        printf("Fehler: RX-Puffer-Adresse (0x%016lX) liegt außerhalb des gültigen Bereichs.\n", rx_address);
    } else {
        printf("RX-Puffer-Adresse ist gültig: 0x%016lX\n", rx_address);
    }

    // TX-Puffer überprüfen
    for (int i = 0; i < tx_buffer_count; ++i) {
        uintptr_t tx_address = (uintptr_t)tx_buffers[i];
        if (!is_address_valid(tx_address)) {
            printf("Fehler: TX-Puffer %d-Adresse (0x%016lX) liegt außerhalb des gültigen Bereichs.\n", i, tx_address);
        } else {
            printf("TX-Puffer %d-Adresse ist gültig: 0x%016lX\n", i, tx_address);
        }
    }
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
    printf("Aktiviere Bus-Mastering für Gerät %u:%u\n", bus, slot);
    uint16_t command = pci_read(bus, slot, PCI_COMMAND, 2);
    if (!(command & PCI_COMMAND_BUS_MASTER)) {
        command |= PCI_COMMAND_BUS_MASTER;
        pci_write(bus, slot, PCI_COMMAND, 2, command);
        printf("++++Bus Mastering aktiviert.++++\n");
    }
}

// Initialisiert den RX-Puffer
// void initialize_rx_buffer() {
//     // Allokiere den RX-Puffer
//     rx_buffer = (uint8_t*)malloc(RX_BUFFER_SIZE);
//     if (!rx_buffer) {
//         printf("Fehler: RX-Puffer konnte nicht allokiert werden.\n");
//         return;
//     }

//     // empty the buffer
//     memset(rx_buffer, 0, RX_BUFFER_SIZE);

//     // RX-Puffer debuggen
//     printf("RX-Puffer initialisiert: Virtuell = %p, Größe = %d Bytes\n", rx_buffer, RX_BUFFER_SIZE);

//     // Schreibe die physische Adresse des RX-Puffers in das RBSTART-Register
//     outl((unsigned short)(rtl8139_io_base + REG_RECEIVE_BUFFER), (uintptr_t)rx_buffer);
// }

#define RX_BUFFER_SIZE (64 * 1024)  // 64 KB
#define REG_RBSTART 0x30           // Register für RX-Puffer-Startadresse

void initialize_rx_buffer() {
    // Allokiere Speicher für den RX-Puffer
    rx_buffer = (uint8_t*)aligned_alloc(4096, RX_BUFFER_SIZE); // 4-KB-Ausrichtung
    if (!rx_buffer) {
        printf("Fehler: RX-Puffer konnte nicht allokiert werden.\n");
        return;
    }

    // Schreibe die physische Adresse des RX-Puffers in das RBSTART-Register
    uintptr_t phys_address = (uintptr_t)rx_buffer;
    if (phys_address > 0xFFFFFFFF) {
        printf("Fehler: RX-Puffer-Adresse liegt außerhalb des 32-Bit-Adressraums.\n");
        free(rx_buffer);
        return;
    }

    outl(rtl8139_io_base + REG_RBSTART, (uint32_t)phys_address);
    printf("RX-Puffer initialisiert: Virtuelle Adresse = %p, Physische Adresse = 0x%08X\n",
           rx_buffer, (uint32_t)phys_address);
}

uint32_t get_io_base(uint8_t bus, uint8_t device, uint8_t function) {
    uint32_t bar0 = pci_read(bus, device, function, 0x10); // BAR0 auslesen

    // Prüfen, ob die Adresse für I/O oder Memory ist
    if (bar0 & 0x01) { // I/O-Bit gesetzt
        return bar0 & ~0x3; // Entferne die unteren Bits für Alignment
    } else {
        printf("Fehler: BAR0 zeigt auf eine Memory-Adresse, keine I/O-Adresse.\n");
        return 0; // Fehler
    }
}

void initialize_tx_buffers() {
    for (int i = 0; i < MAX_TX_BUFFERS; ++i) {
        tx_buffers[i] = (uint8_t*)malloc(TX_BUFFER_SIZE);
        if (!tx_buffers[i]) {
            printf("Fehler: Speicherzuweisung für TX-Puffer %d fehlgeschlagen.\n", i);
        } else {
            printf("TX-Puffer %d initialisiert: %p\n", i, tx_buffers[i]);
        }
    }
}

void free_tx_buffers() {
    for (int i = 0; i < MAX_TX_BUFFERS; ++i) {
        if (tx_buffers[i]) {
            free(tx_buffers[i]);
            tx_buffers[i] = NULL;
            printf("TX-Puffer %d freigegeben.\n", i);
        }
    }
}

// Initialisiert die RTL8139-Netzwerkkarte
void rtl8139_init() {
    printf("Initialisiere RTL8139 Netzwerkkarte...\n");
    printf("PCI-Konfiguration: IO-Base-Adresse = 0x%04X\n", rtl8139_io_base);

    outb((unsigned short)(rtl8139_io_base + 0x37), 0x10); // Reset Command
    while (inb((unsigned short)(rtl8139_io_base + 0x37)) & 0x10); // Warten auf Reset

    initialize_rx_buffer();
    initialize_tx_buffers();

    check_buffer_addresses(rx_buffer, tx_buffers, MAX_TX_BUFFERS);

    outl((unsigned short)(rtl8139_io_base + 0x44), 0xf | (1 << 7)); // Akzeptiere alle Pakete
    outw((unsigned short)(rtl8139_io_base + 0x3C), 0x0005);   // Interrupts aktivieren
    outb((unsigned short)(rtl8139_io_base + 0x37), 0x0C);     // RX und TX aktivieren

    printf("RTL8139 initialisiert.\n");
}

void rtl8139_send_packet(void* data, uint16_t len) {
    if (len > TX_BUFFER_SIZE) {
        printf("Fehler: Paket zu groß (%u Bytes, max %u Bytes).\n", len, TX_BUFFER_SIZE);
        return;
    }

    // Prüfen, ob der aktuelle TX-Puffer frei ist
    uint32_t tsd_status = inl((unsigned short)(rtl8139_io_base + 0x10 + (current_tx_buffer * 4)));
    if (tsd_status & 0x8000) {
        printf("Sendepuffer %u ist noch nicht frei.\n", current_tx_buffer);
        return;
    }

    // Kopiere die Daten in den dynamischen TX-Puffer
    memcpy(tx_buffers[current_tx_buffer], data, len);

    // Schreibe die Adresse des TX-Puffers in das RTL8139-Register
    uint32_t tx_buffer_addr = (uintptr_t)tx_buffers[current_tx_buffer];
    outl((unsigned short)(rtl8139_io_base + 0x20 + (current_tx_buffer * 4)), tx_buffer_addr);

    // Schreibe die Länge des Pakets und starte die Übertragung
    outl((unsigned short)(rtl8139_io_base + 0x10 + (current_tx_buffer * 4)), len);

    // Zum nächsten Puffer wechseln
    current_tx_buffer = (current_tx_buffer + 1) % MAX_TX_BUFFERS;

    printf("Paket mit %u Bytes gesendet über Puffer %u.\n", len, (current_tx_buffer + MAX_TX_BUFFERS - 1) % MAX_TX_BUFFERS);
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
void rtl8139_interrupt_handler() {
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
            // Prüfen, ob das Gerät existiert
            uint32_t id = pci_read(bus, device, 0, 0);
            if ((id & 0xFFFF) == 0xFFFF) { // Kein Gerät vorhanden
                continue;
            }

            // Prüfen, ob das Gerät mehrere Funktionen unterstützt
            uint32_t header_type = pci_read(bus, device, 0, 0x0C) >> 16;
            uint8_t multifunction = (header_type & 0x80) != 0;

            // Über alle Funktionen iterieren
            for (uint8_t function = 0; function < (multifunction ? 8 : 1); ++function) {
                // PCI-Geräte-ID und Vendor-ID auslesen
                id = pci_read(bus, device, function, 0);
                if ((id & 0xFFFF) == RTL8139_VENDOR_ID && ((id >> 16) & 0xFFFF) == RTL8139_DEVICE_ID) {
                    // BAR0 (I/O Base) auslesen
                    uint32_t bar0 = pci_read(bus, device, function, 0x10);
                    rtl8139_io_base = bar0 & ~0x3; // Nur I/O-Bits verwenden

                    // IRQ-Nummer auslesen
                    uint32_t irq_line = pci_read(bus, device, function, 0x3C) & 0xFF;
                    printf("RTL8139 gefunden: Bus %u, Device %u, Funktion %u, IRQ %u\n",
                           bus, device, function, irq_line);

                    // Bus Mastering aktivieren
                    uint16_t command = pci_read(bus, device, function, PCI_COMMAND);
                    command |= PCI_COMMAND_BUS_MASTER;
                    pci_write(bus, device, function, PCI_COMMAND, command);

                    // Interrupt-Handler registrieren
                    register_interrupt_handler(irq_line, rtl8139_interrupt_handler);
                    unmask_irq(irq_line);

                    // MAC-Adresse ausgeben
                    print_mac_address();

                    return 0;
                }
            }
        }
    }
    return -1; // Keine RTL8139-Karte gefunden
}

/**
 * Sendet ein Ethernet-Frame mit Testdaten an eine Ziel-MAC-Adresse.
 *
 * @param dest_mac  Ziel-MAC-Adresse (6 Bytes)
 * @param src_mac   Quell-MAC-Adresse (6 Bytes)
 * @param data      Nutzdaten
 * @param data_len  Länge der Nutzdaten
 */
void send_test_packet(uint8_t* dest_mac, uint8_t* src_mac, const uint8_t* data, uint16_t data_len) {
    if (data_len > (MAX_PACKET_SIZE - sizeof(ethernet_header_t))) {
        printf("Fehler: Nutzdaten zu groß (%u Bytes, max %u Bytes).\n", data_len, MAX_PACKET_SIZE - sizeof(ethernet_header_t));
        return;
    }

    // Allokiere Speicher für das vollständige Ethernet-Frame
    uint8_t packet[MAX_PACKET_SIZE];
    memset(packet, 0, MAX_PACKET_SIZE);

    // Erstelle den Ethernet-Header
    ethernet_header_t* eth_header = (ethernet_header_t*)packet;
    memcpy(eth_header->dest_mac, dest_mac, 6);       // Ziel-MAC-Adresse setzen
    memcpy(eth_header->src_mac, src_mac, 6);        // Quell-MAC-Adresse setzen
    eth_header->ethertype = htons(ETHERTYPE_TEST);  // Ethertype (im Network-Byte-Order)

    // Kopiere die Nutzdaten in das Frame
    memcpy((packet + sizeof(ethernet_header_t)), data, data_len);

    // Berechne die Gesamtlänge des Frames
    uint16_t frame_len = sizeof(ethernet_header_t) + data_len;

    // Sende das Ethernet-Frame
    rtl8139_send_packet(packet, frame_len);

    printf("Test-Paket gesendet: Ziel-MAC %02X:%02X:%02X:%02X:%02X:%02X, Länge %u Bytes.\n",
            dest_mac[0], dest_mac[1], dest_mac[2], dest_mac[3], dest_mac[4], dest_mac[5], frame_len);
}

void test_loopback() {
    char test_packet[64] = "Loopback Test Packet";
    printf("Sende Loopback-Paket: %s\n", test_packet);

    uint8_t mac[6];
    rtl8139_get_mac_address(mac);

    //send_test_packet(mac, mac, (const uint8_t*)test_packet, sizeof(test_packet));

    // Setze Loopback-Modus
    outl((unsigned short)(rtl8139_io_base + 0x40), 0x00060000);

    // Sende das Paket
    rtl8139_send_packet(test_packet, sizeof(test_packet));

    //free_tx_buffers();

    // printf("Loopback-Test fehlgeschlagen: Kein Paket empfangen.\n");
}