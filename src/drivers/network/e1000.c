#include "e1000.h"
#include "toolchain/stdio.h"
#include "toolchain/stdlib.h"
#include "toolchain/string.h"
#include "drivers/pci.h"
#include "drivers/io/io.h"
#include "kernel/sys.h"
#include "kernel/memory.h"


// PCI Configuration Constants
#define PCI_CONFIG_ADDRESS              0xCF8
#define PCI_CONFIG_DATA                 0xCFC

// E1000 PCI Device IDs
#define E1000_VENDOR_ID                 0x8086
#define E1000_DEVICE_ID                 0x100E

// E1000 MMIO Base Address (to be set via PCI configuration)
#define E1000_MMIO_BASE                 0xF0000000

// E1000 Register Offsets
#define E1000_REG_CTRL                  0x0000      // Device Control
#define E1000_REG_STATUS                0x0008      // Device Status
#define E1000_REG_RCTL                  0x0100      // Receive Control
#define E1000_REG_TCTL                  0x0400      // Transmit Control
#define E1000_REG_TIPG                  0x0410      // Transmit Inter-Packet Gap
#define E1000_REG_RDBAL                 0x2800      // Receive Descriptor Base Low
#define E1000_REG_RDBAH                 0x2804      // Receive Descriptor Base High
#define E1000_REG_RDLEN                 0x2808      // Receive Descriptor Length
#define E1000_REG_RDH                   0x2810      // Receive Descriptor Head
#define E1000_REG_RDT                   0x2818      // Receive Descriptor Tail
#define E1000_REG_TDBAL                 0x3800      // Transmit Descriptor Base Low
#define E1000_REG_TDBAH                 0x3804      // Transmit Descriptor Base High
#define E1000_REG_TDLEN                 0x3808      // Transmit Descriptor Length
#define E1000_REG_TDH                   0x3810      // Transmit Descriptor Head
#define E1000_REG_TDT                   0x3818      // Transmit Descriptor Tail
#define E1000_REG_ICR                   0x00C0      // Interrupt Cause Read
#define E1000_REG_IMS                   0x00D0      // Interrupt Mask Set
#define E1000_REG_ICS                   0x00C8      // Interrupt Cause Set
#define E1000_REG_TPT                   0x040D4     // Total Packets Transmitted

// Control Register Bits
#define E1000_CTRL_RST                  (1 << 26)   // Device Reset
#define E1000_CTRL_PHY_RST              (1 << 31)   // PHY Reset

// Status Register Bits
#define E1000_STATUS_LINK_UP            (1 << 1)    // Link Up

// Receive Control (RCTL) Bits
#define E1000_RCTL_EN                   (1 << 1)    // Receiver Enable
#define E1000_RCTL_SBP                  (1 << 2)    // Store Bad Packets
#define E1000_RCTL_UPE                  (1 << 3)    // Unicast Promiscuous Enable
#define E1000_RCTL_MPE                  (1 << 4)    // Multicast Promiscuous Enable
#define E1000_RCTL_LBM_NONE             (0 << 6)    // No Loopback
#define E1000_RCTL_LBM_MAC              (1 << 6)    // MAC Loopback
#define E1000_RCTL_BAM                  (1 << 15)   // Broadcast Accept Mode
#define E1000_RCTL_BSIZE_2048           (0 << 16)   // Buffer Size: 2048 Bytes
#define E1000_RCTL_BSIZE_4096           ((3 << 16) | (1 << 25)) // 4096 Bytes
#define E1000_RCTL_BSIZE_8192           ((2 << 16) | (1 << 25)) // 8192 Bytes
#define E1000_RCTL_BSIZE_16384          ((1 << 16) | (1 << 25)) // 16384 Bytes
#define E1000_RCTL_SECRC                (1 << 26)   // Strip Ethernet CRC

// Transmit Control (TCTL) Bits
#define E1000_TCTL_EN                   (1 << 1)    // Transmitter Enable
#define E1000_TCTL_PSP                  (1 << 3)    // Pad Short Packets
#define E1000_TCTL_CT_SHIFT             4           // Collision Threshold Shift
#define E1000_TCTL_COLD_SHIFT           12          // Collision Distance Shift
#define E1000_TCTL_RTLC                 (1 << 24)   // Retransmit on Late Collision

