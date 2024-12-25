#include "ne2000.h"
#include "toolchain/stdio.h"
#include "toolchain/stdlib.h"
#include "toolchain/string.h"
#include "kernel/sys.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "drivers/pci.h"
#include "drivers/io/io.h"

#define NE2000_VENDOR_ID 0x10EC
#define NE2000_DEVICE_ID 0x8029

// Define NE2000 register offsets
#define NE2000_CR      0x00  // Command Register
#define NE2000_PSTART  0x01  // Page Start Register
#define NE2000_PSTOP   0x02  // Page Stop Register
#define NE2000_BNRY    0x03  // Boundary Pointer
#define NE2000_TPSR    0x04  // Transmit Page Start Register
#define NE2000_TBCR0   0x05  // Transmit Byte Count Register 0
#define NE2000_TBCR1   0x06  // Transmit Byte Count Register 1
#define NE2000_ISR     0x07  // Interrupt Status Register
#define NE2000_RSAR0   0x08  // Remote Start Address Register 0
#define NE2000_RSAR1   0x09  // Remote Start Address Register 1
#define NE2000_RBCR0   0x0A  // Remote Byte Count Register 0
#define NE2000_RBCR1   0x0B  // Remote Byte Count Register 1
#define NE2000_RCR     0x0C  // Receive Configuration Register
#define NE2000_TCR     0x0D  // Transmit Configuration Register
#define NE2000_DCR     0x0E  // Data Configuration Register
#define NE2000_IMR     0x0F  // Interrupt Mask Register
#define NE2000_CURR    0x07  // Current Page Register (Page 1)
#define NE2000_PAR0    0x01  // Physical Address Register 0 (Page 1)
#define NE2000_CR_PAGE1 0x40 // Command Register: Select Page 1
#define NE2000_CR_PAGE0 0x00 // Command Register: Select Page 0
#define NE2000_DATA    0x10  // Data Port

#define MAC_ADDRESS_LENGTH 6

// Define NE2000 command bits
#define CR_STP 0x01  // Stop
#define CR_STA 0x02  // Start
#define CR_RD2 0x08  // Remote DMA Command

// Define NE2000 ISR bits
#define ISR_RDC 0x40  // Remote DMA Complete
#define ISR_PTX 0x02  // Packet Transmitted

// Define NE2000 DCR bits
#define DCR_WTS 0x01  // Word Transfer Select

// Define NE2000 RCR bits
#define RCR_MON 0x20  // Monitor Mode

// Define NE2000 TCR bits
#define TCR_LB0 0x02  // Loopback Mode 0 (internal)
#define TCR_LB1 0x04  // Loopback Mode 1 (external)

// Define buffer pages
#define RX_START_PAGE 0x40
#define RX_STOP_PAGE  0x80
#define TX_START_PAGE 0x20

#define CR_RD0  0x08  // Remote Read DMA
#define CR_RD1  0x10  // Remote Write DMA
#define CR_RD2  0x18  // Send Packet Command (Remote Read/Write Off)


#define DEBUG_PRINT(fmt, ...) printf("DEBUG: " fmt "\n", ##__VA_ARGS__)

// I/O base address (to be set during runtime)
uint16_t io_base = 0xc0ff;// = 0x300;
uint8_t mac_address[MAC_ADDRESS_LENGTH] = {0};

// prototypes
int ne2000_receive_packet(uint8_t *buffer, uint16_t buffer_size);
void print_packet(const uint8_t *packet, uint16_t length);

// Function to write to a NE2000 register
static inline void ne2000_write(uint8_t reg, uint8_t value) {
    if (io_base == 0) {
        printf("IO base address not set\n");
        return;
    }
    outb(io_base + reg, value);
}

// Function to read from a NE2000 register
static inline uint8_t ne2000_read(uint8_t reg) {
    if (io_base == 0) {
        printf("IO base address not set\n");
        return 0xFF;
    }
    return inb(io_base + reg);
}

