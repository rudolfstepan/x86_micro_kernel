#include "ne2000.h"
#include "lib/libc/stdio.h"
#include "lib/libc/stdlib.h"
#include "lib/libc/string.h"
#include "arch/x86/sys.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "drivers/bus/pci.h"
#include "drivers/char/io.h"

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
static bool ne2000_initialized = false;

// prototypes
int ne2000_receive_packet(uint8_t *buffer, uint16_t buffer_size);
void print_packet(const uint8_t *packet, uint16_t length);
void print_hex_dump(const char* label, const uint8_t *data, uint16_t length);
void ne2000_dump_page(uint8_t page_num, uint16_t length);

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

    // Set TCR to enable Mode 2 loopback (external loopback through encoder/decoder)
    // Mode 2 = LB0 | LB1 = 0x06
    // This mode actually puts packets in the receive buffer
    outb(io_base + NE2000_TCR, 0x06);  // External loopback
    printf("NE2000 loopback mode enabled (external/mode 2).\n");
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
    uint8_t isr = ne2000_read(NE2000_ISR);
    
    // Silently ignore spurious interrupts
    if (isr == 0) {
        return;
    }
    
    printf("NE2000 IRQ - ISR: 0x%02X\n", isr);

    // Handle buffer overrun first (most critical)
    if (isr & 0x10) {  // Overwrite warning
        printf("[WARNING] RX buffer overrun - resetting receive buffer\n");
        // Stop NIC
        ne2000_write(NE2000_CR, 0x21);  // Stop
        // Clear Remote DMA
        ne2000_write(NE2000_RBCR0, 0);
        ne2000_write(NE2000_RBCR1, 0);
        // Reset receive buffer pointers
        ne2000_write(NE2000_BNRY, RX_START_PAGE);
        ne2000_write(NE2000_CR, 0x62);  // Page 1
        ne2000_write(NE2000_CURR, RX_START_PAGE + 1);
        ne2000_write(NE2000_CR, 0x22);  // Page 0, Start
        // Clear overwrite flag
        ne2000_write(NE2000_ISR, 0x10);
    }

    // Process up to 5 packets per interrupt to avoid getting stuck
    int packets_processed = 0;
    const int max_packets_per_irq = 5;
    
    while ((isr & 0x01) && packets_processed < max_packets_per_irq) {  // Packet received
        uint8_t packet[1500];
        int length = ne2000_receive_packet(packet, sizeof(packet));
        if (length > 0) {
            printf("Received packet: %d bytes\n", length);
            
            // Parse and display ethernet frame
            if (length >= 14) {  // Minimum ethernet frame has 14-byte header
                printf("  Dst MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                       packet[0], packet[1], packet[2], packet[3], packet[4], packet[5]);
                printf("  Src MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                       packet[6], packet[7], packet[8], packet[9], packet[10], packet[11]);
                uint16_t ethertype = (packet[12] << 8) | packet[13];
                printf("  EtherType: 0x%04X ", ethertype);
                
                if (ethertype == 0x0806) {
                    printf("(ARP)\n");
                    // Show ARP details if enough data
                    if (length >= 42) {
                        uint16_t ar_op = (packet[20] << 8) | packet[21];
                        printf("    ARP Operation: %d %s\n", ar_op, 
                               ar_op == 1 ? "(Request)" : ar_op == 2 ? "(Reply)" : "");
                        printf("    Sender IP: %d.%d.%d.%d\n",
                               packet[28], packet[29], packet[30], packet[31]);
                        printf("    Target IP: %d.%d.%d.%d\n",
                               packet[38], packet[39], packet[40], packet[41]);
                    }
                } else if (ethertype == 0x0800) {
                    printf("(IPv4)\n");
                } else if (ethertype == 0x86DD) {
                    printf("(IPv6)\n");
                } else {
                    printf("(Unknown)\n");
                }
            }
            
            print_packet(packet, length > 64 ? 64 : length);
            packets_processed++;
        } else if (length < 0) {
            // Error occurred, stop processing
            break;
        } else {
            // No more packets
            break;
        }
        
        // Check if more packets arrived
        isr = ne2000_read(NE2000_ISR);
    }

    // Clear all ISR flags
    ne2000_write(NE2000_ISR, 0xFF);
}   

// Function to initialize the NE2000 card
void ne2000_init() {
    printf("Initializing NE2000 network card...\n");
    printf("IO base address: 0x%04X\n", io_base);

    // Reset the card
    ne2000_reset();

    // === Proper NE2000 Initialization Sequence ===
    
    // 1. Stop the NIC (CR = 0x21: Page 0, Stop, NoDMA)
    ne2000_write(NE2000_CR, 0x21);
    
    // 2. Set Data Configuration Register (DCR) - BYTE mode, loopback disabled
    // DCR bits: WTS=0 (byte mode), BOS=0, LAS=0, LS=0, ARM=1, FT=01
    ne2000_write(NE2000_DCR, 0x48);  // Byte mode instead of word mode
    
    // 3. Clear Remote Byte Count Registers
    ne2000_write(NE2000_RBCR0, 0);
    ne2000_write(NE2000_RBCR1, 0);
    
    // 4. Set Receive Configuration Register (RCR) - monitor mode initially  
    ne2000_write(NE2000_RCR, 0x20);  // Monitor mode (no packets accepted yet)
    
    // 5. Set Transmit Configuration Register (TCR) - loopback mode
    ne2000_write(NE2000_TCR, 0x02);  // Internal loopback
    
    // 6. Set up Receive Buffer Ring
    ne2000_write(NE2000_PSTART, RX_START_PAGE);
    ne2000_write(NE2000_PSTOP, RX_STOP_PAGE);
    ne2000_write(NE2000_BNRY, RX_START_PAGE);
    
    // 7. Clear Interrupt Status Register
    ne2000_write(NE2000_ISR, 0xFF);
    
    // 8. Set Interrupt Mask Register - enable receive interrupt
    ne2000_write(NE2000_IMR, 0x0F);  // Enable RX, TX, RX error, TX error interrupts
    
    // 9. Read MAC address from PROM using Remote DMA
    // The NE2000 stores MAC in PROM at address 0x0000, word-wide (6 bytes = 3 words)
    // But the PROM stores each byte twice (word format), so we read 12 bytes
    ne2000_write(NE2000_CR, 0x21);      // Page 0, Stop, NoDMA
    ne2000_write(NE2000_RBCR0, 12);     // Read 12 bytes (MAC is doubled in PROM)
    ne2000_write(NE2000_RBCR1, 0);
    ne2000_write(NE2000_RSAR0, 0);      // Start at PROM address 0
    ne2000_write(NE2000_RSAR1, 0);
    ne2000_write(NE2000_CR, 0x0A);      // Start Remote DMA Read
    
    // Read MAC address from data port (every other byte in word mode)
    for (int i = 0; i < MAC_ADDRESS_LENGTH; i++) {
        mac_address[i] = inb(io_base + NE2000_DATA);
        inb(io_base + NE2000_DATA);  // Dummy read (PROM stores each byte twice)
    }
    
    // Wait for Remote DMA to complete
    while (!(ne2000_read(NE2000_ISR) & ISR_RDC));
    ne2000_write(NE2000_ISR, ISR_RDC);  // Clear RDC flag
    
    // 10. Switch to Page 1 to set Physical Address and Multicast
    ne2000_write(NE2000_CR, 0x61);  // Page 1, Stop, NoDMA
    
    // 11. Set Physical Address Registers (write the MAC we just read)
    for (int i = 0; i < MAC_ADDRESS_LENGTH; i++) {
        ne2000_write(NE2000_PAR0 + i, mac_address[i]);
    }
    
    // 12. Set Multicast Address Registers (accept all multicast for broadcast)
    for (int i = 0; i < 8; i++) {
        ne2000_write(0x08 + i, 0xFF);  // MAR0-MAR7
    }
    
    // 13. Set CURR (Current Page Register)
    ne2000_write(NE2000_CURR, RX_START_PAGE + 1);
    
    // 14. Switch back to Page 0 and START the NIC
    ne2000_write(NE2000_CR, 0x22);  // Page 0, Start, NoDMA
    
    // 15. Enable packet reception (exit monitor mode)
    ne2000_write(NE2000_RCR, 0x04);  // Accept broadcast packets
    
    // 16. Set normal transmission mode initially (loopback will be set by test function)
    ne2000_write(NE2000_TCR, 0x00);  // Normal operation

    // set irq handler
    register_interrupt_handler(11, ne2000_irq_handler);

    ne2000_initialized = true;
    printf("NE2000 initialization complete.\n");
}

bool ne2000_is_initialized() {
    return ne2000_initialized;
}

void ne2000_print_status() {
    if (!ne2000_initialized) {
        printf("NE2000 is not initialized\n");
        return;
    }

    printf("\n=== NE2000 Network Card Status ===\n");
    printf("IO Base Address: 0x%04X\n", io_base);
    printf("MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
           mac_address[0], mac_address[1], mac_address[2], 
           mac_address[3], mac_address[4], mac_address[5]);
    
    printf("\nRegister Status:\n");
    printf("  PSTART:  0x%02X (RX buffer start page)\n", ne2000_read(NE2000_PSTART));
    printf("  PSTOP:   0x%02X (RX buffer stop page)\n", ne2000_read(NE2000_PSTOP));
    printf("  BNRY:    0x%02X (Boundary pointer)\n", ne2000_read(NE2000_BNRY));
    printf("  TPSR:    0x%02X (TX page start)\n", ne2000_read(NE2000_TPSR));
    printf("  ISR:     0x%02X (Interrupt status)\n", ne2000_read(NE2000_ISR));
    printf("  IMR:     0x%02X (Interrupt mask)\n", ne2000_read(NE2000_IMR));
    
    // Switch to page 1 to read CURR
    ne2000_write(NE2000_CR, 0x62);
    uint8_t current_page = ne2000_read(NE2000_CURR);
    ne2000_write(NE2000_CR, 0x22);
    printf("  CURR:    0x%02X (Current page)\n", current_page);
    
    printf("==================================\n\n");
}

void ne2000_send_packet(uint8_t *data, uint16_t length) {
    if (length > 1500) {
        printf("Packet too large to send: %d bytes\n", length);
        return;
    }

    // Use the defined TX buffer page, not hardcoded
    uint8_t tx_page_start = TX_START_PAGE;  // 0x20 - separate from RX buffer
    
    // Pad packet to minimum ethernet size (60 bytes)
    uint16_t send_length = length;
    if (send_length < 60) {
        send_length = 60;
    }

    // 1. Set COMMAND register to "start" and "nodma" (0x22)
    ne2000_write(NE2000_CR, 0x22);

    // 2. Load RBCRx (Remote Byte Count Registers) with the packet size
    ne2000_write(NE2000_RBCR0, send_length & 0xFF);    // Low byte
    ne2000_write(NE2000_RBCR1, (send_length >> 8));    // High byte

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
    // Pad with zeros if necessary
    while (i < send_length) {
        outb(io_base + NE2000_DATA, 0);
        i++;
    }

    // 7. Poll ISR register until bit 6 ("Remote DMA completed") is set
    int timeout = 10000;
    while (!(ne2000_read(NE2000_ISR) & ISR_RDC) && timeout-- > 0) {
        // Wait for Remote DMA to complete
    }
    
    if (timeout <= 0) {
        printf("[TX] Timeout waiting for Remote DMA complete\n");
        return;
    }

    // Clear "Remote DMA complete" bit
    ne2000_write(NE2000_ISR, ISR_RDC);

    // Start transmission
    ne2000_write(NE2000_TPSR, tx_page_start);    // Set Transmit Page Start
    ne2000_write(NE2000_TBCR0, send_length & 0xFF);  // Set Transmit Byte Count (low byte)
    ne2000_write(NE2000_TBCR1, (send_length >> 8));  // Set Transmit Byte Count (high byte)
    ne2000_write(NE2000_CR, 0x26);              // Start transmission (CR = 0x26)

    // Wait for transmission complete (ISR_PTX = 0x02)
    timeout = 100000;  // Longer timeout for actual transmission
    while (!(ne2000_read(NE2000_ISR) & ISR_PTX) && timeout-- > 0) {
        // Wait for Packet Transmitted
    }

    if (timeout <= 0) {
        printf("[TX] Timeout waiting for packet transmission\n");
        return;
    }

    // Clear transmission complete flag
    ne2000_write(NE2000_ISR, ISR_PTX);

    printf("Packet sent successfully, length: %d bytes\n", length);
}

int ne2000_receive_packet(uint8_t *buffer, uint16_t buffer_size) {
    // Switch to Page 1 to read CURR register
    ne2000_write(NE2000_CR, 0x62);  // Page 1, Start, NoDMA
    uint8_t current_page = ne2000_read(NE2000_CURR);
    ne2000_write(NE2000_CR, 0x22);  // Page 0, Start, NoDMA

    uint8_t boundary = ne2000_read(NE2000_BNRY);
    
    // Check if buffer is empty (CURR == next read position)
    uint8_t next_read = boundary + 1;
    if (next_read >= RX_STOP_PAGE) {
        next_read = RX_START_PAGE;
    }
    
    printf("[RX] CURR=0x%02X, BNRY=0x%02X, scanning pages 0x%02X-0x%02X\n", 
           current_page, boundary, next_read, current_page - 1);
    
    // If no new packets, return silently
    if (next_read == current_page) {
        return 0;  // Buffer empty
    }

    // Scan forward to find first non-empty page (skip up to 5 empty pages)
    uint8_t next_packet_page = next_read;
    int empty_pages_skipped = 0;
    const int max_empty_pages = 5;
    
    while (empty_pages_skipped < max_empty_pages && next_packet_page != current_page) {
        // Try to read header from this page
        ne2000_write(NE2000_CR, 0x22);
        ne2000_write(NE2000_RBCR0, 4);
        ne2000_write(NE2000_RBCR1, 0);
        ne2000_write(NE2000_RSAR0, 0);
        ne2000_write(NE2000_RSAR1, next_packet_page);
        ne2000_write(NE2000_CR, 0x0A);
        
        uint8_t test_header[4];
        for (int i = 0; i < 4; i++) {
            test_header[i] = inb(io_base + NE2000_DATA);
        }
        
        int timeout = 10000;
        while (!(ne2000_read(NE2000_ISR) & ISR_RDC) && timeout-- > 0);
        ne2000_write(NE2000_ISR, ISR_RDC);
        
        // Check if page is empty (all zeros)
        if (test_header[0] == 0 && test_header[1] == 0 && 
            test_header[2] == 0 && test_header[3] == 0) {
            empty_pages_skipped++;
            next_packet_page++;
            if (next_packet_page >= RX_STOP_PAGE) {
                next_packet_page = RX_START_PAGE;
            }
            continue;
        }
        
        // Found non-empty page!
        if (empty_pages_skipped > 0) {
            printf("[RX] Skipped %d empty pages, found data at 0x%02X\n", 
                   empty_pages_skipped, next_packet_page);
        }
        break;
    }
    
    if (next_packet_page == current_page) {
        return 0;
    }

    // Now read the actual packet header from the non-empty page
    ne2000_write(NE2000_CR, 0x22);              // Page 0, Start, NoDMA
    ne2000_write(NE2000_RBCR0, 4);              // Read 4 bytes (header)
    ne2000_write(NE2000_RBCR1, 0);
    ne2000_write(NE2000_RSAR0, 0);              // Start at offset 0 in page
    ne2000_write(NE2000_RSAR1, next_packet_page); // Page number
    ne2000_write(NE2000_CR, 0x0A);              // Page 0, Start, Remote Read

    // Read packet header (4 bytes)
    uint8_t header[4];
    for (int i = 0; i < 4; i++) {
        header[i] = inb(io_base + NE2000_DATA);
    }

    // Wait for Remote DMA to complete with timeout
    int timeout = 10000;
    while (!(ne2000_read(NE2000_ISR) & ISR_RDC) && timeout-- > 0);
    if (timeout <= 0) {
        printf("[RX] Timeout waiting for DMA (header)\n");
        return -1;
    }
    ne2000_write(NE2000_ISR, ISR_RDC);  // Clear RDC flag

    uint8_t status = header[0];
    uint8_t next_page = header[1];
    uint16_t packet_length = header[2] | (header[3] << 8);

    printf("[RX] Page 0x%02X: status=0x%02X, next=0x%02X, len=%d\n", 
           next_packet_page, status, next_page, packet_length);

    // Show first 64 bytes of page for diagnosis
    if (packet_length > 1518) {
        uint8_t page_data[64];
        ne2000_write(NE2000_CR, 0x22);
        ne2000_write(NE2000_RBCR0, 64);
        ne2000_write(NE2000_RBCR1, 0);
        ne2000_write(NE2000_RSAR0, 0);
        ne2000_write(NE2000_RSAR1, next_packet_page);
        ne2000_write(NE2000_CR, 0x0A);
        for (int i = 0; i < 64; i++) {
            page_data[i] = inb(io_base + NE2000_DATA);
        }
        int timeout = 10000;
        while (!(ne2000_read(NE2000_ISR) & ISR_RDC) && timeout-- > 0);
        ne2000_write(NE2000_ISR, ISR_RDC);
        print_hex_dump("[RX] Page content", page_data, 64);
    }

    // Check status byte for errors
    // Bit 0 (0x01) should be set for valid packet
    if (!(status & 0x01)) {
        printf("[RX] Bad status, skipping page\n");
        // Try to advance past bad packet if we have valid next_page
        if (next_page >= RX_START_PAGE && next_page < RX_STOP_PAGE) {
            uint8_t new_boundary = next_page - 1;
            if (new_boundary < RX_START_PAGE) {
                new_boundary = RX_STOP_PAGE - 1;
            }
            // CRITICAL: Switch to Page 0 before writing BNRY!
            ne2000_write(NE2000_CR, 0x22);  // Page 0, Start, NoDMA
            ne2000_write(NE2000_BNRY, new_boundary);
        } else {
            // Both status and next_page are bad - manual advance
            ne2000_write(NE2000_CR, 0x22);  // Page 0, Start, NoDMA
            ne2000_write(NE2000_BNRY, next_packet_page);
        }
        return -1;
    }

    // Validate next_page pointer
    if (next_page < RX_START_PAGE || next_page >= RX_STOP_PAGE) {
        printf("[RX] Invalid next_page: 0x%02X - advancing manually\n", next_page);
        // Manually advance to next page since next_page is corrupt
        uint8_t manual_next = next_packet_page + 1;
        if (manual_next >= RX_STOP_PAGE) {
            manual_next = RX_START_PAGE;
        }
        ne2000_write(NE2000_CR, 0x22);  // Page 0, Start, NoDMA
        ne2000_write(NE2000_BNRY, next_packet_page);
        return -1;
    }

    // Validate packet length
    if (packet_length < 60 || packet_length > 1518) {
        printf("[RX] Invalid length: %d - trying to recover packet\n", packet_length);
        
        // Header is corrupt, but there might be valid data after it
        // Try reading 64 bytes starting from offset 0 (include the corrupt header)
        uint16_t recovery_length = 64;
        
        ne2000_write(NE2000_CR, 0x22);
        ne2000_write(NE2000_RBCR0, recovery_length & 0xFF);
        ne2000_write(NE2000_RBCR1, (recovery_length >> 8) & 0xFF);
        ne2000_write(NE2000_RSAR0, 0);  // Start at offset 0 (include header)
        ne2000_write(NE2000_RSAR1, next_packet_page);
        ne2000_write(NE2000_CR, 0x0A);
        
        for (uint16_t i = 0; i < recovery_length; i++) {
            buffer[i] = inb(io_base + NE2000_DATA);
        }
        
        int timeout = 10000;
        while (!(ne2000_read(NE2000_ISR) & ISR_RDC) && timeout-- > 0);
        ne2000_write(NE2000_ISR, ISR_RDC);
        
        printf("[RX] Recovered %d bytes, analyzing...\n", recovery_length);
        print_hex_dump("[RX] Recovered data", buffer, recovery_length);
        
        // Look for common EtherType values anywhere in the first part of the packet
        // 0x0800 = IP, 0x0806 = ARP, 0x86DD = IPv6
        int found_offset = -1;
        for (int i = 0; i < 20 && i + 1 < recovery_length; i++) {
            uint16_t val = (buffer[i] << 8) | buffer[i + 1];
            if (val == 0x0800 || val == 0x0806 || val == 0x86DD) {
                // Found potential EtherType - it should be at offset 12 from frame start
                // So frame start would be at (i - 12)
                int frame_start = i - 12;
                printf("[RX] Found EtherType 0x%04X at byte %d (frame would start at byte %d)\n", 
                       val, i, frame_start);
                
                // Accept frame start between -4 and +4 (since header might be 4 bytes)
                if (frame_start >= -4 && frame_start < 10) {
                    // Adjust for NE2000 header
                    if (frame_start < 0) {
                        printf("[RX] Frame seems to be missing %d bytes at start (NE2000 header issue)\n", -frame_start);
                        found_offset = 4;  // Start after 4-byte NE2000 header
                    } else {
                        found_offset = frame_start;
                    }
                    printf("[RX] Using offset %d as frame start\n", found_offset);
                    break;
                }
            }
        }
        
        if (found_offset >= 0) {
            printf("[RX] Extracting packet starting at offset %d!\n", found_offset);
            
            // Shift data to start of buffer if needed
            if (found_offset > 0) {
                for (int i = 0; i < recovery_length - found_offset; i++) {
                    buffer[i] = buffer[i + found_offset];
                }
                printf("[RX] Shifted packet data by %d bytes\n", found_offset);
                print_hex_dump("[RX] Aligned packet", buffer, 60);
            }
            
            // Advance BNRY and return the recovered data
            ne2000_write(NE2000_CR, 0x22);
            ne2000_write(NE2000_BNRY, next_packet_page);
            return recovery_length - found_offset;
        }
        
        printf("[RX] No valid ethernet frame found\n");
        ne2000_write(NE2000_CR, 0x22);
        ne2000_write(NE2000_BNRY, next_packet_page);
        return -1;
    }

    if (packet_length - 4 > buffer_size) {  // Subtract 4 byte CRC
        printf("Received packet too large: %d bytes (buffer: %d)\n", packet_length - 4, buffer_size);
        ne2000_write(NE2000_CR, 0x22);  // Page 0, Start, NoDMA
        ne2000_write(NE2000_BNRY, next_page - 1);
        return -1;
    }

    // Setup Remote DMA to read packet data (excluding 4-byte header we already read)
    uint16_t data_length = packet_length - 4;  // Subtract CRC
    ne2000_write(NE2000_CR, 0x22);              // Page 0, Start, NoDMA
    ne2000_write(NE2000_RBCR0, data_length & 0xFF);
    ne2000_write(NE2000_RBCR1, (data_length >> 8) & 0xFF);
    ne2000_write(NE2000_RSAR0, 4);              // Start after 4-byte header
    ne2000_write(NE2000_RSAR1, next_packet_page);
    ne2000_write(NE2000_CR, 0x0A);              // Page 0, Start, Remote Read

    // Read packet data
    for (uint16_t i = 0; i < data_length; i++) {
        buffer[i] = inb(io_base + NE2000_DATA);
    }

    // Wait for Remote DMA to complete with timeout
    timeout = 10000;
    while (!(ne2000_read(NE2000_ISR) & ISR_RDC) && timeout-- > 0);
    if (timeout <= 0) {
        printf("[RX] Timeout waiting for DMA (data)\n");
        return -1;
    }
    ne2000_write(NE2000_ISR, ISR_RDC);  // Clear RDC flag

    // Update Boundary Register to next_page - 1
    uint8_t new_boundary = next_page - 1;
    if (new_boundary < RX_START_PAGE) {
        new_boundary = RX_STOP_PAGE - 1;
    }
    // CRITICAL: Switch to Page 0 before writing BNRY!
    ne2000_write(NE2000_CR, 0x22);  // Page 0, Start, NoDMA
    ne2000_write(NE2000_BNRY, new_boundary);

    printf("Packet received, length: %d bytes (header status: 0x%02X)\n", data_length, status);
    print_hex_dump("[RX] Packet data", buffer, data_length > 64 ? 64 : data_length);
    
    return data_length;
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

void ne2000_get_mac_address(uint8_t *mac) {
    for (int i = 0; i < 6; i++) {
        mac[i] = mac_address[i];
    }
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

void print_hex_dump(const char* label, const uint8_t *data, uint16_t length) {
    printf("%s (%d bytes):\n", label, length);
    
    for (uint16_t i = 0; i < length; i += 16) {
        // Print offset
        printf("  %04X: ", i);
        
        // Print hex values
        for (uint16_t j = 0; j < 16; j++) {
            if (i + j < length) {
                printf("%02X ", data[i + j]);
            } else {
                printf("   ");
            }
            // Extra space in the middle
            if (j == 7) printf(" ");
        }
        
        printf(" | ");
        
        // Print ASCII values
        for (uint16_t j = 0; j < 16 && i + j < length; j++) {
            uint8_t c = data[i + j];
            if (c >= 32 && c <= 126) {
                printf("%c", c);
            } else {
                printf(".");
            }
        }
        
        printf("\n");
    }
}

void ne2000_dump_page(uint8_t page_num, uint16_t length) {
    printf("[DIAG] Dumping NE2000 page 0x%02X (%d bytes):\n", page_num, length);
    
    uint8_t buffer[256];
    if (length > 256) length = 256;
    
    // Setup Remote DMA to read from page
    ne2000_write(NE2000_CR, 0x22);              // Page 0, Start, NoDMA
    ne2000_write(NE2000_RBCR0, length & 0xFF);
    ne2000_write(NE2000_RBCR1, (length >> 8) & 0xFF);
    ne2000_write(NE2000_RSAR0, 0);              // Start at offset 0
    ne2000_write(NE2000_RSAR1, page_num);       // Page number
    ne2000_write(NE2000_CR, 0x0A);              // Start Remote Read
    
    // Read data
    for (uint16_t i = 0; i < length; i++) {
        buffer[i] = inb(io_base + NE2000_DATA);
    }
    
    // Wait for DMA complete
    int timeout = 10000;
    while (!(ne2000_read(NE2000_ISR) & ISR_RDC) && timeout-- > 0);
    ne2000_write(NE2000_ISR, ISR_RDC);
    
    print_hex_dump("Page content", buffer, length);
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

    printf("Testing NE2000 packet transmission...\n");
    
    // Try promiscuous mode to receive all packets
    ne2000_write(NE2000_RCR, 0x10);  // Promiscuous mode - accept all packets
    printf("Set promiscuous mode (RCR=0x10)\n");
    
    // DISABLE loopback - send real packet to network
    // Loopback mode doesn't work in QEMU emulation
    ne2000_write(NE2000_TCR, 0x00);  // Normal transmission mode
    printf("Loopback DISABLED - sending real packet to network\n");

    // Clear any pending interrupts
    ne2000_write(NE2000_ISR, 0xFF);
    
    // Read initial register values (CURR requires Page 1)
    ne2000_write(NE2000_CR, 0x62);  // Page 1
    uint8_t curr_before = ne2000_read(NE2000_CURR);
    ne2000_write(NE2000_CR, 0x22);  // Page 0
    uint8_t bnry_before = ne2000_read(NE2000_BNRY);
    uint8_t isr_before = ne2000_read(NE2000_ISR);
    printf("Before send - CURR: 0x%02X, BNRY: 0x%02X, ISR: 0x%02X\n",
           curr_before, bnry_before, isr_before);

    // Send the packet
    ne2000_send_packet(test_packet, sizeof(test_packet));

    // Longer delay to allow packet transmission and potential network response
    printf("Waiting for network activity...\n");
    for (volatile int i = 0; i < 1000000; i++);  // 10x longer delay

    // Check ISR for any activity
    uint8_t isr = ne2000_read(NE2000_ISR);
    printf("ISR after send: 0x%02X ", isr);
    if (isr & 0x01) printf("(PTX-PacketTX) ");
    if (isr & 0x02) printf("(PRX-PacketRX) ");
    if (isr & 0x04) printf("(RXE-RXError) ");
    if (isr & 0x08) printf("(TXE-TXError) ");
    if (isr & 0x10) printf("(OVW-Overwrite) ");
    if (isr & 0x20) printf("(CNT-Counter) ");
    if (isr & 0x40) printf("(RDC-RemoteDMA) ");
    if (isr & 0x80) printf("(RST-Reset) ");
    printf("\n");

    // Check CURR and BNRY to see if packets arrived
    ne2000_write(NE2000_CR, 0x62);  // Page 1
    uint8_t curr = ne2000_read(NE2000_CURR);
    ne2000_write(NE2000_CR, 0x22);  // Page 0
    uint8_t bnry = ne2000_read(NE2000_BNRY);
    printf("CURR: 0x%02X, BNRY: 0x%02X\n", curr, bnry);
    
    if (curr != bnry) {
        printf("Buffer has data! (CURR != BNRY)\n");
    } else {
        printf("Buffer empty (CURR == BNRY)\n");
    }

    // Try to receive ANY packets (not just our test packet)
    uint8_t received_packet[1500];
    int received_length = ne2000_receive_packet(received_packet, sizeof(received_packet));

    if (received_length > 0) {
        printf("Received %d bytes from network:\n", received_length);
        print_packet(received_packet, received_length);
    } else {
        printf("No packets received.\n");
        printf("NOTE: QEMU NE2000 loopback emulation may not work.\n");
        printf("Try: make run-net-dump to capture packets with Wireshark\n");
    }

    // Restore normal RCR (accept broadcasts)
    ne2000_write(NE2000_RCR, 0x04);
}

void ne2000_test_send() {
    // Send a simple test packet (broadcast)
    uint8_t test_packet[] = {
        // Destination MAC (broadcast)
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        // Source MAC (use NE2000 MAC)
        mac_address[0], mac_address[1], mac_address[2], mac_address[3], mac_address[4], mac_address[5],
        // EtherType (0x0800 = IPv4)
        0x08, 0x00,
        // Payload (dummy data - "TEST" in hex)
        0x54, 0x45, 0x53, 0x54, 0xDE, 0xAD, 0xBE, 0xEF
    };

    printf("Sending test packet (%d bytes)...\n", sizeof(test_packet));
    ne2000_send_packet(test_packet, sizeof(test_packet));
    
    // Brief delay to allow transmission
    for (volatile int i = 0; i < 100000; i++);
    
    printf("Packet sent successfully\n");
}


