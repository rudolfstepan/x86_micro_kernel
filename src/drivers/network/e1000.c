#include "e1000.h"
#include "toolchain/stdio.h"
#include "toolchain/stdlib.h"
#include "toolchain/strings.h"
#include "drivers/pci.h"
#include "drivers/io/io.h"
#include "kernel/sys.h"


// PIC-Konstanten
#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1

#define TRANSMIT_RING_SIZE 64
#define RECEIVE_RING_SIZE 64

struct e1000_tx_desc transmit_ring[TRANSMIT_RING_SIZE] __attribute__((aligned(16)));
struct e1000_rx_desc receive_ring[RECEIVE_RING_SIZE] __attribute__((aligned(16)));

uint8_t tx_buffers[TRANSMIT_RING_SIZE][2048] __attribute__((aligned(16)));
uint8_t rx_buffers[RECEIVE_RING_SIZE][2048] __attribute__((aligned(16)));

uint32_t *e1000_mmio = NULL;

// #define E1000_REG_IMS 0x00D0
// #define E1000_REG_ICR 0x00C0

uint8_t device_irq = 11;




// MMIO base address of the E1000 device (to be set up via PCI configuration)
#define E1000_MMIO_BASE 0xF0000000

// Register offsets (consult the E1000 datasheet)
#define E1000_CTRL      0x00000  // Device Control Register
#define E1000_STATUS    0x00008  // Device Status Register
#define E1000_RCTL      0x00100  // Receive Control Register
#define E1000_TCTL      0x00400  // Transmit Control Register
#define E1000_ICR       0x000C0  // Interrupt Cause Read
#define E1000_IMS       0x000D0  // Interrupt Mask Set/Read
#define E1000_ICS       0x000C8  // Interrupt Cause Set
#define E1000_RDBAL     0x02800  // Receive Descriptor Base Low
#define E1000_RDBAH     0x02804  // Receive Descriptor Base High
#define E1000_RDLEN     0x02808  // Receive Descriptor Length
#define E1000_TDBAL     0x03800  // Transmit Descriptor Base Low
#define E1000_TDBAH     0x03804  // Transmit Descriptor Base High
#define E1000_TDLEN     0x03808  // Transmit Descriptor Length

// RCTL Register Bits
#define E1000_RCTL_EN       0x00000002  // Receiver Enable
#define E1000_RCTL_LBM_MAC  0x00000040  // Loopback Mode

// TCTL Register Bits
#define E1000_TCTL_EN       0x00000002  // Transmitter Enable

// IMS Register Bits
#define E1000_IMS_RXT0      0x00000080  // Receive Timer Interrupt

// Read a 32-bit register
static inline uint32_t e1000_read_reg(uint32_t offset) {
    return e1000_mmio[offset / 4];
}

// Write a 32-bit register
static inline void e1000_write_reg(uint32_t offset, uint32_t value) {
    e1000_mmio[offset / 4] = value;
}

void e1000_enable_interrupts() {
    // Enable Receive Timer Interrupt
    e1000_write_reg(E1000_IMS, E1000_IMS_RXT0);
}

void e1000_enable_loopback() {
    // Enable Receiver with MAC Loopback Mode
    uint32_t rctl = e1000_read_reg(E1000_RCTL);
    rctl |= E1000_RCTL_EN | E1000_RCTL_LBM_MAC;
    e1000_write_reg(E1000_RCTL, rctl);

    // Enable Transmitter
    uint32_t tctl = e1000_read_reg(E1000_TCTL);
    tctl |= E1000_TCTL_EN;
    e1000_write_reg(E1000_TCTL, tctl);
}











void check_received_packet() {
    static uint32_t current_tail = 0;

    // Check the status of the current tail descriptor
    if (receive_ring[current_tail].status & 0x1) { // DD (Descriptor Done) bit
        // The descriptor contains a valid packet
        char *received_data = (char *)rx_buffers[current_tail];
        uint16_t received_length = receive_ring[current_tail].length;

        // Print the received packet for debugging
        printf("Received packet: %.*s\n", received_length, received_data);

        // Clear the status of the descriptor for reuse
        receive_ring[current_tail].status = 0;

        // Update the RDT register to indicate the descriptor is ready for reuse
        current_tail = (current_tail + 1) % RECEIVE_RING_SIZE;
        e1000_mmio[E1000_REG_RDT / 4] = current_tail;
    } else {
        // No packet is available in the current descriptor
        //printf("No packet received.\n");
    }
}