// Function to enable loopback mode
void ne2000_enable_loopback(uint16_t io_base) {
    // Ensure the card is started
    uint8_t cr = inb(io_base + NE2000_CR);
    outb(io_base + NE2000_CR, cr | CR_STA);

    // Set TCR to enable internal loopback mode
    outb(io_base + NE2000_TCR, TCR_LB0);
    printf("NE2000 loopback mode enabled (internal).\n");
}

// Function to disable loopback mode
void ne2000_disable_loopback(uint16_t io_base) {
    // Restore TCR to normal transmission mode
    outb(io_base + NE2000_TCR, 0x00);
    printf("NE2000 loopback mode disabled.\n");
}

// Function to reset the NE2000 card
void ne2000_reset() {
    DEBUG_PRINT("Resetting NE2000 network card...");
    ne2000_write(0x1F, ne2000_read(0x1F));  // Trigger software reset

    while (!(ne2000_read(NE2000_ISR) & 0x80)) {
        // Wait for reset complete
    }
    ne2000_write(NE2000_ISR, 0xFF);  // Clear all ISR flags
    DEBUG_PRINT("NE2000 reset complete.");
}

// Improved IRQ handler
void ne2000_irq_handler() {
    DEBUG_PRINT("NE2000 IRQ triggered.");
    uint8_t isr = ne2000_read(NE2000_ISR);
    DEBUG_PRINT("ISR: 0x%02X", isr);

    if (isr & 0x01) {  // Packet received
        uint8_t packet[1500];
        int length = ne2000_receive_packet(packet, sizeof(packet));
        if (length > 0) {
            printf("Packet received (%d bytes):\n", length);
            print_packet(packet, length);
        }
    }

    if (isr & 0x02) {  // Packet transmitted
        DEBUG_PRINT("Packet transmitted.");
    }

    ne2000_write(NE2000_ISR, isr);  // Clear handled ISR flags
}

// Improved initialization function
void ne2000_init() {
    DEBUG_PRINT("Initializing NE2000...");
    DEBUG_PRINT("IO base address: 0x%04X", io_base);

    // Reset the card
    ne2000_reset();

    // Program RX buffer boundaries
    ne2000_write(NE2000_PSTART, RX_START_PAGE);
    ne2000_write(NE2000_PSTOP, RX_STOP_PAGE);
    ne2000_write(NE2000_BNRY, RX_START_PAGE);

    // Set RX configuration to accept broadcast and physical packets
    ne2000_write(NE2000_RCR, 0x0F);  // Accept all packets

    // Set TX configuration to enable loopback (internal)
    ne2000_write(NE2000_TCR, TCR_LB0);

    // Set data configuration (word transfer)
    ne2000_write(NE2000_DCR, DCR_WTS);

    // Enable RX interrupts
    ne2000_write(NE2000_IMR, 0x01);

    // Read and store MAC address
    ne2000_write(NE2000_CR, NE2000_CR_PAGE1);  // Switch to page 1
    for (int i = 0; i < MAC_ADDRESS_LENGTH; i++) {
        mac_address[i] = ne2000_read(0x10);
    }
    ne2000_write(NE2000_CR, NE2000_CR_PAGE0);  // Switch back to page 0

    DEBUG_PRINT("NE2000 initialized successfully.");
    ne2000_print_mac_address();
}

