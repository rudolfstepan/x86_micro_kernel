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
#define TCR_LB0         0x02  // Loopback Mode 0 (internal)
#define TCR_LB1         0x04  // Loopback Mode 1 (external)

// Define buffer pages
#define RX_START_PAGE 0x40
#define RX_STOP_PAGE  0x80
#define TX_START_PAGE 0x20

// I/O base address (to be set during runtime)
uint16_t io_base = 0xc000;// = 0x300;

uint8_t mac_address[MAC_ADDRESS_LENGTH] = {0};

// prototypes
int ne2000_receive_packet(uint8_t *buffer, uint16_t buffer_size);
void print_packet(const uint8_t *packet, uint16_t length);

// Function to write to a NE2000 register
static inline void ne2000_write(uint8_t reg, uint8_t value) {
    if(io_base == 0) {
        printf("IO base address not set\n");
        return;
    }
    outb(io_base + reg, value);
}

// Function to read from a NE2000 register
static inline uint8_t ne2000_read(uint8_t reg) {
     if(io_base == 0) {
        printf("IO base address not set\n");
        return;
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
    // Perform a software reset
    printf("Resetting NE2000 network card...\n");

    //ne2000_write(NE2000_CR, CR_STP | CR_RD2);
    // write the value of RESET into the RESET register
    ne2000_write(0x1F, ne2000_read(0x1F));

    // wait for the RESET to complete
    while (!(ne2000_read(NE2000_ISR) & 0x80));

    // mask interrupts
    ne2000_write(NE2000_ISR, 0xFF);

    printf("NE2000 reset complete.\n");
}

void ne2000_irq_handler() {
    printf("NE2000 IRQ handler\n");
    uint8_t isr = ne2000_read(NE2000_ISR);
    printf("NE2000 ISR: 0x%02X\n", isr);

    if (isr & 0x01) {  // Packet received
        uint8_t packet[1500];
        int length = ne2000_receive_packet(packet, sizeof(packet));
        if (length > 0) {
            printf("Received packet: %d bytes\n", length);
            print_packet(packet, length);
        }
    }

    // Clear ISR flags
    ne2000_write(NE2000_ISR, isr);
}   

// Function to initialize the NE2000 card
void ne2000_init() {
    printf("Initializing NE2000 network card...\n");
    printf("IO base address: 0x%04X\n", io_base);

    // Reset the card
    ne2000_reset();

    outb(io_base, (1 << 5) | 1);	// page 0, no DMA, stop
    outb(io_base + 0x0E, 0x49);		// set word-wide access
    outb(io_base + 0x0A, 0);		// clear the count regs
    outb(io_base + 0x0B, 0);
    outb(io_base + 0x0F, 0);		// mask completion IRQ
    outb(io_base + 0x07, 0xFF);
    outb(io_base + 0x0C, 0x20);		// set to monitor
    outb(io_base + 0x0D, 0x02);		// and loopback mode.
    outb(io_base + 0x0A, 32);		// reading 32 bytes
    outb(io_base + 0x0B, 0);		// count high
    outb(io_base + 0x08, 0);		// start DMA at 0
    outb(io_base + 0x09, 0);		// start DMA high
    outb(io_base, 0x0A);		// start the read

    // Switch to Page 1
    ne2000_write(NE2000_CR, NE2000_CR_PAGE1);

    // Read MAC address from Physical Address Registers (PAR0 to PAR5)
    for (int i = 0; i < MAC_ADDRESS_LENGTH; i++) {
        mac_address[i] = ne2000_read(io_base + 0x10);
    }

    // Switch back to Page 0
    ne2000_write(NE2000_CR, NE2000_CR_PAGE0);

    // program the PAR0..PAR5 registers to listen for packets to our MAC address!		
    for (int i=0; i<6; i++) {
        ne2000_write(0x01 + i, mac_address[i]);
    };

    // Set the Receive Configuration Register (RCR) to accept all packets
    //ne2000_write(NE2000_RCR, 0x0F);
    ne2000_write(NE2000_RCR, 0x04);  // Accept broadcast packets

    // Set the Transmit Configuration Register (TCR) to loopback mode
    ne2000_write(NE2000_TCR, TCR_LB0);

    // Set the Data Configuration Register (DCR) to word mode
    ne2000_write(NE2000_DCR, DCR_WTS);

    // Set the Interrupt Mask Register (IMR) to enable interrupts
    ne2000_write(NE2000_IMR, 0x01);

    // Set the Page Start Register (PSTART) to the start of the RX buffer
    ne2000_write(NE2000_PSTART, RX_START_PAGE);

    // Set the Page Stop Register (PSTOP) to the end of the RX buffer
    ne2000_write(NE2000_PSTOP, RX_STOP_PAGE);

    // Set the Boundary Register (BNRY) to the start of the RX buffer
    ne2000_write(NE2000_BNRY, RX_START_PAGE);

    // set irq handler
    register_interrupt_handler(11, ne2000_irq_handler);

    printf("NE2000 initialization complete.\n");
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

int ne2000_receive_packet(uint8_t *buffer, uint16_t buffer_size) {
    // Switch to Page 1 to read CURR register
    ne2000_write(NE2000_CR, 0x62);
    uint8_t current_page = ne2000_read(NE2000_CURR);
    ne2000_write(NE2000_CR, 0x22);  // Return to normal operation

    uint8_t boundary = ne2000_read(NE2000_BNRY);

    // If no new packets, return
    if (boundary == current_page) {
        return 0;
    }

    // Calculate start of packet in RX buffer
    uint16_t packet_start = boundary * 256;

    // Read packet header (4 bytes)
    uint8_t header[4];
    for (int i = 0; i < 4; i++) {
        header[i] = inb(io_base + NE2000_DATA);
    }

    uint16_t packet_length = header[2] | (header[3] << 8);

    if (packet_length > buffer_size) {
        printf("Received packet too large: %d bytes\n", packet_length);
        return -1;
    }

    // Read packet data
    for (uint16_t i = 0; i < packet_length; i++) {
        buffer[i] = inb(io_base + NE2000_DATA);
    }

    // Update Boundary Register
    ne2000_write(NE2000_BNRY, header[1] - 1);

    printf("Packet received, length: %d bytes\n", packet_length);
    
    return packet_length;
}

void ne2000_validate_init() {
    printf("PSTART: 0x%02X\n", ne2000_read(NE2000_PSTART));
    printf("PSTOP:  0x%02X\n", ne2000_read(NE2000_PSTOP));
    printf("BNRY:   0x%02X\n", ne2000_read(NE2000_BNRY));
    printf("TPSR:   0x%02X\n", ne2000_read(NE2000_TPSR));
}

void ne2000_print_mac_address() {
    // Print MAC address
    printf("NE2000 MAC address: %02X:%02X:%02X:%02X:%02X:%02X\n",
           mac_address[0], mac_address[1], mac_address[2], mac_address[3], mac_address[4], mac_address[5]);
}

void ne2000_detect() {
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
                        return;
                    }

                    printf("NE2000 IO base address: 0x%04X\n", io_base);
                    ne2000_init();
                    ne2000_validate_init();
                    ne2000_print_mac_address();

                    return;
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
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05,  // Source MAC
        0x08, 0x00,                          // EtherType (IPv4)
        0x45, 0x00, 0x00, 0x54,              // Payload
        0x00, 0x01, 0xDE, 0xAD, 0xBE, 0xEF
    };

    // Enable loopback mode
    ne2000_enable_loopback(io_base);

    // Send the packet
    ne2000_send_packet(test_packet, sizeof(test_packet));

    // Attempt to receive the packet
    uint8_t received_packet[1500];
    int received_length = ne2000_receive_packet(received_packet, sizeof(received_packet));

    if (received_length > 0) {
        printf("Loopback test successful, received %d bytes.\n", received_length);
    } else {
        printf("Loopback test failed, no packet received.\n");
    }

    // Disable loopback mode
    ne2000_disable_loopback(io_base);
}

void ne2000_test_send() {

    test_ne2000_loopback(io_base);


    // Test sending a packet
    // uint8_t test_packet[] = {
    //     // Destination MAC
    //     0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    //     // Source MAC (use NE2000 MAC)
    //     mac_address[0], mac_address[1], mac_address[2], mac_address[3], mac_address[4], mac_address[5],
    //     // EtherType (0x0800 = IPv4)
    //     0x08, 0x00,
    //     // Payload (dummy data)
    //     0xDE, 0xAD, 0xBE, 0xEF
    // };

    // ne2000_send_packet(test_packet, sizeof(test_packet));
}


