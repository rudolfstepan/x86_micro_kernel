#include "e1000.h"
#include "toolchain/stdio.h"
#include "toolchain/stdlib.h"
#include "toolchain/strings.h"
#include "drivers/pci.h"
#include "drivers/io/io.h"
#include "kernel/sys.h"
#include "kernel/memory.h"


// PIC-Konstanten
#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1

uint32_t *e1000_mmio = NULL;
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


#define E1000_NUM_RX_DESC 32
#define E1000_NUM_TX_DESC 8

// struct e1000_rx_desc *receive_ring;
// struct e1000_tx_desc *transmit_ring;

struct e1000_rx_desc *rx_descs[E1000_NUM_RX_DESC]; // Receive Descriptor Buffers
struct e1000_tx_desc *tx_descs[E1000_NUM_TX_DESC]; // Transmit Descriptor Buffers

uint16_t rx_cur;      // Current Receive Descriptor Buffer
uint16_t tx_cur;      // Current Transmit Descriptor Buffer

// struct e1000_tx_desc transmit_ring[TRANSMIT_RING_SIZE] __attribute__((aligned(16)));
// uint8_t tx_buffers[TRANSMIT_RING_SIZE][2048] __attribute__((aligned(256)));

// struct e1000_rx_desc receive_ring[RECEIVE_RING_SIZE] __attribute__((aligned(16)));
// uint8_t rx_buffers[RECEIVE_RING_SIZE][2048] __attribute__((aligned(256)));

// Read a 32-bit register
static inline uint32_t e1000_read_reg(uint32_t offset) {
    return e1000_mmio[offset / 4];
}

// Write a 32-bit register
static inline void e1000_write_reg(uint32_t offset, uint32_t value) {
    e1000_mmio[offset / 4] = value;
}

void e1000_enable_interrupts() {
    e1000_write_reg(REG_IMASK ,0x1F6DC);
    e1000_write_reg(REG_IMASK ,0xff & ~4);
    e1000_read_reg(0xc0);
}

void e1000_enable_loopback() {
    uint32_t rctl = e1000_read_reg(E1000_RCTL);
    rctl |= E1000_RCTL_EN;          // Enable receiver
    rctl |= E1000_RCTL_LBM_MAC;     // Enable loopback mode
    rctl |= E1000_RCTL_UPE;         // Unicast Promiscuous Enable
    rctl |= E1000_RCTL_MPE;         // Multicast Promiscuous Enable
    e1000_write_reg(E1000_RCTL, rctl);
}

#define PCI_COMMAND 0x04
#define PCI_COMMAND_BUS_MASTER 0x4
#define E1000_CTRL 0x00000       // Device Control Register
#define E1000_STATUS 0x00008     // Device Status Register
#define E1000_CTRL_RST 0x04000000 // Device Reset
#define E1000_CTRL_PHY_RST 0x80000000 // PHY Reset

// Method to enable/reset E1000 network card
void e1000_power_on_and_reset(uint8_t bus, uint8_t device) {
    // Step 1: Enable Bus Mastering in the PCI configuration space
    uint16_t command = pci_read(bus, device, PCI_COMMAND, 2);
    if (!(command & PCI_COMMAND_BUS_MASTER)) {
        command |= PCI_COMMAND_BUS_MASTER;
        pci_write(bus, device, PCI_COMMAND, 2, command);
        printf("Bus mastering enabled.\n");
    } else {
        printf("Bus mastering already enabled.\n");
    }

    // Step 2: Perform a device reset
    printf("Performing E1000 hardware reset...\n");

    // Write to the CTRL register to initiate a device reset
    uint32_t ctrl = e1000_read_reg(E1000_CTRL);
    ctrl |= E1000_CTRL_RST; // Set the RST bit
    e1000_write_reg(E1000_CTRL, ctrl);

    // Wait for the reset to complete (the RST bit clears when done)
    delay_ms(10); // 10ms delay to allow reset to complete

    ctrl = e1000_read_reg(E1000_CTRL);
    if (ctrl & E1000_CTRL_RST) {
        printf("Error: E1000 reset did not complete.\n");
        return;
    }
    printf("E1000 reset complete.\n");

    // Step 3: Ensure the device is powered on and enabled
    printf("Ensuring device is enabled and powered on...\n");
    ctrl = e1000_read_reg(E1000_CTRL);
    ctrl &= ~E1000_CTRL_PHY_RST; // Clear PHY_RST to power on the device
    e1000_write_reg(E1000_CTRL, ctrl);

    // Step 4: Verify the device is ready
    uint32_t status = e1000_read_reg(E1000_STATUS);
    if (!(status & 0x1)) { // Check if the device is not ready
        printf("Error: E1000 device not ready.\n");
        return;
    }
    printf("E1000 device is ready and powered on.\n");
}