void ne2000_send_packet(const uint8_t *data, uint16_t length) {
    if (length > 1500) {
        printf("Packet too large to send: %d bytes\n", length);
        return;
    }

    uint8_t tx_page_start = 0x40;  // Example TX buffer start page

    // 1. Set COMMAND register to "start" and "nodma" (0x22)
    ne2000_write(NE2000_CR, 0x22);

    // 2. Load RBCRx (Remote Byte Count Registers) with the packet size
    ne2000_write(NE2000_RBCR0, length & 0xFF);    // Low byte
    ne2000_write(NE2000_RBCR1, (length >> 8));    // High byte

    // 3. Clear "Remote DMA complete" bit by writing 1 to bit 6 of ISR
    ne2000_write(NE2000_ISR, ISR_RDC);  // Write 0x40 to ISR

    // 4. Load RSARx (Remote Start Address Registers) with 0x00 (low) and target page number (high)
    ne2000_write(NE2000_RSAR0, 0x00);             // Low byte
    ne2000_write(NE2000_RSAR1, tx_page_start);    // High byte (target page)

    // 5. Set COMMAND register to "start" and "remote write DMA" (0x12)
    ne2000_write(NE2000_CR, 0x12);

    // 6. Write packet data to the "data port" (register 0x10)
    uint16_t i = 0;
    for (; i + 1 < length; i += 2) {
        uint16_t word = (data[i + 1] << 8) | data[i];  // Combine two bytes into a 16-bit word
        outw(io_base + NE2000_DATA, word);            // Write the 16-bit word
    }
    // Write the last byte if the length is odd
    if (i < length) {
        outb(io_base + NE2000_DATA, data[i]);
    }

    // 7. Poll ISR register until bit 6 ("Remote DMA completed") is set
    while (!(ne2000_read(NE2000_ISR) & ISR_RDC)) {
        // Wait for Remote DMA to complete
    }

    // Clear "Remote DMA complete" bit
    ne2000_write(NE2000_ISR, ISR_RDC);

    // Start transmission
    ne2000_write(NE2000_TPSR, tx_page_start);    // Set Transmit Page Start
    ne2000_write(NE2000_TBCR0, length & 0xFF);  // Set Transmit Byte Count (low byte)
    ne2000_write(NE2000_TBCR1, (length >> 8));  // Set Transmit Byte Count (high byte)
    ne2000_write(NE2000_CR, 0x26);              // Start transmission (CR = 0x26)

    // Wait for transmission complete (ISR_PTX = 0x02)
    while (!(ne2000_read(NE2000_ISR) & ISR_PTX)) {
        // Wait for Packet Transmitted
    }

    // Clear transmission complete flag
    ne2000_write(NE2000_ISR, ISR_PTX);

    printf("Packet sent successfully, length: %d bytes\n", length);
}

// Improved receive function
int ne2000_receive_packet(uint8_t *buffer, uint16_t buffer_size) {
    // Step 1: Switch to Page 1 to get CURR pointer
    ne2000_write(NE2000_CR, NE2000_CR_PAGE1);
    uint8_t curr_page = ne2000_read(NE2000_CURR); // Current RX page
    ne2000_write(NE2000_CR, NE2000_CR_PAGE0);     // Switch back to Page 0

    uint8_t boundary = ne2000_read(NE2000_BNRY);

    // Log boundary and current page
    printf("Boundary=0x%02X, CurrentPage=0x%02X\n", boundary, curr_page);

    // Step 2: Check if new packets are available
    if (boundary == curr_page) {
        return 0;  // No new packets
    }

    // Step 3: Calculate packet start address
    uint16_t packet_start = boundary * 256;

    // Step 4: Read RX header via DMA
    ne2000_write(NE2000_RSAR0, packet_start & 0xFF);
    ne2000_write(NE2000_RSAR1, (packet_start >> 8));
    ne2000_write(NE2000_RBCR0, 4);  // Request 4 bytes (RX header)
    ne2000_write(NE2000_RBCR1, 0);
    ne2000_write(NE2000_CR, CR_STA | CR_RD0);  // Start DMA

    uint8_t header[4];
    for (int i = 0; i < 4; i++) {
        header[i] = inb(io_base + NE2000_DATA);
    }

    // Parse RX header
    uint8_t rx_status = header[0];
    uint8_t next_page = header[1];
    uint16_t packet_length = header[2] | (header[3] << 8);

    printf("RX Header: Status=0x%02X, NextPage=0x%02X, Length=%d\n",
           rx_status, next_page, packet_length);

    // Step 5: Validate packet length
    if (packet_length > buffer_size || packet_length < 6) {
        printf("Invalid packet length: %d\n", packet_length);
        return -1;
    }

    // Step 6: Set up DMA for payload
    uint16_t payload_start = packet_start + 4;
    ne2000_write(NE2000_RSAR0, payload_start & 0xFF);
    ne2000_write(NE2000_RSAR1, (payload_start >> 8));
    ne2000_write(NE2000_RBCR0, packet_length & 0xFF);
    ne2000_write(NE2000_RBCR1, (packet_length >> 8));
    ne2000_write(NE2000_CR, CR_STA | CR_RD0);  // Start DMA

    // Step 7: Read packet payload in words
    uint16_t i = 0;
    for (; i + 1 < packet_length; i += 2) {
        uint16_t word = inw(io_base + NE2000_DATA);  // Read 16-bit word
        buffer[i] = word & 0xFF;                    // Lower byte
        buffer[i + 1] = (word >> 8);                // Higher byte
    }

    // Step 8: Read the last byte if the packet length is odd
    if (i < packet_length) {
        buffer[i] = inb(io_base + NE2000_DATA);  // Read single byte
    }

    // Step 9: Update boundary
    uint8_t new_boundary = (next_page == RX_START_PAGE) ? RX_STOP_PAGE - 1 : next_page - 1;
    ne2000_write(NE2000_BNRY, new_boundary);

    // Step 10: Clear RDC flag
    ne2000_write(NE2000_ISR, ISR_RDC);

    return packet_length;
}

