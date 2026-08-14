#include "e1000.h"
#include "lib/libc/stdio.h"
#include "lib/libc/stdlib.h"
#include "lib/libc/string.h"
#include "drivers/bus/pci.h"
#include "drivers/char/io.h"
#include "drivers/net/netdev.h"
#include "arch/x86/include/sys.h"
#include "mm/kmalloc.h"
#include "kernel/time/pit.h"


// PCI Configuration Constants
#define PCI_CONFIG_ADDRESS              0xCF8
#define PCI_CONFIG_DATA                 0xCFC

// E1000 PCI Device IDs
#define E1000_VENDOR_ID                 0x8086
#define E1000_DEVICE_ID_82540EM         0x100E
#define E1000_DEVICE_ID_82545EM         0x100F

// E1000 MMIO Base Address (to be set via PCI configuration)
#define E1000_MMIO_BASE                 0xF0000000
#define E1000_MMIO_SIZE                 0x20000U

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
#define E1000_REG_TXDCTL                0x3828      // Transmit Descriptor Control
#define E1000_REG_ICR                   0x00C0      // Interrupt Cause Read
#define E1000_REG_IMS                   0x00D0      // Interrupt Mask Set
#define E1000_REG_IMC                   0x00D8      // Interrupt Mask Clear
#define E1000_REG_ICS                   0x00C8      // Interrupt Cause Set
#define E1000_REG_TPT                   0x040D4     // Total Packets Transmitted
#define E1000_REG_RAL                   0x5400      // Receive Address Low
#define E1000_REG_RAH                   0x5404      // Receive Address High

// Control Register Bits
#define E1000_CTRL_FD                   (1 << 0)    // Full duplex
#define E1000_CTRL_SLU                  (1 << 6)    // Set Link Up
#define E1000_CTRL_SPEED_MASK           (3 << 8)
#define E1000_CTRL_SPEED_1000           (2 << 8)
#define E1000_CTRL_FRCSPD               (1 << 11)   // Force Speed
#define E1000_CTRL_FRCDPLX              (1 << 12)   // Force Duplex
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
#define E1000_TXD_CMD_IC                (1 << 2)    // Insert Checksum
#define E1000_TXD_CMD_RS                (1 << 3)    // Report Status
#define E1000_TXD_CMD_RPS               (1 << 4)    // Report Packet Sent
#define E1000_TXD_CMD_IDE               (1 << 7)    // Interrupt Delay Enable

// Transmit Descriptor Status Bits
#define E1000_TXD_STAT_DD               (1 << 0)    // Descriptor Done

// Interrupt Cause/Mask Bits
#define E1000_INT_TXDW                  (1u << 0)   // TX descriptor written back
#define E1000_INT_LSC                   (1u << 2)   // Link status changed
#define E1000_INT_RXO                   (1u << 6)   // Receiver overrun
#define E1000_INT_RXT0                  (1u << 7)   // Receive timer interrupt

// Receive Descriptor Status Bits
#define E1000_RXD_STAT_DD               (1u << 0)   // Descriptor done
#define E1000_RXD_STAT_EOP              (1u << 1)   // End of packet

// Descriptor Ring Sizes
#define E1000_NUM_RX_DESC               32          // Number of RX Descriptors
#define E1000_NUM_TX_DESC               8           // Number of TX Descriptors

// PIC Constants
#define PIC1_COMMAND                    0x20
#define PIC1_DATA                       0x21
#define PIC2_COMMAND                    0xA0
#define PIC2_DATA                       0xA1


#define RX_BUFFER_SIZE 8192   // Size of each RX buffer
#define E1000_RX_DESCRIPTOR_DROPPED (-2)

typedef struct {
    volatile uint32_t *mmio_base;     // MMIO base address
    uint32_t irq;                     // IRQ number
} e1000_device_t;


e1000_device_t e1000_device = {0};

struct e1000_rx_desc rx_descs[E1000_NUM_RX_DESC] __attribute__((aligned(16)));
struct e1000_tx_desc tx_descs[E1000_NUM_TX_DESC] __attribute__((aligned(16)));

uint16_t rx_cur = 0;      // Current Receive Descriptor Buffer
uint16_t tx_cur = 0;      // Current Transmit Descriptor Buffer
uint8_t old_cur;

// Buffers for RX and TX
void *rx_buffers[E1000_NUM_RX_DESC]; // Array to hold RX buffer addresses

static uint8_t rx_buffer_storage[E1000_NUM_RX_DESC][RX_BUFFER_SIZE]
    __attribute__((aligned(16)));
static uint8_t tx_buffer_storage[E1000_NUM_TX_DESC][2048]
    __attribute__((aligned(16)));
static uint8_t rx_delivery_buffer[2048] __attribute__((aligned(16)));
static bool e1000_initialized = false;
static volatile uint32_t e1000_tx_busy;
static volatile uint32_t e1000_pending_events;

