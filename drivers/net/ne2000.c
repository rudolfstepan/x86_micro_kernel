#include "ne2000.h"
#include "lib/libc/stdio.h"
#include "lib/libc/stdlib.h"
#include "lib/libc/string.h"
#include "arch/x86/include/sys.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "drivers/bus/pci.h"
#include "drivers/char/io.h"
#include "drivers/net/netdev.h"
#include "arch/x86/include/interrupt.h"

#define NE2000_VENDOR_ID 0x10EC
#define NE2000_DEVICE_ID 0x8029

// Define NE2000 register offsets
#define NE2000_CR      0x00  // Command Register
#define NE2000_PSTART  0x01  // Page Start Register
#define NE2000_PSTOP   0x02  // Page Stop Register
#define NE2000_BNRY    0x03  // Boundary Pointer
#define NE2000_TSR     0x04  // Transmit Status Register (Read)
#define NE2000_TPSR    0x04  // Transmit Page Start Register (Write)
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
#define NE2000_RX_INTERRUPT_MASK 0x15

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
#define NE2000_MAX_FRAME_SIZE 1518

// I/O base address (to be set during runtime)
uint16_t io_base = 0xc000;// = 0x300;

uint8_t mac_address[MAC_ADDRESS_LENGTH] = {0};
static bool ne2000_initialized = false;
static volatile uint32_t ne2000_tx_busy;
static volatile bool ne2000_rx_pending;
static volatile uint8_t ne2000_rx_events;
static uint8_t ne2000_irq = PCI_IRQ_INVALID;

// prototypes
static int ne2000_receive_hardware_packet(uint8_t *buffer,
                                           uint16_t buffer_size);
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
        return 0xFF;
    }
    return inb(io_base + reg);
}