void check_received_packet() {
    static uint32_t current_tail = 0;

    // Check the status of the current tail descriptor
    // if (receive_ring[current_tail].status & 0x1) { // DD (Descriptor Done) bit
    //     // The descriptor contains a valid packet
    //     // char *received_data = (char *)rx_buffers[current_tail];
    //     // uint16_t received_length = receive_ring[current_tail].length;

    //     // // Print the received packet for debugging
    //     // printf("Received packet: %.*s\n", received_length, received_data);

    //     // Clear the status of the descriptor for reuse
    //     receive_ring[current_tail].status = 0;

    //     // Update the RDT register to indicate the descriptor is ready for reuse
    //     current_tail = (current_tail + 1) % RECEIVE_RING_SIZE;
    //     e1000_mmio[E1000_REG_RDT / 4] = current_tail;
    // } else {
    //     // No packet is available in the current descriptor
    //     //printf("No packet received.\n");
    // }
}

void e1000_isr() {
    uint32_t icr = e1000_read_reg(E1000_ICR);

    if (icr & (1 << 7)) { // RXDMT (Receive Descriptor Minimum Threshold Reached)
        check_received_packet();
    }

    if (icr & (1 << 1)) { // TXDW (Transmit Descriptor Written Back)
        printf("Transmit interrupt triggered.\n");
    }

    if (icr & (1 << 6)) { // RXO (Receiver Overrun)
        printf("Receiver overrun occurred.\n");
    }

    // Acknowledge the interrupt
    outb(PIC1_COMMAND, 0x20);
}



#define REG_RCTRL       0x0100

void setup_transmit_ring() {

    uint8_t * ptr;
    struct e1000_tx_desc *descs;

    ptr = (uint8_t *)(k_malloc(sizeof(struct e1000_tx_desc)*E1000_NUM_TX_DESC + 16));
    descs = (struct e1000_tx_desc *)ptr;

    for(int i = 0; i < E1000_NUM_TX_DESC; i++)
    {
        tx_descs[i] = (struct e1000_tx_desc *)((uint8_t*)descs + i*16);
        tx_descs[i]->buffer_addr = 0;
        tx_descs[i]->cmd = 0;
        tx_descs[i]->status = TSTA_DD;
    }

    e1000_write_reg(REG_TXDESCHI, (uint32_t)((uint64_t)ptr >> 32) );
    e1000_write_reg(REG_TXDESCLO, (uint32_t)((uint64_t)ptr & 0xFFFFFFFF));


    //now setup total length of descriptors
    e1000_write_reg(REG_TXDESCLEN, E1000_NUM_TX_DESC * 16);


    //setup numbers
    e1000_write_reg( REG_TXDESCHEAD, 0);
    e1000_write_reg( REG_TXDESCTAIL, 0);
    tx_cur = 0;
    e1000_write_reg(REG_TCTRL,  TCTL_EN
        | TCTL_PSP
        | (15 << TCTL_CT_SHIFT)
        | (64 << TCTL_COLD_SHIFT)
        | TCTL_RTLC);

    // This line of code overrides the one before it but I left both to highlight that the previous one works with e1000 cards, but for the e1000e cards 
    // you should set the TCTRL register as follows. For detailed description of each bit, please refer to the Intel Manual.
    // In the case of I217 and 82577LM packets will not be sent if the TCTRL is not configured using the following bits.
    e1000_write_reg(REG_TCTRL,  0b0110000000000111111000011111010);
    e1000_write_reg(REG_TIPG,  0x0060200A);

    printf("Transmit ring and buffers initialized.\n");
}