typedef struct {
    volatile uint32_t irq_total;
    volatile uint32_t irq_rx;
    volatile uint32_t irq_tx;
    volatile uint32_t irq_overrun;
    volatile uint32_t irq_link_change;
    volatile uint32_t last_icr;
    volatile uint32_t link_up;
    volatile uint32_t rx_packets;
    volatile uint32_t rx_invalid_length;
    volatile uint32_t rx_descriptor_errors;
    volatile uint32_t tx_packets;
    volatile uint32_t tx_errors;
} e1000_stats_t;

static e1000_stats_t e1000_stats;

// Static TX packet buffer (must be in kernel data section for DMA)
static uint8_t tx_packet_buffer[2048] __attribute__((aligned(16)));

// Read a 32-bit register
static inline uint32_t e1000_read_reg(uint32_t offset) {
    return e1000_device.mmio_base[offset / 4];
}

// Write a 32-bit register
static inline void e1000_write_reg(uint32_t offset, uint32_t value) {
    e1000_device.mmio_base[offset / 4] = value;
}

// Forward declarations
void e1000_send_arp_reply(uint8_t *request_packet);
static void e1000_drain_rx(void);

void e1000_enable_interrupts() {
    /* TX completion is polled synchronously, so TXDW would only create an
     * unnecessary interrupt. RX and link events are all that are needed. */
    uint32_t ims = E1000_INT_LSC | E1000_INT_RXO | E1000_INT_RXT0;
    
    e1000_write_reg(E1000_REG_IMC, 0xFFFFFFFFu);
    // Clear causes while still masked, then drain descriptors which may have
    // completed before the handler was installed.
    uint32_t icr = e1000_read_reg(E1000_REG_ICR);
    if (icr) {
        printf("E1000: Cleared pending interrupts (ICR=0x%08X)\n", icr);
    }
    e1000_drain_rx();
    e1000_write_reg(E1000_REG_IMS, ims);
    printf("E1000: Interrupts enabled (IMS=0x%08X)\n", ims);
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

static void e1000_drain_rx(void) {
    for (unsigned int processed = 0; processed < E1000_NUM_RX_DESC;
         ++processed) {
        int length = e1000_receive_packet(rx_delivery_buffer,
                                          sizeof(rx_delivery_buffer));
        if (length == E1000_RX_DESCRIPTOR_DROPPED) continue;
        if (length <= 0) break;
        netdev_deliver_rx(rx_delivery_buffer, (uint16_t)length);
        ++e1000_stats.rx_packets;
    }
}

static void e1000_isr(Registers *regs) {
    (void)regs;
    uint32_t icr = e1000_read_reg(E1000_REG_ICR);
    if (icr == 0 || icr == 0xFFFFFFFFu) return;

    ++e1000_stats.irq_total;
    e1000_stats.last_icr = icr;

    if (icr & E1000_INT_RXO) ++e1000_stats.irq_overrun;
    if (icr & E1000_INT_RXT0) ++e1000_stats.irq_rx;
    if (icr & E1000_INT_TXDW) ++e1000_stats.irq_tx;
    if (icr & E1000_INT_LSC) ++e1000_stats.irq_link_change;

    /* Reading ICR acknowledges the device.  Everything else, including RX
     * descriptor copies and link inspection, runs from netdev_poll(). */
    __atomic_fetch_or(&e1000_pending_events, icr, __ATOMIC_RELEASE);
}

void e1000_poll_rx(void) {
    if (!e1000_initialized) return;

    uint32_t events = __atomic_exchange_n(&e1000_pending_events, 0,
                                           __ATOMIC_ACQ_REL);
    if (events & E1000_INT_LSC) {
        e1000_stats.link_up =
            (e1000_read_reg(E1000_REG_STATUS) & E1000_STATUS_LINK_UP) != 0;
    }
    /* Treat IRQ state as a latency hint.  A completed descriptor is
     * authoritative and must make progress even after a lost/coalesced IRQ. */
    if ((events & (E1000_INT_RXO | E1000_INT_RXT0)) == 0 &&
        (rx_descs[rx_cur].status & E1000_RXD_STAT_DD) == 0) return;

    e1000_drain_rx();
    if (rx_descs[rx_cur].status & E1000_RXD_STAT_DD) {
        __atomic_fetch_or(&e1000_pending_events, E1000_INT_RXT0,
                          __ATOMIC_RELEASE);
    }
}

// Function to initialize rings and buffers
static bool initialize_rings_and_buffers(void) {
    // Initialize RX descriptors and buffers
    for (int i = 0; i < E1000_NUM_RX_DESC; i++) {
        rx_buffers[i] = rx_buffer_storage[i];

        // Initialize RX descriptor
        rx_descs[i].buffer_addr = (uint64_t)(uintptr_t)rx_buffers[i];
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
        tx_descs[i].status = E1000_TXD_STAT_DD;
        tx_descs[i].css = 0;
        tx_descs[i].special = 0;
    }

    printf("TX ring initialized with %d descriptors.\n", E1000_NUM_TX_DESC);
    rx_cur = 0;
    tx_cur = 0;
    return true;
}

void process_packet(void *packet, size_t length) {
    // Process the packet here
    printf("Received packet: %.*s\n", length, (char *)packet);
}

// Send ARP reply in response to an ARP request
void e1000_send_arp_reply(uint8_t *request_packet) {
    uint8_t arp_reply[60];  // Minimum ethernet frame size
    
    // Get our MAC address
    uint8_t our_mac[6];
    e1000_get_mac_address(our_mac);
    
    // Ethernet header
    // Destination MAC: from bytes 6-11 of request (sender MAC)
    memcpy(arp_reply, request_packet + 6, 6);
    
    // Source MAC: our MAC
    memcpy(arp_reply + 6, our_mac, 6);
    
    // EtherType: ARP (0x0806)
    arp_reply[12] = 0x08;
    arp_reply[13] = 0x06;
    
    // ARP packet
    // Hardware type: Ethernet (0x0001)
    arp_reply[14] = 0x00;
    arp_reply[15] = 0x01;
    
    // Protocol type: IPv4 (0x0800)
    arp_reply[16] = 0x08;
    arp_reply[17] = 0x00;
    
    // Hardware size: 6 (MAC address length)
    arp_reply[18] = 0x06;
    
    // Protocol size: 4 (IPv4 address length)
    arp_reply[19] = 0x04;
    
    // Operation: Reply (0x0002)
    arp_reply[20] = 0x00;
    arp_reply[21] = 0x02;
    
    // Sender MAC: our MAC
    memcpy(arp_reply + 22, our_mac, 6);
    
    // Sender IP: 10.0.2.15
    arp_reply[28] = 10;
    arp_reply[29] = 0;
    arp_reply[30] = 2;
    arp_reply[31] = 15;
    
    // Target MAC: from bytes 6-11 of request (requester's MAC)
    memcpy(arp_reply + 32, request_packet + 6, 6);
    
    // Target IP: from bytes 28-31 of request (requester's IP)
    memcpy(arp_reply + 38, request_packet + 28, 4);
    
    // Pad to minimum frame size (42 bytes used, pad to 60)
    memset(arp_reply + 42, 0, 18);
    
    printf("E1000: Sending ARP reply to %02X:%02X:%02X:%02X:%02X:%02X\n",
           arp_reply[0], arp_reply[1], arp_reply[2], 
           arp_reply[3], arp_reply[4], arp_reply[5]);
    
    e1000_send_packet(arp_reply, 60);
}

// void e1000_init(uint8_t bus, uint8_t device, uint8_t function) {
//     // Get BAR0 (Base Address Register 0)
//     uint32_t bar0 = pci_read(bus, device, function, 0x10) & ~0xF;
//     e1000_mmio = (uint32_t *)bar0;

//     // IRQ für das Gerät auslesen
//     device_irq = pci_get_irq(bus, device, function);
//     printf("E1000 found: Bus %u, Device %u, IRQ %u, Function %u\n", bus, device, device_irq, function);

//     // Enable bus mastering
//     pci_set_bus_master(bus, device, 1);

//     // Step 2: Perform a device reset
//     printf("Performing E1000 hardware reset...\n");

//     // Write to the CTRL register to initiate a device reset
//     uint32_t ctrl = e1000_read_reg(E1000_REG_CTRL);
//     ctrl |= E1000_CTRL_RST; // Set the RST bit
//     e1000_write_reg(E1000_REG_CTRL, ctrl);

//     // Wait for the reset to complete (the RST bit clears when done)
//     delay_ms(10); // 10ms delay to allow reset to complete

//     ctrl = e1000_read_reg(E1000_REG_CTRL);
//     if (ctrl & E1000_CTRL_RST) {
//         printf("Error: E1000 reset did not complete.\n");
//         return;
//     }
//     printf("E1000 reset complete.\n");

//     // Step 3: Ensure the device is powered on and enabled
//     printf("Ensuring device is enabled and powered on...\n");
//     ctrl = e1000_read_reg(E1000_REG_CTRL);
//     ctrl &= ~E1000_CTRL_PHY_RST; // Clear PHY_RST to power on the device
//     e1000_write_reg(E1000_REG_CTRL, ctrl);

//     // Step 4: Verify the device is ready
//     uint32_t status = e1000_read_reg(E1000_REG_STATUS);
//     if (!(status & 0x1)) { // Check if the device is not ready
//         printf("Error: E1000 device not ready.\n");
//         return;
//     }
//     printf("E1000 device is ready and powered on.\n");

//     initialize_rings_and_buffers();
//     e1000_enable_interrupts();
//     register_interrupt_handler(device_irq, e1000_isr);

//     // 1. Verify Transmit Engine Configuration
//     uint32_t tctl = e1000_read_reg(E1000_REG_TCTL);
//     if (!(tctl & E1000_TCTL_EN)) {
//         printf("Error: Transmit engine not enabled. Enabling now...\n");
//         uint32_t tctl = 0;
//         tctl |= E1000_TCTL_EN;           // Enable transmitter
//         tctl |= E1000_TCTL_PSP;          // Pad short packets
//         tctl |= (15 << E1000_TCTL_CT_SHIFT);  // Collision threshold
//         tctl |= (64 << E1000_TCTL_COLD_SHIFT); // Collision distance
//         e1000_write_reg(E1000_REG_TCTL, tctl);
//     }

//     e1000_write_reg(E1000_REG_TCTL, 0b0110000000000111111000011111010);
//     //e1000_write_reg(E1000_REG_TIPG, 0x0060200A); // Typical default for gigabit Ethernet

//     printf("E1000 initialized.\n");
// }

int e1000_receive_packet(uint8_t *buffer, size_t buffer_size) {
    if (!e1000_initialized || !buffer || buffer_size == 0) return -1;
    uint32_t next_desc = rx_cur;
    
    // Check if descriptor has DD (Descriptor Done) bit set
    if (!(rx_descs[next_desc].status & E1000_RXD_STAT_DD)) {
        return 0;
    }
    
    // Get packet length from descriptor
    uint16_t packet_length = rx_descs[next_desc].length;
    
    // Validate packet length
    if (packet_length == 0 || packet_length > buffer_size || packet_length > RX_BUFFER_SIZE) {
        ++e1000_stats.rx_invalid_length;
        // Clear descriptor and advance tail
        rx_descs[next_desc].status = 0;
        rx_descs[next_desc].length = 0;
        rx_descs[next_desc].errors = 0;
        e1000_write_reg(E1000_REG_RDT, next_desc);
        rx_cur = (uint16_t)((next_desc + 1u) % E1000_NUM_RX_DESC);
        return E1000_RX_DESCRIPTOR_DROPPED;
    }

    if ((rx_descs[next_desc].status & E1000_RXD_STAT_EOP) == 0 ||
        rx_descs[next_desc].errors != 0) {
        ++e1000_stats.rx_descriptor_errors;
        rx_descs[next_desc].status = 0;
        rx_descs[next_desc].length = 0;
        rx_descs[next_desc].errors = 0;
        e1000_write_reg(E1000_REG_RDT, next_desc);
        rx_cur = (uint16_t)((next_desc + 1u) % E1000_NUM_RX_DESC);
        return E1000_RX_DESCRIPTOR_DROPPED;
    }
    
    // Copy packet to user buffer
    memcpy(buffer, rx_buffers[next_desc], packet_length);
    
    // Clear descriptor status for reuse
    rx_descs[next_desc].status = 0;
    rx_descs[next_desc].length = 0;
    rx_descs[next_desc].errors = 0;
    
    // Update tail pointer to indicate descriptor is available for hardware
    e1000_write_reg(E1000_REG_RDT, next_desc);
    rx_cur = (uint16_t)((next_desc + 1u) % E1000_NUM_RX_DESC);
    
    return packet_length;
}

void e1000_get_mac_address(uint8_t *mac) {
    // Read MAC address from RAL (Receive Address Low) and RAH (Receive Address High) registers
    uint32_t mac_low = e1000_read_reg(E1000_REG_RAL);
    uint32_t mac_high = e1000_read_reg(E1000_REG_RAH);
    
    // Extract bytes from the 32-bit registers
    mac[0] = (mac_low >> 0) & 0xFF;
    mac[1] = (mac_low >> 8) & 0xFF;
    mac[2] = (mac_low >> 16) & 0xFF;
    mac[3] = (mac_low >> 24) & 0xFF;
    mac[4] = (mac_high >> 0) & 0xFF;
    mac[5] = (mac_high >> 8) & 0xFF;
}

static bool reset_e1000(void) {
    // Step 2: Perform a device reset
    printf("Performing E1000 hardware reset...\n");

    // Write to the CTRL register to initiate a device reset
    uint32_t ctrl = e1000_read_reg(E1000_REG_CTRL);
    ctrl |= E1000_CTRL_RST; // Set the RST bit
    e1000_write_reg(E1000_REG_CTRL, ctrl);

    uint32_t timeout = 1000;
    do {
        pit_delay(1);
        ctrl = e1000_read_reg(E1000_REG_CTRL);
    } while ((ctrl & E1000_CTRL_RST) && --timeout > 0);
    if (ctrl & E1000_CTRL_RST) {
        printf("Error: E1000 reset did not complete.\n");
        return false;
    }
    printf("E1000 reset complete.\n");

    // Step 3: Ensure the device is powered on and enabled, set link up
    printf("Ensuring device is enabled and powered on...\n");
    ctrl = e1000_read_reg(E1000_REG_CTRL);
    ctrl &= ~E1000_CTRL_PHY_RST; // Clear PHY_RST to power on the device
    ctrl &= ~E1000_CTRL_SPEED_MASK;
    ctrl |= E1000_CTRL_FD;
    ctrl |= E1000_CTRL_SLU;      // Set Link Up
    ctrl |= E1000_CTRL_SPEED_1000;
    ctrl |= E1000_CTRL_FRCSPD;   // Force Speed (for emulation)
    ctrl |= E1000_CTRL_FRCDPLX;  // Force the FD bit above
    e1000_write_reg(E1000_REG_CTRL, ctrl);
    printf("E1000: Link forced UP (CTRL=0x%08X)\n", ctrl);
    return true;
}   

static bool e1000_init(e1000_device_t *dev) {
    memset((void *)&e1000_stats, 0, sizeof(e1000_stats));
    e1000_pending_events = 0;

    // Reset the device
    if (!dev || !dev->mmio_base || !reset_e1000()) return false;

    // STATUS bit 0 describes duplex; it is not a generic "ready" bit.  Only
    // an all-ones MMIO read reliably indicates an inaccessible device here.
    uint32_t status = e1000_read_reg(E1000_REG_STATUS);
    if (status == 0xFFFFFFFFu) {
        printf("Error: E1000 MMIO is not responding.\n");
        return false;
    }
    printf("E1000 device responds (STATUS=0x%08X).\n", status);
    e1000_stats.link_up = (status & E1000_STATUS_LINK_UP) != 0;

    // Initialize descriptor rings and buffers
    if (!initialize_rings_and_buffers()) return false;
    
    // Configure RX ring
    e1000_write_reg(E1000_REG_RDBAL, (uint32_t)rx_descs);
    e1000_write_reg(E1000_REG_RDBAH, 0);
    e1000_write_reg(E1000_REG_RDLEN, E1000_NUM_RX_DESC * sizeof(struct e1000_rx_desc));
    e1000_write_reg(E1000_REG_RDH, 0);
    e1000_write_reg(E1000_REG_RDT, E1000_NUM_RX_DESC - 1);
    
    printf("E1000: RX ring configured (base=0x%08X, len=%u, head=0, tail=%u)\n",
           (uint32_t)rx_descs, E1000_NUM_RX_DESC * sizeof(struct e1000_rx_desc), E1000_NUM_RX_DESC - 1);
    
    // Configure TX ring
    e1000_write_reg(E1000_REG_TDBAL, (uint32_t)tx_descs);
    e1000_write_reg(E1000_REG_TDBAH, 0);
    e1000_write_reg(E1000_REG_TDLEN, E1000_NUM_TX_DESC * sizeof(struct e1000_tx_desc));
    e1000_write_reg(E1000_REG_TDH, 0);
    e1000_write_reg(E1000_REG_TDT, 0);
    
    printf("E1000: TX ring configured (base=0x%08X, len=%u)\n",
           (uint32_t)tx_descs, E1000_NUM_TX_DESC * sizeof(struct e1000_tx_desc));
    
    // Configure TXDCTL - Enable transmit descriptor fetching
    uint32_t txdctl = e1000_read_reg(E1000_REG_TXDCTL);
    txdctl |= (1 << 25);  // GRAN bit - descriptor granularity
    txdctl |= (1 << 22);  // LWTHRESH - low water threshold
    e1000_write_reg(E1000_REG_TXDCTL, txdctl);
    printf("E1000: TXDCTL configured (0x%08X)\n", txdctl);
    
    // Set up receive address (MAC address filter)
    // Read current MAC from RAL/RAH and write it back to enable reception
    uint32_t ral = e1000_read_reg(E1000_REG_RAL);
    uint32_t rah = e1000_read_reg(E1000_REG_RAH);
    rah |= (1 << 31);  // Set AV (Address Valid) bit
    e1000_write_reg(E1000_REG_RAL, ral);
    e1000_write_reg(E1000_REG_RAH, rah);
    printf("E1000: MAC filter configured (RAL=0x%08X, RAH=0x%08X)\n", ral, rah);
    
    // Clear multicast table array (accept all in promiscuous mode)
    for (int i = 0; i < 128; i++) {
        e1000_write_reg(0x5200 + (i * 4), 0);
    }
    printf("E1000: Multicast table cleared\n");
    
    // Enable receiver
    uint32_t rctl = 0;
    rctl |= E1000_RCTL_EN;           // Enable receiver
    /* RAR0 already contains this adapter's MAC. Keep promiscuous unicast and
     * multicast disabled for normal LAN use; DHCP/ARP still arrive via BAM. */
    rctl |= E1000_RCTL_BAM;          // Accept broadcast
    rctl |= E1000_RCTL_BSIZE_8192;   // 8KB buffers
    rctl |= E1000_RCTL_SECRC;        // Strip CRC
    e1000_write_reg(E1000_REG_RCTL, rctl);
    printf("E1000: Receiver enabled (RCTL=0x%08X)\n", rctl);
    
    // Enable transmitter - Read current value first
    uint32_t tctl = e1000_read_reg(E1000_REG_TCTL);
    tctl |= E1000_TCTL_EN;           // Enable transmitter
    tctl |= E1000_TCTL_PSP;          // Pad short packets
    tctl |= (15 << E1000_TCTL_CT_SHIFT);  // Collision threshold (15)
    tctl |= (0x40 << E1000_TCTL_COLD_SHIFT); // Collision distance (64 = 0x40)
    e1000_write_reg(E1000_REG_TCTL, tctl);
    printf("E1000: Transmitter enabled (TCTL=0x%08X)\n", tctl);
    
    // Set TIPG (Transmit IPG) for proper packet spacing
    e1000_write_reg(E1000_REG_TIPG, 0x0060200A);
    printf("E1000: TIPG configured\n");
    
    // Register IRQ handler
    if (!pci_irq_is_valid((uint8_t)dev->irq)) {
        printf("E1000: refusing invalid IRQ %u\n", dev->irq);
        return false;
    }
    if (register_interrupt_handler((int)dev->irq, (void *)e1000_isr) != 0) {
        printf("E1000: IRQ handler registration failed\n");
        return false;
    }
    printf("E1000: IRQ handler registered for IRQ %u\n", dev->irq);
    
    return true;
}

int e1000_probe(pci_device_t *pci_dev) {
    printf("E1000: Probe called for device %04X:%04X\n", pci_dev->vendor_id, pci_dev->device_id);
    
    if (pci_dev->vendor_id == E1000_VENDOR_ID &&
        (pci_dev->device_id == E1000_DEVICE_ID_82540EM ||
         pci_dev->device_id == E1000_DEVICE_ID_82545EM)) {

        printf("E1000: Device matched, initializing...\n");

        // Map MMIO region
        uint32_t raw_bar0 = pci_read_bar(pci_dev, 0);
        if ((raw_bar0 & 1u) != 0 || (raw_bar0 & ~0xFu) == 0) {
            printf("E1000: BAR0 is not a valid MMIO BAR\n");
            return -1;
        }
        if ((raw_bar0 & 0x6u) == 0x4u && pci_read_bar(pci_dev, 1) != 0) {
            printf("E1000: 64-bit BAR above 4 GiB is unsupported\n");
            return -1;
        }
        uint64_t bar0 = raw_bar0 & ~0xFu;
        e1000_device.mmio_base = map_mmio_region(bar0, E1000_MMIO_SIZE);
        if (e1000_device.mmio_base == NULL) {
            printf("E1000: unable to map BAR0 MMIO region\n");
            return -1;
        }

        printf("E1000: MMIO base mapped to 0x%08X\n", (uint32_t)e1000_device.mmio_base);

        // Configure IRQ
        e1000_device.irq = pci_configure_irq(pci_dev);
        if (!pci_irq_is_valid((uint8_t)e1000_device.irq)) {
            printf("E1000: invalid legacy IRQ %u\n", e1000_device.irq);
            e1000_device.mmio_base = NULL;
            return -1;
        }

        printf("E1000: IRQ configured to %u\n", e1000_device.irq);

        pci_enable_device(pci_dev);
        pci_set_bus_master(pci_dev->bus, pci_dev->slot,
                           pci_dev->function, 1);

        // Initialize the device
        if (!e1000_init(&e1000_device)) {
            pci_set_bus_master(pci_dev->bus, pci_dev->slot,
                               pci_dev->function, 0);
            e1000_device.mmio_base = NULL;
            return -1;
        }
        e1000_initialized = true;
        e1000_enable_interrupts();
        printf("E1000: Interrupts enabled\n");

        uint8_t mac[6];
        e1000_get_mac_address(mac);

        printf("E1000: Initialized successfully\n");
        printf("E1000 MAC: %02X:%02X:%02X:%02X:%02X:%02X, ", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        printf("IO Base: 0x%08X, IRQ: %u\n", (uint32_t)e1000_device.mmio_base, e1000_device.irq);

        return 0;
    }

    printf("E1000: Device not matched\n");
    return -1;
}

void e1000_detect(){
    printf("E1000: Registering 82540EM/82545EM drivers\n");

    pci_register_driver(E1000_VENDOR_ID, E1000_DEVICE_ID_82540EM, e1000_probe);
    pci_register_driver(E1000_VENDOR_ID, E1000_DEVICE_ID_82545EM, e1000_probe);
    
    printf("E1000: Driver registered successfully\n");
}

bool e1000_is_initialized() {
    return e1000_initialized;
}

void e1000_debug_registers() {
    if (!e1000_is_initialized()) {
        printf("E1000 not initialized\n");
        return;
    }
    
    printf("=== E1000 Register Dump ===\n");
    
    uint32_t ctrl = e1000_read_reg(E1000_REG_CTRL);
    uint32_t status = e1000_read_reg(E1000_REG_STATUS);
    printf("CTRL:   0x%08X  STATUS: 0x%08X\n", ctrl, status);
    
    if (status & E1000_STATUS_LINK_UP) {
        printf("  Link: UP\n");
    } else {
        printf("  Link: DOWN\n");
    }
    
    uint32_t rctl = e1000_read_reg(E1000_REG_RCTL);
    uint32_t tctl = e1000_read_reg(E1000_REG_TCTL);
    printf("RCTL:   0x%08X  TCTL:   0x%08X\n", rctl, tctl);
    printf("  RX %s, TX %s\n", 
           (rctl & E1000_RCTL_EN) ? "ENABLED" : "DISABLED",
           (tctl & E1000_TCTL_EN) ? "ENABLED" : "DISABLED");
    
    uint32_t rdh = e1000_read_reg(E1000_REG_RDH);
    uint32_t rdt = e1000_read_reg(E1000_REG_RDT);
    uint32_t rdbal = e1000_read_reg(E1000_REG_RDBAL);
    uint32_t rdlen = e1000_read_reg(E1000_REG_RDLEN);
    printf("RX Ring:\n");
    printf("  RDBAL:  0x%08X  RDLEN: %u\n", rdbal, rdlen);
    printf("  RDH:    %u          RDT:   %u\n", rdh, rdt);
    
    uint32_t tdh = e1000_read_reg(E1000_REG_TDH);
    uint32_t tdt = e1000_read_reg(E1000_REG_TDT);
    uint32_t tdbal = e1000_read_reg(E1000_REG_TDBAL);
    uint32_t tdlen = e1000_read_reg(E1000_REG_TDLEN);
    printf("TX Ring:\n");
    printf("  TDBAL:  0x%08X  TDLEN: %u\n", tdbal, tdlen);
    printf("  TDH:    %u          TDT:   %u\n", tdh, tdt);
    
    uint32_t ims = e1000_read_reg(E1000_REG_IMS);
    printf("Interrupts:\n");
    printf("  Last ICR: 0x%08X  IMS: 0x%08X\n",
           e1000_stats.last_icr, ims);
    printf("  IRQ total=%u RX=%u TX=%u overrun=%u link-change=%u\n",
           e1000_stats.irq_total, e1000_stats.irq_rx,
           e1000_stats.irq_tx, e1000_stats.irq_overrun,
           e1000_stats.irq_link_change);
    
    // Check total packets transmitted counter
    uint32_t tpt = e1000_read_reg(E1000_REG_TPT);
    printf("Statistics:\n");
    printf("  TPT (Total Packets Transmitted): %u\n", tpt);
    printf("  RX packets=%u invalid-length=%u descriptor-errors=%u\n",
           e1000_stats.rx_packets, e1000_stats.rx_invalid_length,
           e1000_stats.rx_descriptor_errors);
    printf("  TX packets=%u errors=%u  Link=%s\n",
           e1000_stats.tx_packets, e1000_stats.tx_errors,
           e1000_stats.link_up ? "UP" : "DOWN");
    
    printf("===========================\n");
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
    printf("IMS: 0x%08X | Last ICR: 0x%08X\n",
           ims, e1000_stats.last_icr);

    // Link and PHY Status
    if (status & E1000_STATUS_LINK_UP) {
        printf("Link is up.\n");
    } else {
        printf("Link is down.\n");
    }

    // Verify Descriptor Alignment
    if ((uintptr_t)rx_descs % 16 != 0 || (uintptr_t)tx_descs % 16 != 0) {
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

bool e1000_send_packet(void *packet, size_t length) {
    if (netdev_outputs_fenced()) return false;
    if (!e1000_initialized || !packet || length > 1518 || length < 14) {
        ++e1000_stats.tx_errors;
        return false;
    }
    if (__sync_lock_test_and_set(&e1000_tx_busy, 1u)) {
        ++e1000_stats.tx_errors;
        return false;
    }
    
    // Get current tail
    uint32_t tail = tx_cur;

    /* A descriptor may only be reused after hardware has written DD. */
    uint32_t wait = 1000000u;
    while (!(tx_descs[tail].status & E1000_TXD_STAT_DD) && wait > 0) {
        --wait;
        __asm__ volatile("pause");
    }
    if (!(tx_descs[tail].status & E1000_TXD_STAT_DD)) {
        ++e1000_stats.tx_errors;
        __sync_lock_release(&e1000_tx_busy);
        return false;
    }
    
    /* The caller may use a stack buffer.  DMA from a persistent per-descriptor
     * bounce buffer so the memory remains valid until DD is written back. */
    memcpy(tx_buffer_storage[tail], packet, length);

    // Set up descriptor
    tx_descs[tail].buffer_addr = (uint64_t)(uintptr_t)tx_buffer_storage[tail];
    tx_descs[tail].length = (uint16_t)length;
    tx_descs[tail].cso = 0;
    tx_descs[tail].cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS | E1000_TXD_CMD_IDE;
    tx_descs[tail].status = 0;
    tx_descs[tail].css = 0;
    tx_descs[tail].special = 0;
    
    // Memory barrier to ensure descriptor is written before TDT update
    __asm__ volatile("" ::: "memory");
    
    // Update tail pointer to signal hardware
    tx_cur = (tx_cur + 1) % E1000_NUM_TX_DESC;
    e1000_write_reg(E1000_REG_TDT, tx_cur);
    
    // Wait for completion, but never hang the kernel on a broken device.
    wait = 1000000u;
    while (!(tx_descs[tail].status & E1000_TXD_STAT_DD) && wait > 0) {
        --wait;
        __asm__ volatile("pause");
    }
    
    // Check if transmission completed
    uint8_t status = tx_descs[tail].status;
    
    if (!(status & E1000_TXD_STAT_DD)) {
        ++e1000_stats.tx_errors;
        __sync_lock_release(&e1000_tx_busy);
        return false;
    }
    ++e1000_stats.tx_packets;
    __sync_lock_release(&e1000_tx_busy);
    return true;
}

void e1000_fence_outputs(void) {
    if (!e1000_initialized || e1000_device.mmio_base == NULL) return;
    e1000_write_reg(E1000_REG_IMC, 0xFFFFFFFFu);
    uint32_t tctl = e1000_read_reg(E1000_REG_TCTL);
    e1000_write_reg(E1000_REG_TCTL, tctl & ~E1000_TCTL_EN);
}

bool e1000_outputs_fenced(void) {
    return !e1000_initialized || e1000_device.mmio_base == NULL ||
           (e1000_read_reg(E1000_REG_TCTL) & E1000_TCTL_EN) == 0;
}

void e1000_send_test_packet() {
    printf("E1000: Sending broadcast test packet...\n");
    
    // Get our MAC address
    uint8_t our_mac[6];
    e1000_get_mac_address(our_mac);
    
    printf("E1000: Our MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
           our_mac[0], our_mac[1], our_mac[2], our_mac[3], our_mac[4], our_mac[5]);
    
    // Use static buffer instead of stack for DMA compatibility
    uint8_t *packet = tx_packet_buffer;
    
    printf("E1000: Using TX buffer at 0x%08X (static kernel data section)\n", (uint32_t)packet);
    
    // Ethernet header
    // Destination: Broadcast
    packet[0] = 0xFF; packet[1] = 0xFF; packet[2] = 0xFF;
    packet[3] = 0xFF; packet[4] = 0xFF; packet[5] = 0xFF;
    
    // Source: Our MAC
    packet[6] = our_mac[0]; packet[7] = our_mac[1]; packet[8] = our_mac[2];
    packet[9] = our_mac[3]; packet[10] = our_mac[4]; packet[11] = our_mac[5];
    
    // EtherType: ARP (0x0806)
    packet[12] = 0x08; packet[13] = 0x06;
    
    // ARP packet
    packet[14] = 0x00; packet[15] = 0x01; // Hardware type: Ethernet
    packet[16] = 0x08; packet[17] = 0x00; // Protocol type: IPv4
    packet[18] = 0x06; // Hardware size: 6
    packet[19] = 0x04; // Protocol size: 4
    packet[20] = 0x00; packet[21] = 0x01; // Operation: Request
    
    // Sender MAC
    packet[22] = our_mac[0]; packet[23] = our_mac[1]; packet[24] = our_mac[2];
    packet[25] = our_mac[3]; packet[26] = our_mac[4]; packet[27] = our_mac[5];
    
    // Sender IP: 10.0.2.15
    packet[28] = 10; packet[29] = 0; packet[30] = 2; packet[31] = 15;
    
    // Target MAC: 00:00:00:00:00:00
    packet[32] = 0x00; packet[33] = 0x00; packet[34] = 0x00;
    packet[35] = 0x00; packet[36] = 0x00; packet[37] = 0x00;
    
    // Target IP: 10.0.2.1 (gateway)
    packet[38] = 10; packet[39] = 0; packet[40] = 2; packet[41] = 1;
    
    // Padding to 60 bytes minimum
    for (int i = 42; i < 60; i++) {
        packet[i] = 0x00;
    }
    
    printf("E1000: Sending ARP request for 10.0.2.1 (gateway)\n");
    
    // Show first 42 bytes of packet (before padding)
    printf("E1000: Packet data (first 42 bytes):\n");
    for (int i = 0; i < 42; i++) {
        if (i % 16 == 0) printf("  %04X: ", i);
        printf("%02X ", packet[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    printf("\n");
    
    e1000_send_packet(packet, 60);
    
    printf("E1000: Test packet sent. Check tcpdump on host.\n");
}