// Transmit Descriptor Command Bits
#define E1000_TXD_CMD_EOP               (1 << 0)    // End of Packet
#define E1000_TXD_CMD_IFCS              (1 << 1)    // Insert Frame Check Sequence
#define E1000_TXD_CMD_RS                (1 << 3)    // Report Status
#define E1000_TXD_CMD_RPS               (1 << 4)    // Report Packet Sent

// Transmit Descriptor Status Bits
#define E1000_TXD_STAT_DD               (1 << 0)    // Descriptor Done

// Interrupt Mask Bits
#define E1000_IMS_RXT0                  (1 << 7)    // Receive Timer Interrupt

// Descriptor Ring Sizes
#define E1000_NUM_RX_DESC               32          // Number of RX Descriptors
#define E1000_NUM_TX_DESC               8           // Number of TX Descriptors

// PIC Constants
#define PIC1_COMMAND                    0x20
#define PIC1_DATA                       0x21
#define PIC2_COMMAND                    0xA0
#define PIC2_DATA                       0xA1


#define RX_BUFFER_SIZE 8192   // Size of each RX buffer



uint32_t *e1000_mmio = NULL;
uint8_t device_irq = 11;

struct e1000_rx_desc rx_descs[E1000_NUM_RX_DESC]; // Receive Descriptor Buffers
struct e1000_tx_desc tx_descs[E1000_NUM_TX_DESC]; // Transmit Descriptor Buffers

uint16_t rx_cur = 0;      // Current Receive Descriptor Buffer
uint16_t tx_cur = 0;      // Current Transmit Descriptor Buffer
uint8_t old_cur;

// Buffers for RX and TX
void *rx_buffers[E1000_NUM_RX_DESC]; // Array to hold RX buffer addresses



// Read a 32-bit register
static inline uint32_t e1000_read_reg(uint32_t offset) {
    return e1000_mmio[offset / 4];
}

// Write a 32-bit register
static inline void e1000_write_reg(uint32_t offset, uint32_t value) {
    e1000_mmio[offset / 4] = value;
}

void e1000_enable_interrupts() {
    e1000_write_reg(E1000_REG_IMS ,0x1F6DC);
    e1000_write_reg(E1000_REG_IMS ,0xff & ~4);
    e1000_read_reg(0xc0);
}

void e1000_enable_loopback() {
    uint32_t rctl = 0;
    rctl |= E1000_RCTL_EN;           // Enable receiver
    rctl |= E1000_RCTL_LBM_MAC; // Enable MAC loopback mode
    rctl |= E1000_RCTL_BAM;          // Accept broadcast packets
    rctl |= E1000_RCTL_UPE;         // Unicast Promiscuous Enable
    rctl |= E1000_RCTL_MPE;         // Multicast Promiscuous Enable
    rctl |= E1000_RCTL_BSIZE_8192;   // Set buffer size to 8192 bytes
    e1000_write_reg(E1000_REG_RCTL, rctl);
}

void check_received_packet() {
    static uint32_t current_tail = 0;
    printf("Packets received.\n");

}

void e1000_isr() {
    uint32_t icr = e1000_read_reg(E1000_REG_ICR);
    printf("ICR after TX interrupt: 0x%x\n", icr);
    e1000_write_reg(E1000_REG_ICR, icr); // Acknowledge interrupt

    if (icr & (1 << 7)) { // RXDMT (Receive Descriptor Minimum Threshold Reached)
        check_received_packet();
    }

    if (icr & (1 << 1)) { // TXDW (Transmit Descriptor Written Back)
        printf("Transmit interrupt triggered.\n");

        // Debugging only: Manually update the status
        //tx_descs[old_cur].status = 0xFF;
    }

    if (icr & (1 << 6)) { // RXO (Receiver Overrun)
        printf("Receiver overrun occurred.\n");
    }
}

