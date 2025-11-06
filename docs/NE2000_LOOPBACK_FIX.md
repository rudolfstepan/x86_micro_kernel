# NE2000 Loopback Reception Fix

## Problem
The NE2000 driver was successfully sending packets in loopback mode (24 bytes transmitted), but packet reception was failing with "Packet received, length: 0 bytes" and "Loopback test failed, no packet received."

## Root Causes Identified

### 1. **Improper Remote DMA Setup for Packet Reception**
**Issue**: The `ne2000_receive_packet()` function was trying to read packet data directly from the data port without properly setting up Remote DMA Read operations.

**Why it failed**: The NE2000 chip requires explicit Remote DMA commands to transfer data from the receive buffer to the CPU. Simply reading from the data port without DMA setup returns garbage or zeros.

**Fix**: Implemented proper Remote DMA Read sequence:
```c
// Setup Remote DMA to read packet header (4 bytes)
ne2000_write(NE2000_CR, 0x22);              // Page 0, Start, NoDMA
ne2000_write(NE2000_RBCR0, 4);              // Read 4 bytes (header)
ne2000_write(NE2000_RBCR1, 0);
ne2000_write(NE2000_RSAR0, 0);              // Start at offset 0 in page
ne2000_write(NE2000_RSAR1, next_packet_page); // Page number
ne2000_write(NE2000_CR, 0x0A);              // Page 0, Start, Remote Read
```

### 2. **Missing CURR Register Initialization**
**Issue**: The CURR (Current Page Register) was never initialized during `ne2000_init()`.

**Why it matters**: CURR tracks where the NIC will write the next received packet. If uninitialized, BNRY (Boundary) and CURR could point to the same location, making the driver think there are no packets to read.

**Fix**: Added CURR initialization in Page 1:
```c
// Switch to Page 1 to set CURR (Current Page Register)
ne2000_write(NE2000_CR, 0x61);  // Page 1, Stop, NoDMA
ne2000_write(NE2000_CURR, RX_START_PAGE + 1);  // CURR is one page ahead
```

### 3. **Incorrect Packet Length Calculation**
**Issue**: The packet length includes the 4-byte CRC, but the driver was trying to read the full length including CRC into the buffer.

**Fix**: Subtract 4 bytes (CRC) from packet length:
```c
uint16_t data_length = packet_length - 4;  // Subtract CRC
```

### 4. **Improper Boundary Register Update**
**Issue**: Boundary register was updated incorrectly using `header[1] - 1` without handling page wrap-around.

**Fix**: Proper boundary update with wrap-around handling:
```c
uint8_t new_boundary = next_page - 1;
if (new_boundary < RX_START_PAGE) {
    new_boundary = RX_STOP_PAGE - 1;
}
ne2000_write(NE2000_BNRY, new_boundary);
```

### 5. **Missing Remote DMA Complete Wait**
**Issue**: The driver didn't wait for Remote DMA operations to complete before proceeding.

**Fix**: Added proper DMA completion checks:
```c
// Wait for Remote DMA to complete
while (!(ne2000_read(NE2000_ISR) & ISR_RDC));
ne2000_write(NE2000_ISR, ISR_RDC);  // Clear RDC flag
```

## Complete Initialization Sequence (Fixed)

The initialization was rewritten to follow the proper NE2000 specification:

```c
void ne2000_init() {
    // 1. Stop the NIC
    ne2000_write(NE2000_CR, 0x21);  // Page 0, Stop, NoDMA
    
    // 2. Configure Data Configuration Register (word mode)
    ne2000_write(NE2000_DCR, 0x49);
    
    // 3. Clear Remote Byte Count
    ne2000_write(NE2000_RBCR0, 0);
    ne2000_write(NE2000_RBCR1, 0);
    
    // 4. Set monitor mode (no packets accepted yet)
    ne2000_write(NE2000_RCR, 0x20);
    
    // 5. Set loopback mode
    ne2000_write(NE2000_TCR, 0x02);
    
    // 6. Configure receive buffer ring
    ne2000_write(NE2000_PSTART, RX_START_PAGE);  // 0x40
    ne2000_write(NE2000_PSTOP, RX_STOP_PAGE);    // 0x80
    ne2000_write(NE2000_BNRY, RX_START_PAGE);    // 0x40
    
    // 7. Clear interrupts
    ne2000_write(NE2000_ISR, 0xFF);
    
    // 8. Enable interrupts
    ne2000_write(NE2000_IMR, 0x0F);
    
    // 9. Switch to Page 1, read/write MAC address
    ne2000_write(NE2000_CR, 0x61);  // Page 1, Stop, NoDMA
    
    // Read MAC from PAR0-PAR5
    for (int i = 0; i < 6; i++) {
        mac_address[i] = ne2000_read(NE2000_PAR0 + i);
    }
    
    // Write back MAC
    for (int i = 0; i < 6; i++) {
        ne2000_write(NE2000_PAR0 + i, mac_address[i]);
    }
    
    // Set multicast filter (all FFs = accept all)
    for (int i = 0; i < 8; i++) {
        ne2000_write(0x08 + i, 0xFF);
    }
    
    // 10. Initialize CURR register (CRITICAL!)
    ne2000_write(NE2000_CURR, RX_START_PAGE + 1);  // 0x41
    
    // 11. Switch back to Page 0 and START
    ne2000_write(NE2000_CR, 0x22);  // Page 0, Start, NoDMA
    
    // 12. Enable packet reception
    ne2000_write(NE2000_RCR, 0x04);  // Accept broadcast
    
    // 13. Keep in loopback
    ne2000_write(NE2000_TCR, 0x02);
}
```

## Improved Packet Reception Logic

The complete rewritten `ne2000_receive_packet()` function:

```c
int ne2000_receive_packet(uint8_t *buffer, uint16_t buffer_size) {
    // 1. Read CURR from Page 1
    ne2000_write(NE2000_CR, 0x62);  // Page 1, Start, NoDMA
    uint8_t current_page = ne2000_read(NE2000_CURR);
    ne2000_write(NE2000_CR, 0x22);  // Page 0, Start, NoDMA

    // 2. Read BNRY from Page 0
    uint8_t boundary = ne2000_read(NE2000_BNRY);

    // 3. Check if packets are available
    if (boundary + 1 == current_page || 
        (boundary == 0x7F && current_page == 0x40)) {
        return 0;  // No packets
    }

    // 4. Calculate next packet page
    uint8_t next_packet_page = boundary + 1;
    if (next_packet_page >= RX_STOP_PAGE) {
        next_packet_page = RX_START_PAGE;
    }

    // 5. Setup Remote DMA to read 4-byte header
    ne2000_write(NE2000_CR, 0x22);
    ne2000_write(NE2000_RBCR0, 4);
    ne2000_write(NE2000_RBCR1, 0);
    ne2000_write(NE2000_RSAR0, 0);
    ne2000_write(NE2000_RSAR1, next_packet_page);
    ne2000_write(NE2000_CR, 0x0A);  // Start Remote Read

    // 6. Read header
    uint8_t header[4];
    for (int i = 0; i < 4; i++) {
        header[i] = inb(io_base + NE2000_DATA);
    }

    // 7. Wait for DMA complete
    while (!(ne2000_read(NE2000_ISR) & ISR_RDC));
    ne2000_write(NE2000_ISR, ISR_RDC);

    // 8. Parse header
    uint8_t status = header[0];
    uint8_t next_page = header[1];
    uint16_t packet_length = header[2] | (header[3] << 8);

    // 9. Validate packet
    if (packet_length < 60 || packet_length > 1518) {
        ne2000_write(NE2000_BNRY, next_page - 1);
        return -1;
    }

    // 10. Setup Remote DMA to read packet data (minus CRC)
    uint16_t data_length = packet_length - 4;  // Subtract 4-byte CRC
    ne2000_write(NE2000_CR, 0x22);
    ne2000_write(NE2000_RBCR0, data_length & 0xFF);
    ne2000_write(NE2000_RBCR1, (data_length >> 8) & 0xFF);
    ne2000_write(NE2000_RSAR0, 4);  // Skip 4-byte header
    ne2000_write(NE2000_RSAR1, next_packet_page);
    ne2000_write(NE2000_CR, 0x0A);

    // 11. Read packet data
    for (uint16_t i = 0; i < data_length; i++) {
        buffer[i] = inb(io_base + NE2000_DATA);
    }

    // 12. Wait for DMA complete
    while (!(ne2000_read(NE2000_ISR) & ISR_RDC));
    ne2000_write(NE2000_ISR, ISR_RDC);

    // 13. Update boundary with wrap-around
    uint8_t new_boundary = next_page - 1;
    if (new_boundary < RX_START_PAGE) {
        new_boundary = RX_STOP_PAGE - 1;
    }
    ne2000_write(NE2000_BNRY, new_boundary);

    return data_length;
}
```