static void ne2000_release_io(uint8_t saved_imr, bool abort_hardware) {
    uint32_t flags = irq_save();
    if (abort_hardware) {
        /* Stop any stuck remote-DMA/transmit command before another RX/TX
         * path is allowed to reuse the shared 8390 registers. */
        ne2000_write(NE2000_CR, 0x21);  /* Stop, Page 0, NoDMA */
        ne2000_write(NE2000_RBCR0, 0);
        ne2000_write(NE2000_RBCR1, 0);
        ne2000_write(NE2000_ISR, ISR_RDC | ISR_PTX | 0x08u);
        ne2000_write(NE2000_CR, 0x22);  /* Start, Page 0, NoDMA */
    }
    __sync_lock_release(&ne2000_tx_busy);
    ne2000_write(NE2000_IMR, saved_imr);
    irq_restore(flags);
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
static bool ne2000_reset(void) {
    // Perform a software reset
    printf("Resetting NE2000 network card...\n");

    //ne2000_write(NE2000_CR, CR_STP | CR_RD2);
    // write the value of RESET into the RESET register
    ne2000_write(0x1F, ne2000_read(0x1F));

    // wait for the RESET to complete
    uint32_t timeout = 1000000u;
    while (!(ne2000_read(NE2000_ISR) & 0x80) && timeout > 0) {
        --timeout;
    }
    if (!(ne2000_read(NE2000_ISR) & 0x80)) {
        printf("NE2000 reset timeout.\n");
        return false;
    }

    // mask interrupts
    ne2000_write(NE2000_ISR, 0xFF);

    printf("NE2000 reset complete.\n");
    return true;
}

// Forward declarations
void ne2000_send_arp_reply(uint8_t *request_packet);

static bool ne2000_ring_has_packet(void) {
    ne2000_write(NE2000_CR, 0x62);  /* Page 1, Start, NoDMA */
    uint8_t current = ne2000_read(NE2000_CURR);
    ne2000_write(NE2000_CR, 0x22);  /* Page 0, Start, NoDMA */
    uint8_t boundary = ne2000_read(NE2000_BNRY);
    uint8_t next = (uint8_t)(boundary + 1u);
    if (next >= RX_STOP_PAGE) next = RX_START_PAGE;
    return next != current;
}

void ne2000_irq_handler() {
    /* TX and RX share the 8390 remote-DMA registers.  A receive interrupt
     * must not touch them while foreground TX owns the device.  TX restores
     * IMR afterwards, which retriggers the unacknowledged receive status. */
    if (!ne2000_initialized || io_base == 0) return;
    if (__atomic_load_n(&ne2000_tx_busy, __ATOMIC_ACQUIRE) != 0) {
        __atomic_store_n(&ne2000_rx_pending, true, __ATOMIC_RELEASE);
        return;
    }

    uint8_t status = inb((uint16_t)(io_base + NE2000_ISR));
    if (status == 0 || status == 0xFFu) return;
    uint8_t events = status & NE2000_RX_INTERRUPT_MASK;
    if (events == 0) return;

    __atomic_fetch_or(&ne2000_rx_events, events, __ATOMIC_RELEASE);
    __atomic_store_n(&ne2000_rx_pending, true, __ATOMIC_RELEASE);
    outb((uint16_t)(io_base + NE2000_ISR), events);
}

// Function to initialize the NE2000 card
void ne2000_init() {
    printf("Initializing NE2000 network card...\n");
    printf("IO base address: 0x%04X\n", io_base);

    // Reset the card
    if (!ne2000_reset()) return;

    // === Proper NE2000 Initialization Sequence ===
    
    // 1. Stop the NIC (CR = 0x21: Page 0, Stop, NoDMA)
    ne2000_write(NE2000_CR, 0x21);
    
    // 2. Set Data Configuration Register (DCR) - word mode for proper QEMU emulation
    // DCR bits: WTS=1 (word mode), BOS=0, LAS=0, LS=0, ARM=1, FT=01
    // NOTE: QEMU's NE2000 emulation works better in word mode
    ne2000_write(NE2000_DCR, 0x49);  // Word mode
    
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
    
    // 8. Keep interrupts masked until the handler is installed.
    ne2000_write(NE2000_IMR, 0x00);
    
    // 9. Read MAC address from PROM using Remote DMA
    // In byte mode (DCR=0x48), we need to read 32 bytes and extract every other byte
    // The PROM stores each MAC byte twice in word format
    ne2000_write(NE2000_CR, 0x21);      // Page 0, Stop, NoDMA
    ne2000_write(NE2000_RBCR0, 32);     // Read 32 bytes to get 6-byte MAC with doubling
    ne2000_write(NE2000_RBCR1, 0);
    ne2000_write(NE2000_RSAR0, 0);      // Start at PROM address 0
    ne2000_write(NE2000_RSAR1, 0);
    ne2000_write(NE2000_CR, 0x0A);      // Start Remote DMA Read
    
    // DCR_WTS selected word mode above.  Consume the complete 32-byte PROM
    // transfer (16 words); each of the first six words contains one duplicated
    // MAC byte on an NE2000-compatible 16-bit board.
    for (int i = 0; i < 16; i++) {
        uint16_t prom_word = inw(io_base + NE2000_DATA);
        if (i < MAC_ADDRESS_LENGTH) mac_address[i] = (uint8_t)prom_word;
    }
    
    // Wait for Remote DMA to complete with timeout
    int mac_timeout = 10000;
    while (!(ne2000_read(NE2000_ISR) & ISR_RDC) && mac_timeout-- > 0);
    bool invalid_mac = true;
    bool all_ff = true;
    for (int i = 0; i < MAC_ADDRESS_LENGTH; ++i) {
        if (mac_address[i] != 0) invalid_mac = false;
        if (mac_address[i] != 0xFF) all_ff = false;
    }
    invalid_mac = invalid_mac || all_ff || (mac_address[0] & 1u);
    if (mac_timeout <= 0 || invalid_mac) {
        printf("[WARN] Timeout reading MAC address, using default\n");
        // Use default MAC if read fails
        mac_address[0] = 0x52;
        mac_address[1] = 0x54;
        mac_address[2] = 0x00;
        mac_address[3] = 0x12;
        mac_address[4] = 0x34;
        mac_address[5] = 0x56;
    }
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

    // Register only a valid PIC index; never index irq_routines with 0xFF.
    if (!pci_irq_is_valid(ne2000_irq)) {
        printf("NE2000: invalid legacy IRQ %u\n", ne2000_irq);
        ne2000_write(NE2000_IMR, 0);
        return;
    }
    if (register_interrupt_handler(ne2000_irq, ne2000_irq_handler) != 0) {
        printf("NE2000: IRQ handler registration failed\n");
        ne2000_write(NE2000_IMR, 0);
        return;
    }

    ne2000_rx_events = 0;
    ne2000_rx_pending = false;
    ne2000_initialized = true;
    /* Any receive status accumulated while masked now safely triggers the
     * installed handler. */
    ne2000_write(NE2000_IMR, NE2000_RX_INTERRUPT_MASK);
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

bool ne2000_send_packet(uint8_t *data, uint16_t length) {
    if (!ne2000_initialized || !data || length < 14 || length > 1518) {
        printf("Packet too large to send: %d bytes\n", length);
        return false;
    }
    if (__sync_lock_test_and_set(&ne2000_tx_busy, 1u)) {
        printf("NE2000: another transmission is in progress\n");
        return false;
    }
    uint8_t saved_imr = ne2000_read(NE2000_IMR);
    ne2000_write(NE2000_IMR, 0);

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

    // 6. Write exactly send_length bytes in word mode, including padding.
    for (uint16_t i = 0; i < send_length; i += 2) {
        uint16_t word = i < length ? data[i] : 0;
        if ((uint16_t)(i + 1u) < length) word |= (uint16_t)data[i + 1u] << 8;
        outw((uint16_t)(io_base + NE2000_DATA), word);
    }

    // 7. Poll ISR register until bit 6 ("Remote DMA completed") is set
    int timeout = 10000;
    while (!(ne2000_read(NE2000_ISR) & ISR_RDC) && timeout-- > 0) {
        // Wait for Remote DMA to complete
    }
    
    if (timeout <= 0) {
        printf("[TX] Timeout waiting for Remote DMA complete\n");
        ne2000_release_io(saved_imr, true);
        return false;
    }

    // Clear "Remote DMA complete" bit
    ne2000_write(NE2000_ISR, ISR_RDC);

    // Start transmission
    ne2000_write(NE2000_TPSR, tx_page_start);    // Set Transmit Page Start
    ne2000_write(NE2000_TBCR0, send_length & 0xFF);  // Set Transmit Byte Count (low byte)
    ne2000_write(NE2000_TBCR1, (send_length >> 8));  // Set Transmit Byte Count (high byte)
    
    printf("[TX] Starting transmission: page=0x%02X, length=%d\n", tx_page_start, send_length);
    ne2000_write(NE2000_CR, 0x26);              // Start transmission (CR = 0x26 = Page 0, Start, TXP)

    // Wait for transmission complete (ISR_PTX = 0x02)
    timeout = 100000;  // Longer timeout for actual transmission
    while (!(ne2000_read(NE2000_ISR) & ISR_PTX) && timeout-- > 0) {
        // Wait for Packet Transmitted
    }

    if (timeout <= 0) {
        printf("[TX] Timeout waiting for packet transmission\n");
        // Check for errors
        uint8_t isr = ne2000_read(NE2000_ISR);
        printf("[TX] ISR at timeout: 0x%02X\n", isr);
        ne2000_release_io(saved_imr, true);
        return false;
    }

    // Clear transmission complete flag
    ne2000_write(NE2000_ISR, ISR_PTX);
    
    uint8_t tsr = ne2000_read(NE2000_TSR);  // Read Transmit Status Register
    printf("[TX] Transmission complete - TSR: 0x%02X\n", tsr);

    if ((tsr & 0x01u) == 0) {
        printf("[TX] Transmission completed with error (TSR=0x%02X)\n", tsr);
        ne2000_release_io(saved_imr, false);
        return false;
    }
    printf("Packet sent successfully, length: %d bytes\n", length);
    ne2000_release_io(saved_imr, false);
    return true;
}

// Send ARP reply
void ne2000_send_arp_reply(uint8_t *request_packet) {
    uint8_t arp_reply[60];  // Minimum ethernet frame size
    
    // Build ethernet header
    // Destination MAC: sender's MAC from the request (bytes 6-11 of request)
    for (int i = 0; i < 6; i++) {
        arp_reply[i] = request_packet[6 + i];  // Copy sender MAC to destination
    }
    
    // Source MAC: our MAC address (use the actual MAC we read from PROM)
    for (int i = 0; i < 6; i++) {
        arp_reply[6 + i] = mac_address[i];
    }
    
    // EtherType: ARP (0x0806)
    arp_reply[12] = 0x08;
    arp_reply[13] = 0x06;
    
    // ARP packet structure
    // Hardware type: Ethernet (0x0001)
    arp_reply[14] = 0x00;
    arp_reply[15] = 0x01;
    
    // Protocol type: IPv4 (0x0800)
    arp_reply[16] = 0x08;
    arp_reply[17] = 0x00;
    
    // Hardware address length: 6
    arp_reply[18] = 0x06;
    
    // Protocol address length: 4
    arp_reply[19] = 0x04;
    
    // Operation: Reply (0x0002)
    arp_reply[20] = 0x00;
    arp_reply[21] = 0x02;
    
    // Sender hardware address (our MAC)
    for (int i = 0; i < 6; i++) {
        arp_reply[22 + i] = mac_address[i];
    }
    
    // Sender protocol address (our IP: 10.0.2.15)
    arp_reply[28] = 10;
    arp_reply[29] = 0;
    arp_reply[30] = 2;
    arp_reply[31] = 15;
    
    // Target hardware address (requester's MAC from bytes 22-27 of request)
    for (int i = 0; i < 6; i++) {
        arp_reply[32 + i] = request_packet[22 + i];
    }
    
    // Target protocol address (requester's IP from bytes 28-31 of request)
    for (int i = 0; i < 4; i++) {
        arp_reply[38 + i] = request_packet[28 + i];
    }
    
    // Pad to minimum frame size
    for (int i = 42; i < 60; i++) {
        arp_reply[i] = 0;
    }
    
    printf("Sending ARP reply to %d.%d.%d.%d (MAC: %02X:%02X:%02X:%02X:%02X:%02X)\n",
           arp_reply[38], arp_reply[39], arp_reply[40], arp_reply[41],
           arp_reply[0], arp_reply[1], arp_reply[2], arp_reply[3], arp_reply[4], arp_reply[5]);
    
    // Show what we're sending
    print_hex_dump("[TX] ARP Reply", arp_reply, 60);
    
    ne2000_send_packet(arp_reply, 60);
}

static int ne2000_receive_hardware_packet(uint8_t *buffer,
                                           uint16_t buffer_size) {
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
        uint16_t w1 = inw(io_base + NE2000_DATA);
        uint16_t w2 = inw(io_base + NE2000_DATA);
        test_header[0] = w1 & 0xFF;
        test_header[1] = (w1 >> 8) & 0xFF;
        test_header[2] = w2 & 0xFF;
        test_header[3] = (w2 >> 8) & 0xFF;
        
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

    // Read packet header (4 bytes) in word mode
    uint8_t header[4];
    uint16_t word1 = inw(io_base + NE2000_DATA);
    uint16_t word2 = inw(io_base + NE2000_DATA);
    header[0] = word1 & 0xFF;
    header[1] = (word1 >> 8) & 0xFF;
    header[2] = word2 & 0xFF;
    header[3] = (word2 >> 8) & 0xFF;

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
        uint16_t recovery_length = buffer_size < 64u ? buffer_size : 64u;
        if (recovery_length < 14u) {
            ne2000_write(NE2000_CR, 0x22);
            ne2000_write(NE2000_BNRY, next_packet_page);
            return -1;
        }
        
        ne2000_write(NE2000_CR, 0x22);
        ne2000_write(NE2000_RBCR0, recovery_length & 0xFF);
        ne2000_write(NE2000_RBCR1, (recovery_length >> 8) & 0xFF);
        ne2000_write(NE2000_RSAR0, 0);  // Start at offset 0 (include header)
        ne2000_write(NE2000_RSAR1, next_packet_page);
        ne2000_write(NE2000_CR, 0x0A);
        
        for (uint16_t i = 0; i + 1 < recovery_length; i += 2) {
            uint16_t word = inw(io_base + NE2000_DATA);
            buffer[i] = word & 0xFF;
            buffer[i + 1] = (word >> 8) & 0xFF;
        }
        if (recovery_length & 1) {
            uint16_t word = inw(io_base + NE2000_DATA);
            buffer[recovery_length - 1] = word & 0xFF;
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
                uint16_t aligned_length = (uint16_t)(recovery_length - found_offset);
                print_hex_dump("[RX] Aligned packet", buffer,
                               aligned_length < 60u ? aligned_length : 60u);
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

    // Read packet data in word mode (16-bit transfers)
    for (uint16_t i = 0; i + 1 < data_length; i += 2) {
        uint16_t word = inw(io_base + NE2000_DATA);
        buffer[i] = word & 0xFF;
        buffer[i + 1] = (word >> 8) & 0xFF;
    }
    // Read last byte if length is odd
    if (data_length & 1) {
        uint16_t word = inw(io_base + NE2000_DATA);
        buffer[data_length - 1] = word & 0xFF;
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

int ne2000_receive_packet(uint8_t *buffer, uint16_t buffer_size) {
    if (!ne2000_initialized) return -1;
    return netdev_receive_frame(buffer, buffer_size);
}

static void ne2000_recover_rx_overrun(void) {
    printf("[WARNING] NE2000 RX buffer overrun; resetting receive ring\n");
    ne2000_write(NE2000_CR, 0x21);  /* Stop, Page 0, NoDMA */
    ne2000_write(NE2000_RBCR0, 0);
    ne2000_write(NE2000_RBCR1, 0);
    ne2000_write(NE2000_BNRY, RX_START_PAGE);
    ne2000_write(NE2000_CR, 0x62);  /* Start, Page 1, NoDMA */
    ne2000_write(NE2000_CURR, RX_START_PAGE + 1);
    ne2000_write(NE2000_CR, 0x22);  /* Start, Page 0, NoDMA */
}

void ne2000_poll_rx(void) {
    if (!ne2000_initialized ||
        !__atomic_load_n(&ne2000_rx_pending, __ATOMIC_ACQUIRE)) return;
    if (__sync_lock_test_and_set(&ne2000_tx_busy, 1u)) return;

    uint8_t saved_imr = ne2000_read(NE2000_IMR);
    ne2000_write(NE2000_IMR, 0);
    __atomic_store_n(&ne2000_rx_pending, false, __ATOMIC_RELEASE);

    uint8_t events = __atomic_exchange_n(&ne2000_rx_events, 0,
                                          __ATOMIC_ACQ_REL);
    events |= ne2000_read(NE2000_ISR) & NE2000_RX_INTERRUPT_MASK;
    if (events != 0) ne2000_write(NE2000_ISR, events);
    if (events & 0x10u) ne2000_recover_rx_overrun();

    const unsigned int budget = RX_STOP_PAGE - RX_START_PAGE;
    unsigned int processed = 0;
    while (processed < budget) {
        uint8_t packet[NE2000_MAX_FRAME_SIZE];
        int length = ne2000_receive_hardware_packet(packet, sizeof(packet));
        if (length <= 0) break;
        netdev_deliver_rx(packet, (uint16_t)length);
        ++processed;
    }
    ne2000_write(NE2000_ISR, 0x05u); /* PRX and RX error only */
    ne2000_rx_pending = processed == budget || ne2000_ring_has_packet();
    ne2000_release_io(saved_imr, false);
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
                    ne2000_irq = pci_get_irq((uint8_t)bus, device, function);
                    if (!pci_irq_is_valid(ne2000_irq)) {
                        printf("NE2000: invalid IRQ line %u\n", ne2000_irq);
                        io_base = 0;
                        return;
                    }
                    uint16_t command = pci_read_config_word((uint8_t)bus, device, function, PCI_COMMAND);
                    pci_write_config_word((uint8_t)bus, device, function, PCI_COMMAND,
                                          (uint16_t)(command | 0x0001u));
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