// Function to initialize rings and buffers
void initialize_rings_and_buffers() {
    // Initialize RX descriptors and buffers
    for (int i = 0; i < E1000_NUM_RX_DESC; i++) {
        // Allocate RX buffer with proper alignment
        rx_buffers[i] = aligned_alloc(16, RX_BUFFER_SIZE);
        if (!rx_buffers[i]) {
            printf("Failed to allocate RX buffer %d\n", i);
            exit(1); // Handle allocation failure
        }

        // Initialize RX descriptor
        rx_descs[i].buffer_addr = (uint64_t)rx_buffers[i];
        rx_descs[i].length = 0;
        rx_descs[i].status = 0; // Descriptor not yet ready
        rx_descs[i].errors = 0;
        rx_descs[i].special = 0;
    }

    printf("RX ring initialized with %d descriptors.\n", E1000_NUM_RX_DESC);

    // Initialize TX descriptors
    for (int i = 0; i < E1000_NUM_TX_DESC; i++) {
        tx_descs[i].buffer_addr = 0;  // No buffer initially
        tx_descs[i].length = 0;
        tx_descs[i].cso = 0;
        tx_descs[i].cmd = 0;
        tx_descs[i].status = 0xFF;   // Mark as available (e.g., TSTA_DD)
        tx_descs[i].css = 0;
        tx_descs[i].special = 0;
    }

    printf("TX ring initialized with %d descriptors.\n", E1000_NUM_TX_DESC);
}

void process_packet(void *packet, size_t length) {
    // Process the packet here
    printf("Received packet: %.*s\n", length, (char *)packet);
}

void e1000_init(uint8_t bus, uint8_t device, uint8_t function) {
    // Get BAR0 (Base Address Register 0)
    uint32_t bar0 = pci_read(bus, device, function, 0x10) & ~0xF;
    e1000_mmio = (uint32_t *)bar0;

    // IRQ für das Gerät auslesen
    device_irq = pci_get_irq(bus, device, function);
    printf("E1000 found: Bus %u, Device %u, IRQ %u, Function %u\n", bus, device, device_irq, function);

    // Enable bus mastering
    pci_set_bus_master(bus, device, 1);

    // Step 2: Perform a device reset
    printf("Performing E1000 hardware reset...\n");

    // Write to the CTRL register to initiate a device reset
    uint32_t ctrl = e1000_read_reg(E1000_REG_CTRL);
    ctrl |= E1000_CTRL_RST; // Set the RST bit
    e1000_write_reg(E1000_REG_CTRL, ctrl);

    // Wait for the reset to complete (the RST bit clears when done)
    delay_ms(10); // 10ms delay to allow reset to complete

    ctrl = e1000_read_reg(E1000_REG_CTRL);
    if (ctrl & E1000_CTRL_RST) {
        printf("Error: E1000 reset did not complete.\n");
        return;
    }
    printf("E1000 reset complete.\n");

    // Step 3: Ensure the device is powered on and enabled
    printf("Ensuring device is enabled and powered on...\n");
    ctrl = e1000_read_reg(E1000_REG_CTRL);
    ctrl &= ~E1000_CTRL_PHY_RST; // Clear PHY_RST to power on the device
    e1000_write_reg(E1000_REG_CTRL, ctrl);

    // Step 4: Verify the device is ready
    uint32_t status = e1000_read_reg(E1000_REG_STATUS);
    if (!(status & 0x1)) { // Check if the device is not ready
        printf("Error: E1000 device not ready.\n");
        return;
    }
    printf("E1000 device is ready and powered on.\n");

    initialize_rings_and_buffers();
    e1000_enable_interrupts();
    register_interrupt_handler(device_irq, e1000_isr);

    // 1. Verify Transmit Engine Configuration
    uint32_t tctl = e1000_read_reg(E1000_REG_TCTL);
    if (!(tctl & E1000_TCTL_EN)) {
        printf("Error: Transmit engine not enabled. Enabling now...\n");
        uint32_t tctl = 0;
        tctl |= E1000_TCTL_EN;           // Enable transmitter
        tctl |= E1000_TCTL_PSP;          // Pad short packets
        tctl |= (15 << E1000_TCTL_CT_SHIFT);  // Collision threshold
        tctl |= (64 << E1000_TCTL_COLD_SHIFT); // Collision distance
        e1000_write_reg(E1000_REG_TCTL, tctl);
    }

    e1000_write_reg(E1000_REG_TCTL, 0b0110000000000111111000011111010);
    //e1000_write_reg(E1000_REG_TIPG, 0x0060200A); // Typical default for gigabit Ethernet

    printf("E1000 initialized.\n");
}

