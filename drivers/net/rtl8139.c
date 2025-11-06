#include "rtl8139.h"
#include "drivers/char/io.h"
#include "mm/kmalloc.h"
#include <stdint.h>
#include "lib/libc/stdio.h"
#include "lib/libc/stdlib.h"
#include "arch/x86/sys.h"
#include <stddef.h>
#include "drivers/bus/pci.h"
#include "drivers/net/ethernet.h"

#define CR_WRITABLE_MASK (CR_RECEIVER_ENABLE | CR_TRANSMITTER_ENABLE)

// RTL8139-spezifische Konstanten
#define RTL8139_VENDOR_ID  0x10EC
#define RTL8139_DEVICE_ID  0x8139

#define REG_ID0 0x00
#define REG_ID4 0x04
#define REG_TRANSMIT_STATUS0 0x10
#define REG_TRANSMIT_ADDR0 0x20
#define REG_RECEIVE_BUFFER 0x30
#define REG_COMMAND 0x37
#define CR_RECEIVER_ENABLE (1 << 3)
#define CR_TRANSMITTER_ENABLE (1 << 2)
#define CR_WRITABLE_MASK (CR_RECEIVER_ENABLE | CR_TRANSMITTER_ENABLE)
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



#define MAX_TX_BUFFERS 4
#define TX_BUFFER_SIZE 2048 // Dynamische Größe für jeden TX-Puffer

#define RX_BUFFER_SIZE (64 * 1024) // 64 KB für den RX-Puffer
#define REG_RECEIVE_BUFFER 0x30    // Offset für RBSTART-Register

// Gültiger Bereich für RTL8139 (32-Bit-Adressraum)
#define MAX_DMA_ADDRESS 0xFFFFFFFF

typedef struct {
    uint32_t tx_buffers[TX_BUFFER_SIZE];
    uint32_t rx_buffers[RX_BUFFER_SIZE];
    volatile uint32_t *mmio_base;     // MMIO base address
    uint32_t irq;                     // IRQ number
    uint32_t tx_producer;             // TX producer index
    uint32_t rx_producer;             // RX producer index
} rtl8139_device_t;

rtl8139_device_t rtl8139_device = {0};

// static uint8_t* tx_buffers[MAX_TX_BUFFERS] = {NULL}; // Dynamische TX-Puffer
// static uint8_t current_tx_buffer = 0;               // Aktueller TX-Puffer

// // Globale Variablen
// uint8_t* rx_buffer = NULL;

void write_and_verify_register(uint32_t base, uint32_t offset, uint32_t value) {
    // Write the value to the register
    outl(base + offset, value);

    // Read the value back
    uint32_t read_value = inl(base + offset);

    // Compare written and read values
    if (read_value != value) {
        printf("(!)Register write mismatch @ 0x%X. Written: 0x%08X, Read: 0x%08X\n",
               offset, value, read_value);
    }
}

void write_and_verify_register_b(uint32_t base, uint8_t offset, uint8_t value) {
    outb(base + offset, value);

    uint8_t read_value = inb(base + offset);

    if ((read_value & CR_WRITABLE_MASK) != (value & CR_WRITABLE_MASK)) {
        printf("Warning: Command register mismatch. Expected: 0x%02X, Actual: 0x%02X\n",
               value & CR_WRITABLE_MASK, read_value & CR_WRITABLE_MASK);
    }
}

void write_and_verify_register_w(uint32_t base, uint32_t offset, uint32_t value) {
    // Write the value to the register
    outw(base + offset, value);

    // Read the value back
    uint32_t read_value = inw(base + offset);

    // Compare written and read values
    if (read_value != value) {
        printf("Warning: Register write mismatch at offset 0x%X. Written: 0x%08X, Read: 0x%08X\n",
               offset, value, read_value);
    }
}

