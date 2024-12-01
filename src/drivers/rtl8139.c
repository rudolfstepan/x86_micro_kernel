#include "rtl8139.h"

#include "drivers/io/io.h"
#include "memory.h"
#include <stdint.h>
#include "toolchain/stdio.h"
#include "kernel/sys.h"


volatile uint8_t* rtl8139_io_base = NULL;

#define PCI_CONFIG_ADDRESS 0xCF8 // Port für PCI Konfigurationsadresse
#define PCI_CONFIG_DATA    0xCFC // Port für PCI Konfigurationsdaten

#define PIC1_COMMAND 0x20  // Master PIC Command Port
#define PIC1_DATA    0x21  // Master PIC Data Port
#define PIC2_COMMAND 0xA0  // Slave PIC Command Port
#define PIC2_DATA    0xA1  // Slave PIC Data Port

#define PCI_COMMAND 0x04
#define PCI_COMMAND_BUS_MASTER 0x04


void unmask_irq(uint8_t irq) {
    uint16_t port;
    uint8_t value;

    if (irq < 8) {
        // IRQ gehört zum Master PIC
        port = PIC1_DATA;
    } else {
        // IRQ gehört zum Slave PIC
        irq -= 8;
        port = PIC2_DATA;
    }

    // Lese die aktuelle IRQ-Maske
    value = inb(port);

    // Unmaskiere das spezifische IRQ-Bit (auf 0 setzen)
    value &= ~(1 << irq);

    // Schreibe die neue Maske zurück
    outb(port, value);
}

// Liest ein 32-Bit-Wert aus dem PCI-Konfigurationsraum
uint32_t pci_read(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    // Erstelle die Adresse gemäß dem PCI-Konfigurationsprotokoll
    uint32_t address = (1 << 31)              // Aktiviert das Configuration Space Access
                     | ((uint32_t)bus << 16)  // Busnummer (8 Bit)
                     | ((uint32_t)device << 11) // Gerät (5 Bit)
                     | ((uint32_t)function << 8) // Funktion (3 Bit)
                     | (offset & 0xFC);        // Register-Offset (word-aligned)

    // Schreibe die Adresse in den PCI_CONFIG_ADDRESS-Port
    outl(PCI_CONFIG_ADDRESS, address);

    // Lese die Daten aus dem PCI_CONFIG_DATA-Port
    return inl(PCI_CONFIG_DATA);
}

// PCI-Configuration Space Register Offsets
#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC


/**
 * Schreibt Daten in den PCI-Configuration Space.
 *
 * @param bus     Der PCI-Bus (0-255)
 * @param slot    Der PCI-Geräteslot (0-31)
 * @param offset  Der Offset im PCI-Configuration Space (0-255)
 * @param size    Die Größe der Daten (1, 2 oder 4 Bytes)
 * @param value   Der zu schreibende Wert
 */
void pci_write(uint8_t bus, uint8_t slot, uint8_t offset, uint8_t size, uint32_t value) {
    // 1. Erzeuge die Konfigurationsadresse
    uint32_t address = (1U << 31) | // Aktiviert das Zugriffsfeld
                       ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) |
                       ((uint32_t)(offset & 0xFC)); // 32-Bit alignment (letzte 2 Bits = 0)

    // 2. Schreibe die Adresse in das PCI_CONFIG_ADDRESS-Register
    outl(PCI_CONFIG_ADDRESS, address);

    // 3. Schreibe die Daten in das PCI_CONFIG_DATA-Register
    switch (size) {
        case 1: // Schreibe 1 Byte
            outb(PCI_CONFIG_DATA + (offset & 3), (uint8_t)value);
            break;
        case 2: // Schreibe 2 Bytes
            outw(PCI_CONFIG_DATA + (offset & 2), (uint16_t)value);
            break;
        case 4: // Schreibe 4 Bytes
            outl(PCI_CONFIG_DATA, value);
            break;
        default:
            printf("Fehler: Ungültige Schreibgröße (%d)\n", size);
            break;
    }
}

