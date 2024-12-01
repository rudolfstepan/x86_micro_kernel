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

// Initialisiert die Realtek RTL8139 Netzwerkkarte
void rtl8139_init() {
    // Netzwerkkarte zurücksetzen
    outb((unsigned short)(rtl8139_io_base + RTL8139_CMD), 0x10); // Reset Command
    while (inb((unsigned short)(rtl8139_io_base + RTL8139_CMD)) & 0x10); // Warten auf Reset

    // Initialisiere den Empfangspuffer
    static uint8_t rx_buffer[8192 + 16 + 1500]; // RX-Puffer
    outl((unsigned short)(rtl8139_io_base + RTL8139_RBSTART), (uint32_t)rx_buffer);

    // Konfiguration: Empfange alle Pakete
    outl((unsigned short)(rtl8139_io_base + RTL8139_RCR), 0x0000000F);

    // Interrupts aktivieren
    outw((unsigned short)(rtl8139_io_base + RTL8139_IMR), 0x0005); // RX und TX OK Interrupts

    // RX und TX aktivieren
    outb((unsigned short)(rtl8139_io_base + RTL8139_CMD), 0x0C); // RX und TX aktivieren
}

// Sendet ein Ethernet-Paket
void rtl8139_send_packet(void* data, uint16_t len) {
    // Prüfe Status des Sendepuffers
    uint32_t tsd0_status = inl((unsigned short)(rtl8139_io_base + 0x10)); // TSD0 Offset
    if (tsd0_status & 0x8000) { // Prüfe "Owner"-Bit (0x8000)
        printf("Sendepuffer TSD0 ist noch nicht frei. Status: 0x%08X\n", tsd0_status);
        return;
    }

    // Schreibe das Paket in den Sendepuffer
    memcpy((void*)(rtl8139_io_base + 0x20), data, len); // TSAD0 Offset

    // Schreibe die Länge des Pakets und starte die Übertragung
    outl((unsigned short)(rtl8139_io_base + 0x10), len); // TSD0 Offset
}

// Interrupt-Handler für die Netzwerkkarte
void rtl8139_handle_interrupt() {
    uint16_t isr = inw(rtl8139_io_base + RTL8139_ISR);
    if (isr & 0x01) { // Paket empfangen
        // Empfangenes Paket verarbeiten
        printf("Paket empfangen!\n");
    }
    if (isr & 0x04) { // Paket gesendet
        printf("Paket gesendet!\n");
    }
    outw(rtl8139_io_base + RTL8139_ISR, isr); // Interrupt zurücksetzen
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
    static uint8_t rx_buffer[8192 + 16 + 1500]; // RX-Puffer
    static uint16_t rx_offset = 0;

    while (!(inb((unsigned short)(rtl8139_io_base + 0x37)) & 0x01)) { // RX leer?
        // Lese Paketlänge und Status aus dem RX-Puffer
        uint16_t status = *(uint16_t*)(rx_buffer + rx_offset);
        uint16_t length = *(uint16_t*)(rx_buffer + rx_offset + 2);

        // Verarbeite das empfangene Paket
        uint8_t* packet = (uint8_t*)(rx_buffer + rx_offset + 4);
        handle_ethernet_frame(packet, length);

        // Aktualisiere den Offset
        rx_offset = (rx_offset + length + 4 + 3) & ~3; // 4-byte alignment
        outw((unsigned short)(rtl8139_io_base + 0x38), rx_offset - 16);
    }
}

void rtl8139_interrupt_handler() {
    uint16_t isr = inw((unsigned short)(rtl8139_io_base + RTL8139_ISR)); // ISR auslesen

    if (isr & 0x01) { // RX OK
        rtl8139_receive_packet();
    }
    if (isr & 0x04) { // TX OK
        printf("Paket wurde erfolgreich gesendet.\n");
    }

    // Interrupt-Status zurücksetzen
    outw((unsigned short)(rtl8139_io_base + RTL8139_ISR), isr);
}

uint8_t get_rtl8139_irq(uint8_t bus, uint8_t device, uint8_t function) {
    uint32_t pci_config = pci_read(bus, device, function, 0x3C);
    return pci_config & 0xFF; // IRQ befindet sich im unteren Byte
}

void setup_rtl8139_interrupt(uint8_t irq) {
    // IRQ auf den passenden IDT-Eintrag mappen
    uint8_t vector = 0x20 + irq; // IRQs beginnen ab Vektor 0x20 (PIC)

    printf("Netzwerkkarte: IRQ %u -> Vektor %u\n", irq, vector);

    // Registriere den Handler in der IDT
    register_interrupt_handler(vector, rtl8139_interrupt_handler);

    // Unmaske den IRQ (PIC)
    unmask_irq(irq);
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
    setup_rtl8139_interrupt(irq);

    // Initialisiere die Netzwerkkarte
    rtl8139_init();
}

void test_loopback() {
    printf("Sende Loopback-Paket...\n");
    char packet[64] = "Test Loopback Packet";
    rtl8139_send_packet(packet, sizeof(packet));

    uint16_t timeout = 1000; // Timeout
    while (timeout--) {
        uint16_t isr = inw((unsigned short)(rtl8139_io_base + 0x3E));
        if (isr & 0x01) { // RX OK
            printf("Loopback-Paket empfangen.\n");
            rtl8139_receive_packet();
            return;
        }
    }

    printf("Loopback-Test fehlgeschlagen: Kein Paket empfangen.\n");
}