void enable_rx_tx(uint32_t base) {
    uint8_t command_value = CR_RECEIVER_ENABLE | CR_TRANSMITTER_ENABLE;
    write_and_verify_register_b(base, REG_COMMAND, command_value & CR_WRITABLE_MASK);
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
        printf("Fehler: RX-Puffer-Adresse (0x%016lX) liegt außerhalb des erlaubten Bereichs.\n", rx_address);
    } 

    // TX-Puffer überprüfen
    for (int i = 0; i < tx_buffer_count; ++i) {
        uintptr_t tx_address = (uintptr_t)tx_buffers[i];
        if (!is_address_valid(tx_address)) {
            printf("Fehler: TX-Puffer %d-Adresse (0x%016lX) liegt außerhalb des erlaubten Bereichs.\n", i, tx_address);
        }
    }
}

// Hilfsfunktion: Unmaskiert einen IRQ
// void unmask_irq(uint8_t irq) {
//     uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
//     uint8_t value = inb(port);
//     value &= ~(1 << (irq % 8));
//     outb(port, value);
// }

void initialize_rx_buffer() {
    // Allokiere Speicher für den RX-Puffer
    // rtl8139_device.rx_buffers = (uint8_t*)aligned_alloc(4096, RX_BUFFER_SIZE); // 4-KB-Ausrichtung
    // if (!rx_buffer) {
    //     printf("Fehler: RX-Puffer konnte nicht allokiert werden.\n");
    //     return;
    // }

    // // RX-Puffer mit Nullen initialisieren
    // memset(rx_buffer, 0, RX_BUFFER_SIZE);

    // Schreibe die physische Adresse des RX-Puffers in das RBSTART-Register
    uintptr_t phys_address = (uintptr_t)rtl8139_device.rx_buffers;
    if (phys_address > 0xFFFFFFFF) {
        printf("Fehler: RX-Puffer-Adresse liegt außerhalb des 32-Bit-Adressraums.\n");
        free(rtl8139_device.rx_buffers);
        return;
    }

    write_and_verify_register(rtl8139_device.mmio_base, REG_RECEIVE_BUFFER, (uint32_t)phys_address);

    //printf("RX-Puffer initialisiert: Virtuelle Adresse = %p, Physische Adresse = 0x%08X\n", rx_buffer, (uint32_t)phys_address);
}

void initialize_tx_buffers() {
    // for (int i = 0; i < MAX_TX_BUFFERS; ++i) {
    //     tx_buffers[i] = (uint8_t*)malloc(TX_BUFFER_SIZE);
    //     if (!tx_buffers[i]) {
    //         printf("Fehler: Speicherzuweisung für TX-Puffer %d fehlgeschlagen.\n", i);
    //     }
    // }
}

void free_tx_buffers() {
    // for (int i = 0; i < MAX_TX_BUFFERS; ++i) {
    //     if (tx_buffers[i]) {
    //         free(tx_buffers[i]);
    //         tx_buffers[i] = NULL;
    //         printf("TX-Puffer %d freigegeben.\n", i);
    //     }
    // }
}

// Initialisiert die RTL8139-Netzwerkkarte
void rtl8139_init(rtl8139_device_t *device) {
    // printf("Initialisiere RTL8139 Netzwerkkarte...\n");
    // printf("PCI-Konfiguration: IO-Base-Adresse = 0x%04X\n", rtl8139_device.mmio_base);

    outb(rtl8139_device.mmio_base + REG_COMMAND, CR_RESET);
    while (inb(rtl8139_device.mmio_base + REG_COMMAND) & CR_RESET);

    initialize_rx_buffer();
    initialize_tx_buffers();

    check_buffer_addresses(rtl8139_device.rx_buffers, rtl8139_device.tx_buffers, MAX_TX_BUFFERS);

    write_and_verify_register(rtl8139_device.mmio_base, REG_RECEIVE_CONFIGURATION,
                          0x0000000F | // Accept all packets
                          (1 << 7) |   // Wrap around buffer
                          (7 << 8));   // Maximum DMA burst size

    write_and_verify_register_w(rtl8139_device.mmio_base, REG_INTERRUPT_MASK, 0x0005); // Enable RX and TX interrupts
    enable_rx_tx(rtl8139_device.mmio_base);

    printf("RTL8139 initialisiert.\n");
}

