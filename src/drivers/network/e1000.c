#include "e1000.h"
#include "toolchain/stdio.h"
#include "toolchain/stdlib.h"
#include "toolchain/strings.h"
#include "drivers/pci.h"
#include "drivers/io/io.h"
#include "kernel/sys.h"
#include "kernel/memory.h"

uint32_t *e1000_mmio = NULL;
uint8_t device_irq = 11;

#define RX_BUFFER_SIZE 8192   // Size of each RX buffer

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
    printf("Packets received.\n");

}

void e1000_isr() {
    uint32_t icr = e1000_read_reg(E1000_ICR);

    if (icr & (1 << 7)) { // RXDMT (Receive Descriptor Minimum Threshold Reached)
        check_received_packet();
    }

    if (icr & (1 << 1)) { // TXDW (Transmit Descriptor Written Back)
        printf("Transmit interrupt triggered.\n");

        // Debugging only: Manually update the status
        tx_descs[old_cur].status = 0xFF;
    }

    if (icr & (1 << 6)) { // RXO (Receiver Overrun)
        printf("Receiver overrun occurred.\n");
    }

    // Acknowledge the interrupt
    e1000_write_reg(E1000_ICR, icr);
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

    initialize_rings_and_buffers();

    // Enable bus mastering
    pci_set_bus_master(bus, device, 1);

    e1000_enable_interrupts();
    register_interrupt_handler(device_irq, e1000_isr);
    // setup_receive_ring();
    // setup_transmit_ring();

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
    printf("Sending packet of length %u...\n", length);

    // 1. Verify Transmit Engine Configuration
    uint32_t tctl = e1000_read_reg(REG_TCTRL);
    printf("TCTRL: 0x%x\n", tctl);

    if (!(tctl & TCTL_EN)) {
        printf("Error: Transmit engine not enabled. Enabling now...\n");
        tctl |= TCTL_EN | TCTL_PSP | (15 << TCTL_CT_SHIFT) | (64 << TCTL_COLD_SHIFT);
        e1000_write_reg(REG_TCTRL, tctl);
    }

    // 2. Inspect Descriptor Tail Update
    printf("TX Tail before send: %d\n", e1000_read_reg(REG_TXDESCTAIL));
    

    // 3. Check Transmit Descriptor Ring Initialization
    for (int i = 0; i < E1000_NUM_TX_DESC; i++) {
        printf("TX Desc %d: Buffer Addr: %p, Length: %u, Status: %u\n", i, tx_descs[i].buffer_addr, tx_descs[i].length, tx_descs[i].status);
    }

    // 4. Inspect Head Pointer Progress
    uint32_t head = e1000_read_reg(REG_TXDESCHEAD);
    uint32_t tail = e1000_read_reg(REG_TXDESCTAIL);

    printf("TX Head: %d, TX Tail: %d\n", head, tail);


    // 5. Check Transmit Command (TCTL) Configuration
    e1000_write_reg(REG_TCTRL, TCTL_EN | TCTL_PSP | (15 << TCTL_CT_SHIFT) | (64 << TCTL_COLD_SHIFT));

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
    tx_descs[tx_cur].cmd = CMD_EOP | CMD_IFCS | CMD_RS;
    tx_descs[tx_cur].status = 0;

    // Debug the descriptor
    printf("Descriptor %d: Buffer Addr: %p, Status: %u\n", tx_cur, tx_descs[tx_cur].buffer_addr, tx_descs[tx_cur].status);

    // Update the tail register to signal the hardware
    old_cur = tx_cur;
    tx_cur = (tx_cur + 1) % E1000_NUM_TX_DESC;
    e1000_write_reg(REG_TXDESCTAIL, tx_cur);

    printf("TX Tail after send: %d\n", e1000_read_reg(REG_TXDESCTAIL));

    // Wait for transmission completion with timeout
    int timeout = 1000;
    while (!(tx_descs[old_cur].status & 0xff)) {
        if (--timeout == 0) {
            printf("Timeout! Descriptor %d status: 0x%x\n", old_cur, tx_descs[old_cur].status);
            printf("TX Head: %d, TX Tail: %d\n", e1000_read_reg(REG_TXDESCHEAD), e1000_read_reg(REG_TXDESCTAIL));
            return;
        }
        delay_ms(10); // Prevent tight busy-looping
    }


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