void e1000_receive_packet() {
    uint32_t tail = e1000_read_reg(E1000_REG_RDT);

    while (true) {
        uint32_t head = e1000_read_reg(E1000_REG_RDH);
        if (head == tail) {
            break; // No more packets
        }

        // struct e1000_rx_desc *desc = &receive_ring[head];
        // if (desc->status & 0x1) { // Descriptor Done
        //     process_packet((void *)desc->buffer_addr, desc->length);
        //     desc->status = 0;
        // }

        // tail = (tail + 1) % RECEIVE_RING_SIZE;
        // e1000_write_reg(E1000_REG_RDT, tail);
    }
}

void get_mac_address() {
    uint8_t mac[6];
    for (int i = 0; i < 6; i++) {
        mac[i] = e1000_mmio[i];
    }
    printf("MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void e1000_detect(){
    printf("Detecting E1000 network card...\n");
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
                if ((id & 0xFFFF) == E1000_VENDOR_ID && ((id >> 16) & 0xFFFF) == E1000_DEVICE_ID) {

                    //pci_set_irq(bus, device, function, 10);
                    e1000_init(bus, device, function);
                    get_mac_address();

                    return;
                }
            }
        }
    }
}


void e1000_test_registers() {
    printf("Testing E1000 register configurations...\n");

    // Device Control and Status
    uint32_t ctrl = e1000_read_reg(E1000_REG_CTRL);
    uint32_t status = e1000_read_reg(E1000_REG_STATUS);
    printf("CTRL: 0x%08X | STATUS: 0x%08X\n", ctrl, status);

    // Receive Control
    uint32_t rctl = e1000_read_reg(E1000_REG_RCTL);
    printf("RCTL: 0x%08X\n", rctl);

    // Transmit Control
    uint32_t tctl = e1000_read_reg(E1000_REG_TCTL);
    uint32_t tipg = e1000_read_reg(E1000_REG_TIPG);
    printf("TCTL: 0x%08X | TIPG: 0x%08X\n", tctl, tipg);

    // Descriptor Ring Base Addresses
    uint32_t rdbal = e1000_read_reg(E1000_REG_RDBAL);
    uint32_t rdbah = e1000_read_reg(E1000_REG_RDBAH);
    uint32_t rdlen = e1000_read_reg(E1000_REG_RDLEN);
    uint32_t tdbal = e1000_read_reg(E1000_REG_TDBAL);
    uint32_t tdbah = e1000_read_reg(E1000_REG_TDBAH);
    uint32_t tdlen = e1000_read_reg(E1000_REG_TDLEN);
    printf("RDBAL: 0x%08X | RDBAH: 0x%08X | RDLEN: 0x%08X\n", rdbal, rdbah, rdlen);
    printf("TDBAL: 0x%08X | TDBAH: 0x%08X | TDLEN: 0x%08X\n", tdbal, tdbah, tdlen);

    // Descriptor Head and Tail
    uint32_t rdh = e1000_read_reg(E1000_REG_RDH);
    uint32_t rdt = e1000_read_reg(E1000_REG_RDT);
    uint32_t tdh = e1000_read_reg(E1000_REG_TDH);
    uint32_t tdt = e1000_read_reg(E1000_REG_TDT);
    printf("RDH: 0x%08X | RDT: 0x%08X\n", rdh, rdt);
    printf("TDH: 0x%08X | TDT: 0x%08X\n", tdh, tdt);

    // Interrupt Mask and Cause Registers
    uint32_t ims = e1000_read_reg(E1000_REG_IMS);
    uint32_t icr = e1000_read_reg(E1000_REG_ICR);
    printf("IMS: 0x%08X | ICR: 0x%08X\n", ims, icr);

    // Link and PHY Status
    if (status & E1000_STATUS_LINK_UP) {
        printf("Link is up.\n");
    } else {
        printf("Link is down.\n");
    }

    // Verify Descriptor Alignment
    if ((uint64_t)rx_descs % 16 != 0 || (uint64_t)tx_descs % 16 != 0) {
        printf("Error: Descriptors are not aligned to 16-byte boundaries!\n");
    } else {
        printf("Descriptors are correctly aligned.\n");
    }

    // Additional Debugging for Transmit and Receive Buffers
    for (int i = 0; i < E1000_NUM_RX_DESC; i++) {
        printf("RX Desc %d: Buffer Addr: %p, Status: %u\n", i, rx_descs[i].buffer_addr, rx_descs[i].status);
    }

    for (int i = 0; i < E1000_NUM_TX_DESC; i++) {
        printf("TX Desc %d: Buffer Addr: %p, Length: %u, Status: %u\n", i, tx_descs[i].buffer_addr, tx_descs[i].length, tx_descs[i].status);
    }

    printf("E1000 register configuration test complete.\n");
}