void e1000_isr() {
    //printf("E1000 ISR triggered.\n");
    // Read the Interrupt Cause Register (ICR) to determine the cause
    //uint32_t icr = e1000_mmio[E1000_REG_ICR / 4];

    printf("E1000 Interrupt Cause Register\n");

    // Check for RX interrupt
    // if (icr & (1 << 7)) { // RXDMT
    //     printf("Receive interrupt triggered.\n");
    //     check_received_packet(); // Process received packets
    // }

    // // Check for TX interrupt
    // if (icr & (1 << 1)) { // TXDW
    //     printf("Transmit interrupt triggered.\n");
    //     // TX cleanup logic (if needed)
    // }

    // // Check for RX Overrun or Errors
    // if (icr & (1 << 6)) { // RXO
    //     printf("Receiver overrun occurred.\n");
    //     // Error handling logic
    // }

    // Add handling for other interrupts as needed
    // clear the interrupt
    //e1000_mmio[E1000_REG_ICR / 4] = icr;

    // Acknowledge the interrupt
    //outb(PIC1_COMMAND, 0x20);
}


// Ports for the PICs
#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1

// void enable_irq(uint8_t irq) {
//     uint16_t port;
//     uint8_t mask;

//     if (irq < 8) {
//         // IRQ is handled by the primary PIC
//         port = PIC1_DATA;
//     } else {
//         // IRQ is handled by the secondary PIC
//         irq -= 8;
//         port = PIC2_DATA;
//     }

//     // Read the current IRQ mask and clear the bit for the given IRQ
//     mask = inb(port) & ~(1 << irq);
//     outb(port, mask);
// }

// void setup_interrupts() {
//     // Map the ISR to the appropriate IRQ vector
//     register_interrupt_handler(device_irq, e1000_isr);

//     // Enable the IRQ in the PIC (Programmable Interrupt Controller)
//     //enable_irq(device_irq);
// }


void setup_transmit_ring() {
    for (int i = 0; i < TRANSMIT_RING_SIZE; i++) {
        transmit_ring[i].buffer_addr = (uint64_t)tx_buffers[i];
        transmit_ring[i].status = 0; // Initialize status to 0
    }
}

void setup_receive_ring() {
    for (int i = 0; i < RECEIVE_RING_SIZE; i++) {
        receive_ring[i].buffer_addr = (uint64_t)rx_buffers[i];
        receive_ring[i].status = 0; // Initialize status to 0
    }
}

void process_packet(void *packet, size_t length) {
    // Process the packet here
    printf("Received packet: %.*s\n", length, (char *)packet);
}

// Transmit Ring Initialization
void e1000_init_transmit() {
    // Map the transmit ring to a contiguous memory area
    memset(transmit_ring, 0, sizeof(transmit_ring));

    // Set the base address and length of the transmit descriptor ring
    e1000_mmio[E1000_REG_TDBAL / 4] = (uint32_t)(uintptr_t)transmit_ring; // Low 32 bits
    e1000_mmio[E1000_REG_TDBAH / 4] = 0; // High 32 bits (for 64-bit addressing)
    e1000_mmio[E1000_REG_TDLEN / 4] = sizeof(transmit_ring);

    // Initialize the Transmit Descriptor Head and Tail
    e1000_mmio[E1000_REG_TDH / 4] = 0;
    e1000_mmio[E1000_REG_TDT / 4] = 0;

    // Enable Transmit in the TCTL register
    uint32_t tctl = e1000_mmio[E1000_REG_TCTL / 4];
    tctl |= (1 << 1); // Transmit Enable
    tctl |= (1 << 3); // Pad Short Packets
    tctl |= (1 << 8); // Collision Threshold
    e1000_mmio[E1000_REG_TCTL / 4] = tctl;
}

void enable_e1000_interrupts() {
    // Enable interrupts for:
    // - RXDMT (Receive Descriptor Minimum Threshold Reached)
    // - RXO (Receiver Overrun)
    // - RXT (Receiver Timer Interrupt)
    // - TXDW (Transmit Descriptor Written Back)
    e1000_mmio[E1000_REG_IMS / 4] = (1 << 7) | (1 << 6) | (1 << 4) | (1 << 1);
}