void rtl8139_send_packet(void* data, uint16_t len) {
    if (len > TX_BUFFER_SIZE) {
        printf("Fehler: Paket zu groß (%u Bytes, max %u Bytes).\n", len, TX_BUFFER_SIZE);
        return;
    }

    // Prüfen, ob der aktuelle TX-Puffer frei ist
    // uint32_t tsd_status = inl((unsigned short)(rtl8139_device.mmio_base + 0x10 + (current_tx_buffer * 4)));
    // if (tsd_status & 0x8000) {
    //     printf("Sendepuffer %u ist noch nicht frei.\n", current_tx_buffer);
    //     return;
    // }

    // Kopiere die Daten in den dynamischen TX-Puffer
    memcpy(rtl8139_device.tx_buffers, data, len);

    //printf("TX Buffer %d Data (first 16 bytes): ", current_tx_buffer);
    for (int i = 0; i < 16; i++) {
        printf("%02X ", rtl8139_device.tx_buffers[i]);
    }
    printf("\n");


    // Schreibe die Adresse des TX-Puffers in das RTL8139-Register
    // uint32_t tx_buffer_addr = (uintptr_t)rtl8139_device.rx_buffers;
    // outl((unsigned short)(rtl8139_device.mmio_base + 0x20 + (current_tx_buffer * 4)), tx_buffer_addr);

    // Schreibe die Länge des Pakets und starte die Übertragung
    // outl((unsigned short)(rtl8139_device.mmio_base + 0x10 + (rtl8139_device.rx_buffers), len);

    // // Zum nächsten Puffer wechseln
    // current_tx_buffer = (current_tx_buffer + 1) % MAX_TX_BUFFERS;

    printf("Paket gesendet: %u Bytes\n", len);

}

// Empfängt ein Ethernet-Paket
void rtl8139_receive_packet() {
    static uint32_t rx_offset = 0;

    while (!(inb(rtl8139_device.mmio_base + REG_COMMAND) & 0x01)) { // Check RX buffer empty
        uint16_t status = *(volatile uint16_t*)(rtl8139_device.rx_buffers + rx_offset);
        uint16_t length = *(volatile uint16_t*)(rtl8139_device.rx_buffers + rx_offset + 2);

        printf("RX Offset: %u, Status: 0x%04X, Length: %u\n", rx_offset, status, length);

        if (status == 0 || length == 0) {
            printf("No valid packets in RX buffer at offset %u.\n", rx_offset);
            return;
        }


        if (!(status & 0x01)) { // Check "Packet OK" bit
            printf("Invalid packet received. Status: 0x%04X\n", status);
            break;
        }

        if (length == 0 || length > 1500) { // Ensure length is within bounds
            printf("Error: Invalid packet length: %u\n", length);
            break;
        }

        // Dump packet data for debugging
        uint8_t* packet = rtl8139_device.rx_buffers + rx_offset + 4;
        printf("Packet Data (first 16 bytes): ");
        for (int i = 0; i < 16; i++) {
            printf("%02X ", packet[i]);
        }
        printf("\n");

        // Process the packet
        handle_ethernet_frame(packet, length);

        // Update RX offset (4-byte aligned)
        rx_offset = (rx_offset + length + 4 + 3) & ~3; // 4-byte alignment
        if (rx_offset >= RX_BUFFER_SIZE) {
            rx_offset -= RX_BUFFER_SIZE; // Wrap around
        }
        outw(rtl8139_device.mmio_base + 0x38, rx_offset - 16); // Update CURR register
    }

    hex_dump(rtl8139_device.rx_buffers, RX_BUFFER_SIZE);
}

// Interrupt-Handler
void rtl8139_interrupt_handler() {
    uint16_t isr = inw(rtl8139_device.mmio_base + REG_INTERRUPT_STATUS);
    printf("+++++++ rtl8139 Interrupt Status: 0x%04X\n", isr);

    if (isr & 0x01) { // RX OK
        printf("RX OK: Packet received interrupt triggered.\n");
        rtl8139_receive_packet();
    }

    if (isr & 0x04) { // TX OK
        printf("TX OK: Packet sent interrupt triggered.\n");
    }

    // Clear the handled interrupts
    outw(rtl8139_device.mmio_base + REG_INTERRUPT_STATUS, isr);
}