void setup_receive_ring() {
    uint8_t * ptr;
    struct e1000_rx_desc *descs;

    // Allocate buffer for receive descriptors. For simplicity, in my case khmalloc returns a virtual address that is identical to it physical mapped address.
    // In your case you should handle virtual and physical addresses as the addresses passed to the NIC should be physical ones
 
    ptr = (uint8_t *)(k_malloc(sizeof(struct e1000_rx_desc)*E1000_NUM_RX_DESC + 16));

    descs = (struct e1000_rx_desc *)ptr;
    for(int i = 0; i < E1000_NUM_RX_DESC; i++)
    {
        rx_descs[i] = (struct e1000_rx_desc *)((uint8_t *)descs + i*16);
        rx_descs[i]->buffer_addr = (uint64_t)(uint8_t *)(k_malloc(8192 + 16));
        rx_descs[i]->status = 0;
    }

    e1000_write_reg(REG_TXDESCLO, (uint32_t)((uint64_t)ptr >> 32) );
    e1000_write_reg(REG_TXDESCHI, (uint32_t)((uint64_t)ptr & 0xFFFFFFFF));

    e1000_write_reg(REG_RXDESCLO, (uint64_t)ptr);
    e1000_write_reg(REG_RXDESCHI, 0);

    e1000_write_reg(REG_RXDESCLEN, E1000_NUM_RX_DESC * 16);

    e1000_write_reg(REG_RXDESCHEAD, 0);
    e1000_write_reg(REG_RXDESCTAIL, E1000_NUM_RX_DESC-1);
    rx_cur = 0;
    e1000_write_reg(REG_RCTRL, RCTL_EN| RCTL_SBP| RCTL_UPE | RCTL_MPE | RCTL_LBM_NONE | RTCL_RDMTS_HALF | RCTL_BAM | RCTL_SECRC  | RCTL_BSIZE_8192);
}

void process_packet(void *packet, size_t length) {
    // Process the packet here
    printf("Received packet: %.*s\n", length, (char *)packet);
}

void e1000_init(uint8_t bus, uint8_t device, uint8_t function) {
    // Get BAR0 (Base Address Register 0)
    uint32_t bar0 = pci_read(bus, device, function, 0x10) & ~0xF;
    e1000_mmio = (uint32_t *)bar0;

    // Enable bus mastering
    pci_set_bus_master(bus, device, 1);

    e1000_enable_interrupts();
    register_interrupt_handler(device_irq, e1000_isr);
    setup_receive_ring();
    setup_transmit_ring();

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

                    // IRQ für das Gerät auslesen
                    device_irq = pci_get_irq(bus, device, function);
                    printf("E1000 found: Bus %u, Device %u, IRQ %u, Function %u\n", bus, device, device_irq, function);

                    e1000_power_on_and_reset(bus, device);
                    e1000_init(bus, device, function);
                    get_mac_address();

                    return;
                }
            }
        }
    }
}

void e1000_send_packet(void *packet, size_t length) {

    //e1000_enable_loopback();

    tx_descs[tx_cur]->buffer_addr = (uint64_t)packet;
    tx_descs[tx_cur]->length = length;
    tx_descs[tx_cur]->cmd = CMD_EOP | CMD_IFCS | CMD_RS;
    tx_descs[tx_cur]->status = 0;
    uint8_t old_cur = tx_cur;
    tx_cur = (tx_cur + 1) % E1000_NUM_TX_DESC;

    e1000_write_reg(REG_TXDESCTAIL, tx_cur);   

    while(!(tx_descs[old_cur]->status & 0xff));    

    hex_dump(tx_descs[tx_cur], length);

    printf("Packet sent successfully.\n");
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


    e1000_send_packet(data, sizeof(data));
}