void e1000_init(uint8_t bus, uint8_t device, uint8_t function) {
    // Get BAR0 (Base Address Register 0)
    uint32_t bar0 = pci_read(bus, device, function, 0x10) & ~0xF;
    e1000_mmio = (uint32_t *)bar0;

    setup_transmit_ring();
    setup_receive_ring();

    // Initialize Transmit
    e1000_mmio[E1000_REG_TDBAL / 4] = (uint32_t)(uint64_t)transmit_ring; // Base Address Low
    e1000_mmio[E1000_REG_TDBAH / 4] = (uint32_t)((uint64_t)transmit_ring >> 32); // Base Address High
    e1000_mmio[E1000_REG_TDLEN / 4] = TRANSMIT_RING_SIZE * sizeof(struct e1000_tx_desc); // Length
    e1000_mmio[E1000_REG_TDH / 4] = 0; // Head
    e1000_mmio[E1000_REG_TDT / 4] = 0; // Tail

    // Enable Transmit
    e1000_mmio[E1000_REG_TCTL / 4] = 0x4010;

    // Initialize Receive
    e1000_mmio[E1000_REG_RDBAL / 4] = (uint32_t)(uint64_t)receive_ring; // Base Address Low
    e1000_mmio[E1000_REG_RDBAH / 4] = (uint32_t)((uint64_t)receive_ring >> 32); // Base Address High
    e1000_mmio[E1000_REG_RDLEN / 4] = RECEIVE_RING_SIZE * sizeof(struct e1000_rx_desc); // Length
    e1000_mmio[E1000_REG_RDH / 4] = 0; // Head
    e1000_mmio[E1000_REG_RDT / 4] = RECEIVE_RING_SIZE - 1; // Tail

    // Enable Receive
    e1000_mmio[E1000_REG_RCTL / 4] = 0x40000; // Promiscuous mode (for testing)

    e1000_init_transmit();

    // Map the ISR to the appropriate IRQ vector
    register_interrupt_handler(device_irq, e1000_isr);

    e1000_enable_interrupts();
    e1000_enable_loopback();

    // Clear any pending interrupts
    e1000_write_reg(E1000_ICR, 0xFFFFFFFF);

    printf("E1000 initialized.\n");
}

void e1000_receive_packet() {
    uint32_t head = e1000_mmio[E1000_REG_RDH / 4];
    while (head != e1000_mmio[E1000_REG_RDT / 4]) {
        struct e1000_rx_desc *desc = &receive_ring[head];
        if (desc->status & 0x1) { // Check DD bit
            process_packet((void *)desc->buffer_addr, desc->length);
            desc->status = 0;
        }
        head = (head + 1) % RECEIVE_RING_SIZE;
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

                    // IRQ für das Gerät auslesen
                    device_irq = pci_get_irq(bus, device, function);
                    printf("E1000 found: Bus %u, Device %u, IRQ %u, Function %u\n", bus, device, device_irq, function);

                    e1000_init(bus, device, function);
                    get_mac_address();

                    return;
                }
            }
        }
    }
}

void e1000_send_packet(void *packet, size_t length) {
    // Get the tail index
    uint32_t tail = e1000_mmio[E1000_REG_TDT / 4];

    // Get the descriptor at the current tail
    struct e1000_tx_desc *desc = &transmit_ring[tail];

    // Configure the descriptor
    desc->buffer_addr = (uint64_t)packet; // Physical address
    desc->length = length;
    desc->cmd = 0x8 | 0x1; // RS (Report Status) | EOP (End of Packet)
    desc->status = 0;      // Clear status

    // Update the tail to the next descriptor
    e1000_mmio[E1000_REG_TDT / 4] = (tail + 1) % TRANSMIT_RING_SIZE;

    printf("Packet sent: %u Bytes\n", length);
}

void e1000_send_test_packet() {

    // Enable loopback mode
    //e1000_mmio[E1000_REG_RCTL / 4] |= (1 << 6);

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


    e1000_send_packet(data, sizeof(data));
}

void loopback_test() {
    e1000_send_test_packet();
    e1000_receive_packet();
}