void rtl8139_get_mac_address(uint8_t* mac) {
    for (int i = 0; i < 6; i++) {
        mac[i] = inb((unsigned short)(rtl8139_device.mmio_base + i));
    }
}

// // Findet die RTL8139-Karte im PCI-Bus
// int find_rtl8139() {
//     for (uint16_t bus = 0; bus < 256; ++bus) {
//         for (uint8_t device = 0; device < 32; ++device) {
//             // Prüfen, ob das Gerät existiert
//             uint32_t id = pci_read(bus, device, 0, 0);
//             if ((id & 0xFFFF) == 0xFFFF) { // Kein Gerät vorhanden
//                 continue;
//             }

//             // Prüfen, ob das Gerät mehrere Funktionen unterstützt
//             uint32_t header_type = pci_read(bus, device, 0, 0x0C) >> 16;
//             uint8_t multifunction = (header_type & 0x80) != 0;

//             // Über alle Funktionen iterieren
//             for (uint8_t function = 0; function < (multifunction ? 8 : 1); ++function) {
//                 // PCI-Geräte-ID und Vendor-ID auslesen
//                 id = pci_read(bus, device, function, 0);
//                 if ((id & 0xFFFF) == RTL8139_VENDOR_ID && ((id >> 16) & 0xFFFF) == RTL8139_DEVICE_ID) {
//                     // BAR0 (I/O Base) auslesen
//                     uint32_t bar0 = pci_read(bus, device, function, 0x10);
//                     rtl8139_io_base = bar0 & ~0x3; // Nur I/O-Bits verwenden

//                     //pci_set_irq(bus, device, function, 11);

//                     // IRQ-Nummer auslesen
//                     uint32_t irq_line = pci_read(bus, device, function, 0x3C) & 0xFF;
//                     printf("RTL8139 gefunden: Bus %u, Device %u, Funktion %u, IRQ %u\n", bus, device, function, irq_line);

//                     // Bus Mastering aktivieren
//                     pci_set_bus_master(bus, device, 1);

//                     // Interrupt-Handler registrieren
//                     register_interrupt_handler(irq_line, rtl8139_interrupt_handler);
//                     //unmask_irq(irq_line);

//                     // MAC-Adresse ausgeben
//                     print_mac_address();

//                     return 0;
//                 }
//             }
//         }
//     }
//     return -1; // Keine RTL8139-Karte gefunden
// }

void rtl8139_probe(pci_device_t *pci_dev) {
    if (pci_dev->vendor_id == RTL8139_VENDOR_ID && pci_dev->device_id == RTL8139_DEVICE_ID) {
        //printf("Detected e1000 device\n");
        // Enable the device
        pci_enable_device(pci_dev);

        // Map MMIO region
        uint64_t bar0 = pci_read_bar(pci_dev, 0);
        rtl8139_device.mmio_base = (volatile uint32_t *)map_mmio(bar0);

        // Configure IRQ
        rtl8139_device.irq = pci_configure_irq(pci_dev);

        // Initialize the device
        rtl8139_init(&rtl8139_device);

        uint8_t mac[6];
        rtl8139_get_mac_address(&mac);

        printf("RTL8139 MAC: %02X:%02X:%02X:%02X:%02X:%02X, ", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        printf("IO Base: 0x%08X, IRQ: %u\n", rtl8139_device.mmio_base, rtl8139_device.irq);

    }
}

void rtl8139_detect() {
    printf("Detecting rtl8139 network card...\n");

    pci_register_driver(RTL8139_VENDOR_ID, RTL8139_DEVICE_ID, rtl8139_probe);
}


void rtl8139_send_test_packet() {

    // MAC-Adresse des Zielrechners
    uint8_t dest_mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};

    // MAC-Adresse des Senders
    uint8_t src_mac[6];
    rtl8139_get_mac_address(src_mac);

    // Testdaten
    uint8_t data[] = "Hello, World!";
    uint16_t data_len = strlen((char*)data);

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
}