void e1000_send_packet(void *packet, size_t length) {

    e1000_test_registers();

    wait_enter_pressed();

    printf("Sending packet of length %u...\n", length);

    // 2. Inspect Descriptor Tail Update
    printf("TX Tail before send: %d\n", e1000_read_reg(E1000_REG_TDT));
    

    // 3. Check Transmit Descriptor Ring Initialization
    for (int i = 0; i < E1000_NUM_TX_DESC; i++) {
        printf("TX Desc %d: Buffer Addr: %p, Length: %u, Status: %u\n", i, tx_descs[i].buffer_addr, tx_descs[i].length, tx_descs[i].status);
    }

    // 4. Inspect Head Pointer Progress
    uint32_t head = e1000_read_reg(E1000_REG_TDH);
    uint32_t tail = e1000_read_reg(E1000_REG_TDT);

    printf("TX Head: %d, TX Tail: %d\n", head, tail);

    // 5. Check Transmit Command (TCTL) Configuration
    e1000_write_reg(E1000_REG_TCTL, E1000_TCTL_EN | E1000_TCTL_PSP | (15 << E1000_TCTL_CT_SHIFT) | (64 << E1000_TCTL_COLD_SHIFT));

    // 6. Ensure Loopback Configuration (If Enabled)
    e1000_enable_loopback();

    // Check alignment
    if ((uint64_t)tx_descs % 16 != 0) {
        printf("Error: TX Descriptors not 16-byte aligned!\n");
        return;
    }

    // Set up descriptor
    tx_descs[tx_cur].buffer_addr = (uint64_t)packet;
    tx_descs[tx_cur].length = (uint16_t)length;
    tx_descs[tx_cur].cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS;
    tx_descs[tx_cur].status = 0;

    // Debug the descriptor
    printf("Descriptor %d: Buffer Addr: %p, Status: %u\n", tx_cur, tx_descs[tx_cur].buffer_addr, tx_descs[tx_cur].status);

    // Update the tail register to signal the hardware
    old_cur = tx_cur;
    tx_cur = (tx_cur + 1) % E1000_NUM_TX_DESC;
    e1000_write_reg(E1000_REG_TDT, tx_cur);

    printf("TX Tail after send: %d\n", e1000_read_reg(E1000_REG_TDT));

    // Wait for transmission completion with timeout
    int timeout = 1000;
    while (!(tx_descs[old_cur].status & 0xff)) {
        if (--timeout == 0) {
            printf("Timeout! Descriptor %d status: 0x%x\n", old_cur, tx_descs[old_cur].status);
            printf("TX Head: %d, TX Tail: %d\n", e1000_read_reg(E1000_REG_TDH), e1000_read_reg(E1000_REG_TDT));
            return;
        }
        delay_ms(10); // Prevent tight busy-looping
    }

    uint32_t tpt = e1000_read_reg(E1000_REG_TPT);
    printf("Total Packets Transmitted: %u\n", tpt);


    // Debug successful transmission
    printf("Packet sent successfully. Descriptor %d status: 0x%x\n",
           old_cur, tx_descs[old_cur].status);
}

void e1000_send_test_packet() {

    uint8_t dest_mac[6] = {0x52,0x54,0x00,0x12,0x34,0x57};
    uint8_t src_mac[6] = {0x40,0x00,0x83,0x00,0x88,0x00};
    //uint8_t dest_mac[6] ={0x40,0x00,0x83,0x00,0x88,0x00};

    // create an ethernet frame
    uint8_t data[] = {
        // Destination MAC
        dest_mac[0], dest_mac[1], dest_mac[2], dest_mac[3], dest_mac[4], dest_mac[5],
        // Source MAC
        src_mac[0], src_mac[1], src_mac[2], src_mac[3], src_mac[4], src_mac[5],
        // EtherType (IPv4)
        0x08, 0x00,
        // Payload (ICMP Echo Request)
        0x45, 0x00, 0x00, 0x54, 0x00, 0x00, 0x40, 0x00, 0x40, 0x01, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x01,
        0x0A, 0x00, 0x00, 0x02, 0x08, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02};

    e1000_send_packet(&data, sizeof(data));
}