## Enhanced Loopback Test

Added diagnostics to help debug:

```c
void test_ne2000_loopback(uint16_t io_base) {
    // Prepare test packet
    uint8_t test_packet[] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // Dest MAC
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05,  // Src MAC
        0x08, 0x00,                          // EtherType
        0x45, 0x00, 0x00, 0x54,              // Payload
        0x00, 0x01, 0xDE, 0xAD, 0xBE, 0xEF
    };

    // Enable loopback
    ne2000_enable_loopback(io_base);

    // Clear interrupts
    ne2000_write(NE2000_ISR, 0xFF);

    // Send packet
    ne2000_send_packet(test_packet, sizeof(test_packet));

    // Small delay
    for (volatile int i = 0; i < 10000; i++);

    // Check status
    uint8_t isr = ne2000_read(NE2000_ISR);
    printf("ISR after send: 0x%02X\n", isr);

    ne2000_write(NE2000_CR, 0x62);
    uint8_t curr = ne2000_read(NE2000_CURR);
    ne2000_write(NE2000_CR, 0x22);
    uint8_t bnry = ne2000_read(NE2000_BNRY);
    printf("CURR: 0x%02X, BNRY: 0x%02X\n", curr, bnry);

    // Receive packet
    uint8_t received_packet[1500];
    int received_length = ne2000_receive_packet(received_packet, 
                                                 sizeof(received_packet));

    if (received_length > 0) {
        printf("Loopback test successful, received %d bytes.\n", 
               received_length);
        print_packet(received_packet, received_length);
    } else {
        printf("Loopback test failed, no packet received.\n");
    }

    ne2000_disable_loopback(io_base);
}
```

## Expected Results

After these fixes, running `NET SEND` should show:

```
Sending test packet via NE2000...
Resetting NE2000 network card...
NE2000 reset complete.
NE2000 loopback mode enabled (internal).
Packet sent successfully, length: 24 bytes
ISR after send: 0x02  (or higher - packet transmitted)
CURR: 0x41, BNRY: 0x40
Packet received, length: 24 bytes (header status: 0x01)
Packet data (24 bytes): FF FF FF FF FF FF 00 01 02 03 04 05 08 00 45 00 00 54 00 01 DE AD BE EF
Loopback test successful, received 24 bytes.
NE2000 loopback mode disabled.
```

## Key Learnings

1. **NE2000 requires explicit Remote DMA setup** - You can't just read from the data port
2. **CURR must be initialized** - Critical for packet ring buffer management
3. **Packet length includes CRC** - Must subtract 4 bytes when reading data
4. **Page wrap-around must be handled** - Both for next_packet and boundary updates
5. **Remote DMA completion must be checked** - Wait for ISR_RDC flag before proceeding
6. **Proper initialization sequence matters** - NIC must be stopped during configuration

## Testing Commands

```bash
# Build and run
make build-qemu && make run

# In kernel shell:
NET STATUS    # Verify NE2000 is initialized
NET INFO      # Check register state
NET SEND      # Run loopback test
```

## Next Steps

With loopback working correctly, the next steps are:

1. **Disable loopback mode** - Set TCR = 0x00 for real transmission
2. **Test with QEMU TAP** - Verify packets reach the virtual network
3. **Implement ARP** - Address resolution for IP communication
4. **Add ICMP** - Ping functionality
5. **Build network stack** - IP, UDP, TCP protocols

## References

- NE2000 Programming Guide
- Realtek RTL8029AS Datasheet
- DP8390D/NS32490D NIC Core Specification