// Utility: Print MAC address
void ne2000_print_mac_address() {
    printf("MAC address: %02X:%02X:%02X:%02X:%02X:%02X\n",
           mac_address[0], mac_address[1], mac_address[2],
           mac_address[3], mac_address[4], mac_address[5]);
}

int ne2000_detect() {
    printf("Detecting NE2000 network card...\n");
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
                if ((id & 0xFFFF) == NE2000_VENDOR_ID && ((id >> 16) & 0xFFFF) == NE2000_DEVICE_ID) {
                    printf("NE2000 network card found at %02X:%02X.%d\n", bus, device, function);
                    
                    // get base io  address
                    io_base = get_io_base(bus, device, function);
                    if (io_base == 0) {
                        printf("Failed to get IO base address\n");
                        return -1;
                    }

                    printf("NE2000 IO base address: 0x%04X\n", io_base);
                    ne2000_init();
                    ne2000_print_mac_address();

                    return 0;
                }
            }
        }
    }
}

void print_packet(const uint8_t *packet, uint16_t length) {
    printf("Packet data (%d bytes): ", length);
    for (uint16_t i = 0; i < length; i++) {
        printf("%02X ", packet[i]);
    }
    printf("\n");
}

void test_ne2000_loopback(uint16_t io_base) {
    uint8_t test_packet[] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // Destination MAC (Broadcast)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Source MAC (will be replaced with NE2000 MAC)
        0x45, 0x00, 0x00, 0x54,              // Payload
        0x00, 0x01, 0xEE, 0xCE, 0xDE, 0xFE
    };

    // Enable loopback mode
    //ne2000_enable_loopback(io_base);

    // Send the packet
    ne2000_send_packet(test_packet, sizeof(test_packet));

    // Attempt to receive the packet
    uint8_t received_packet[1500];
    int received_length = ne2000_receive_packet(received_packet, sizeof(received_packet));

    hex_dump(received_packet, 256);

    if (received_length > 0) {
        printf("Loopback test successful, received %d bytes.\n", received_length);
    } else {
        printf("Loopback test failed, no packet received.\n");
    }

    // Disable loopback mode
    //ne2000_disable_loopback(io_base);
}

void ne2000_test_send() {

    test_ne2000_loopback(io_base);
}