void enable_bus_master(uint8_t bus, uint8_t slot) {
    uint16_t command = pci_read(bus, slot, PCI_COMMAND, 2);
    if (!(command & PCI_COMMAND_BUS_MASTER)) {
        command |= PCI_COMMAND_BUS_MASTER;
        pci_write(bus, slot, PCI_COMMAND, 2, command);
        printf("Bus Mastering aktiviert.\n");
    }
}

uint8_t* rx_buffer = NULL; // Pointer für den RX-Puffer

void initialize_rx_buffer() {
    // Dynamisch Speicher für den RX-Puffer allozieren
    rx_buffer = (uint8_t*)malloc(8192 + 16);
    if (!rx_buffer) {
        printf("Fehler: RX-Puffer konnte nicht allokiert werden.\n");
        return;
    }

    // RX-Puffer-Adresse in das RBSTART-Register schreiben
    outl((unsigned short)(rtl8139_io_base + 0x30), (uint32_t)rx_buffer);

    printf("RX-Puffer initialisiert: %p\n", rx_buffer);
}

// Initialisiert die Realtek RTL8139 Netzwerkkarte
void rtl8139_init() {
    printf("Initialisiere RTL8139 Netzwerkkarte...\n");
    // 1. Netzwerkkarte zurücksetzen
    outb((unsigned short)(rtl8139_io_base + 0x37), 0x10); // Reset Command
    while (inb((unsigned short)(rtl8139_io_base + 0x37)) & 0x10); // Warten auf Reset

    // 2. Empfangspuffer initialisieren
    initialize_rx_buffer();

    // 3. Empfangskonfiguration setzen
    outl((unsigned short)(rtl8139_io_base + 0x44), 0x00000F); // Alle Pakete akzeptieren

    // 4. Interrupts aktivieren
    outw((unsigned short)(rtl8139_io_base + 0x3C), 0x0005); // RX und TX OK Interrupts

    // 5. Sende- und Empfangsmodule aktivieren
    outb((unsigned short)(rtl8139_io_base + 0x37), 0x0C); // RX und TX aktivieren

    printf("RTL8139 initialisiert. RX-Puffer bei %p\n", rx_buffer);
}

// Sendet ein Ethernet-Paket
void rtl8139_send_packet(void* data, uint16_t len) {
    static uint8_t current_tx_buffer = 0; // Index des aktuellen Sendepuffers

    if (len > 1500) { // Maximale Ethernet-Paketgröße
        printf("Fehler: Paket zu groß (%u Bytes).\n", len);
        return;
    }

    // 1. Prüfen, ob der aktuelle Sendepuffer frei ist
    uint32_t tsd_status = inl((unsigned short)(rtl8139_io_base + 0x10 + (current_tx_buffer * 4)));
    if (tsd_status & 0x8000) { // Prüfe "Owner"-Bit
        printf("Sendepuffer %u ist noch nicht frei.\n", current_tx_buffer);
        return;
    }

    // 2. Paket in den Sendepuffer schreiben
    memcpy((void*)(rtl8139_io_base + 0x20 + (current_tx_buffer * 4)), data, len);

    // 3. Länge des Pakets schreiben und Übertragung starten
    outl((unsigned short)(rtl8139_io_base + 0x10 + (current_tx_buffer * 4)), len);

    // 4. Nächsten Puffer auswählen
    current_tx_buffer = (current_tx_buffer + 1) % 4;

    printf("Paket mit %u Bytes gesendet über Puffer %u.\n", len, (current_tx_buffer + 3) % 4);
}

// Sucht die Netzwerkkarte im PCI-Bus
int find_rtl8139() {
    for (uint8_t bus = 0; bus < 256; ++bus) {
        for (uint8_t device = 0; device < 32; ++device) {
            uint32_t id = pci_read(bus, device, 0, 0);
            if ((id & 0xFFFF) == RTL8139_VENDOR_ID && ((id >> 16) & 0xFFFF) == RTL8139_DEVICE_ID) {
                uint32_t bar0 = pci_read(bus, device, 0, RTL8139_IO_BASE);
                rtl8139_io_base = (volatile uint8_t*)(bar0 & ~0x3); // IO-Base-Adresse
                return 0;
            }
        }
    }
    return -1;
}

void handle_ethernet_frame(const uint8_t* frame, uint16_t length) {
    printf("Ethernet Frame empfangen.\n");

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

void rtl8139_receive_packet() {
    printf("Empfange Paket...\n");

    static uint8_t rx_buffer[8192 + 16]; // Empfangspuffer
    static uint16_t rx_offset = 0; // Lese-Offset im RX-Puffer

    // RX leer prüfen
    while (!(inb((unsigned short)(rtl8139_io_base + 0x37)) & 0x01)) {
        // Status und Länge des Pakets lesen
        uint16_t status = *(volatile uint16_t*)(rx_buffer + rx_offset);
        uint16_t length = *(volatile uint16_t*)(rx_buffer + rx_offset + 2);

        // Debug-Ausgaben
        printf("Status: 0x%04X, Länge: %u\n", status, length);

        // Prüfen, ob das Paket gültig ist
        if (!(status & 0x01)) { // Prüfe OK-Bit
            printf("Ungültiges Paket empfangen. Status: 0x%04X\n", status);
            break;
        }

        // RX-Puffer debuggen
        printf("RX-Puffer-Daten:\n");
        for (int i = 0; i < 64; i++) {
            printf("%02X ", rx_buffer[rx_offset + i]);
            if ((i + 1) % 16 == 0) printf("\n");
        }

        // Paketdaten auslesen
        uint8_t* packet = (uint8_t*)(rx_buffer + rx_offset + 4);

        // Verarbeite das Paket
        handle_ethernet_frame(packet, length);

        // Lese-Offset aktualisieren
        rx_offset = (rx_offset + length + 4 + 3) & ~3; // 4-Byte-Alignment
        outw((unsigned short)(rtl8139_io_base + 0x38), rx_offset - 16); // CAPR setzen

        printf("Neuer RX-Offset: %u\n", rx_offset);
    }
}

// Interrupt-Handler für die Netzwerkkarte
void rtl8139_interrupt_handler() {
    printf("RTL8139 Interrupt!\n");
    uint16_t isr = inw((unsigned short)(rtl8139_io_base + 0x3E)); // ISR auslesen

    if (isr & 0x01) { // RX OK
        printf("RX OK: Paket empfangen.\n");
        rtl8139_receive_packet();
    }

    if (isr & 0x04) { // TX OK
        printf("TX OK: Paket wurde erfolgreich gesendet.\n");
    }

    // ISR zurücksetzen
    outw((unsigned short)(rtl8139_io_base + 0x3E), isr);
}

uint8_t get_rtl8139_irq(uint8_t bus, uint8_t device, uint8_t function) {
    uint32_t pci_config = pci_read(bus, device, function, 0x3C);
    return pci_config & 0xFF; // IRQ befindet sich im unteren Byte
}

void initialize_rtl8139(uint8_t bus, uint8_t device, uint8_t function) {
    // Hole die IO-Base-Adresse der Netzwerkkarte
    uint32_t bar0 = pci_read(bus, device, function, RTL8139_IO_BASE);
    rtl8139_io_base = (volatile uint8_t*)(bar0 & ~0x3); // IO-Base-Adresse

    // Hole den IRQ der Netzwerkkarte
    uint8_t irq = get_rtl8139_irq(bus, device, function);

    printf("RTL8139 Netzwerkkarte gefunden: Bus %u, Gerät %u, Funktion %u, IRQ %u\n",
           bus, device, function, irq);

    // Registriere den Interrupt-Handler
    register_interrupt_handler(irq, rtl8139_interrupt_handler);

    // Initialisiere die Netzwerkkarte
    rtl8139_init();

    enable_bus_master(bus, device); // Bus Mastering aktivieren
}

void test_loopback() {

    rtl8139_init();

    char test_packet[64] = "Loopback Test Packet";
    printf("Sende Loopback-Paket: %s\n", test_packet);

    // Setze Loopback-Modus
    outl((unsigned short)(rtl8139_io_base + 0x40), 0x00060000);

    // Sende das Paket
    rtl8139_send_packet(test_packet, sizeof(test_packet));

    // Warte auf Empfang
    // read buffer
    hex_dump(rx_buffer, 64);
    

    // printf("Loopback-Test fehlgeschlagen: Kein Paket empfangen.\n");
}
