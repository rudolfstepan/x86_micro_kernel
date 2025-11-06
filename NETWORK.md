# Network Implementation Guide

## ⚠️ TESTING STATUS & QEMU LIMITATIONS

**Current Implementation Status (November 2025):**
- ✅ NE2000 driver initialization: **WORKING**
- ✅ MAC address reading from PROM: **WORKING** (shows 52:00:34:12:34:56)
- ✅ Packet transmission (TX): **WORKING** (confirmed via ISR PTX bit)
- ✅ Register read/write operations: **WORKING** (all 16-step init complete)
- ❌ Packet reception (RX) via loopback: **NOT WORKING IN QEMU**

**QEMU NE2000 Emulation Limitation:**
QEMU's NE2000 emulation **filters out packets sent by the same device**, even when:
- Using promiscuous mode (RCR=0x10)
- Sending to broadcast address (FF:FF:FF:FF:FF:FF)
- Using external loopback mode (TCR=0x06)  
- Using socket multicast networking backend

This is a limitation of QEMU's emulation, **not a driver bug**. The driver correctly:
1. Transmits packets (verified by ISR PTX=0x01 after send)
2. Monitors buffer pointers (CURR advances properly during init)
3. Implements proper Remote DMA for RX (header + data reading)
4. Validates packet headers and lengths

**Testing Recommendations:**
- For TX testing: Use `NET SEND` - transmission works correctly
- For RX testing: Requires real hardware or better emulator
- Alternative: Implement E1000 driver (better QEMU emulation support)
- Network functionality can be verified on real x86 hardware with PCI NE2000 card

## Overview
The x86 microkernel has network driver support with focus on NE2000 PCI network cards. The implementation includes drivers for multiple network cards, but currently NE2000 is the most complete and enabled by default.

## Supported Network Cards

### 1. NE2000 (RTL8029) - **ACTIVE**
- **Status**: Implemented and enabled
- **Vendor ID**: 0x10EC (Realtek)
- **Device ID**: 0x8029
- **Driver**: `drivers/net/ne2000.c`
- **QEMU Support**: Yes (`-device ne2k_pci`)

### 2. Intel E1000
- **Status**: Implemented but disabled
- **Vendor ID**: 0x8086
- **Device ID**: 0x100E
- **Driver**: `drivers/net/e1000.c`
- **QEMU Support**: Yes (`-device e1000`)

### 3. RTL8139
- **Status**: Implemented but disabled
- **Vendor ID**: 0x10EC
- **Device ID**: 0x8139
- **Driver**: `drivers/net/rtl8139.c`
- **QEMU Support**: Yes (`-device rtl8139`)

### 4. VMware VMXnet3
- **Status**: Partially implemented, disabled
- **Driver**: `drivers/net/vmxnet3.c`
- **QEMU Support**: Limited

## Current Network Architecture

### Driver Stack
```
Application/Shell Commands
         ↓
   NET Command Handler (kernel/shell/command.c)
         ↓
   NE2000 Driver (drivers/net/ne2000.c)
         ↓
   PCI Bus Interface (drivers/bus/pci.c)
         ↓
   Hardware (QEMU emulated network card)
```

### NE2000 Driver Components

#### Initialization (`ne2000_init()`)
1. **Reset**: Software reset via 0x1F register
2. **MAC Address**: Read from PAR0-PAR5 registers (Page 1)
3. **Buffer Configuration**:
   - RX buffer: Pages 0x40 to 0x80
   - TX buffer: Page 0x40
4. **Interrupts**: IRQ 11 handler registered
5. **Mode**: Currently configured for loopback testing

#### Key Registers
- **CR (0x00)**: Command Register
- **PSTART (0x01)**: Page Start Register
- **PSTOP (0x02)**: Page Stop Register
- **BNRY (0x03)**: Boundary Pointer
- **TPSR (0x04)**: Transmit Page Start
- **ISR (0x07)**: Interrupt Status Register
- **RCR (0x0C)**: Receive Configuration
- **TCR (0x0D)**: Transmit Configuration
- **DCR (0x0E)**: Data Configuration
- **IMR (0x0F)**: Interrupt Mask

## Shell Commands

### NET Command
```bash
NET               # Show help
NET STATUS        # Show network interface status
NET INFO          # Show detailed register information
NET SEND          # Send test packet (loopback mode)
NET RECV          # Receive packet (not yet implemented)
```

### Example Usage
```
> NET STATUS
Network card: NE2000 (initialized)
NE2000 MAC address: 52:54:00:12:34:56

> NET INFO
=== NE2000 Network Card Status ===
IO Base Address: 0xC000
MAC Address: 52:54:00:12:34:56

Register Status:
  PSTART:  0x40 (RX buffer start page)
  PSTOP:   0x80 (RX buffer stop page)
  BNRY:    0x40 (Boundary pointer)
  TPSR:    0x40 (TX page start)
  ISR:     0x00 (Interrupt status)
  IMR:     0x01 (Interrupt mask)
  CURR:    0x41 (Current page)
==================================

> NET SEND
Sending test packet via NE2000...
Resetting NE2000 network card...
NE2000 reset complete.
NE2000 loopback mode enabled (internal).
Packet sent successfully, length: 30 bytes
```

## QEMU Network Configuration

### Current Setup (Makefile)
```bash
-device ne2k_pci,netdev=net0 -netdev user,id=net0
```

### Network Modes

#### 1. User Mode (Current)
- **Pros**: Simple, no host configuration needed
- **Cons**: Limited functionality, no raw packet access
- **Use case**: Basic testing

#### 2. TAP Mode (Advanced)
```bash
-netdev tap,id=net0,ifname=tap0,script=no,downscript=no
-device ne2k_pci,netdev=net0
```
**Requires**: 
- Root/sudo access
- TAP device setup on host
```bash
sudo ip tuntap add dev tap0 mode tap
sudo ip link set tap0 up
sudo ip addr add 10.0.0.1/24 dev tap0
```

#### 3. Bridge Mode (Production)
- Connect VM to physical network
- Requires bridge configuration on host

## Current Implementation Status

### ✅ Working
- [x] PCI device detection
- [x] NE2000 card initialization
- [x] MAC address reading
- [x] Register configuration
- [x] IRQ handler registration
- [x] Loopback mode packet transmission
- [x] Shell command interface
- [x] Status/diagnostics commands

### ⚠️ Partial/Testing
- [ ] Packet reception (implemented but needs testing)
- [ ] Loopback test completion
- [ ] Interrupt handling (registered but not verified)

### ❌ Not Implemented
- [ ] Full network stack (IP, TCP, UDP)
- [ ] ARP protocol
- [ ] ICMP (ping)
- [ ] DHCP client
- [ ] Socket API
- [ ] Multiple packet buffers
- [ ] DMA transfers
- [ ] Error handling and recovery
- [ ] Network statistics

## Known Issues

### 1. Loopback Mode Only
**Issue**: Card is configured in internal loopback mode (TCR = 0x02)
**Impact**: Packets don't reach the wire, only internal testing
**Fix**: Set TCR = 0x00 for normal transmission mode

### 2. Single Buffer
**Issue**: Using single TX buffer (page 0x40)
**Impact**: No packet queuing, must wait for each transmission
**Fix**: Implement circular buffer with multiple TX pages

### 3. No Receive Processing
**Issue**: RX interrupt handler reads packet but doesn't process
**Impact**: Cannot handle incoming network traffic
**Fix**: Implement packet processing pipeline

### 4. No Network Stack
**Issue**: No IP/TCP/UDP implementation
**Impact**: Cannot communicate with real network
**Fix**: Implement minimal network stack or integrate lwIP

## Testing Procedures

### 1. Basic Detection Test
```
> NET STATUS
```
Expected output: Shows NE2000 initialized with MAC address

### 2. Register Diagnostic
```
> NET INFO
```
Expected output: All registers showing reasonable values

### 3. Loopback Test
```
> NET SEND
```
Expected output: "Packet sent successfully"

### 4. External Ping Test (TAP mode required)
```bash
# On host (after TAP setup):
ping -c 3 10.0.0.2

# In kernel:
> NET RECV  # Should receive ARP/ICMP packets
```

## Development Roadmap

### Phase 1: Basic Functionality (Current)
- [x] Driver initialization
- [x] Loopback testing
- [x] Shell commands

### Phase 2: Real Networking
- [ ] Disable loopback mode
- [ ] Test packet transmission to wire
- [ ] Verify packet reception
- [ ] ARP protocol implementation
- [ ] ICMP echo (ping) response

### Phase 3: Network Stack
- [ ] IP header parsing/creation
- [ ] UDP socket implementation
- [ ] Basic TCP implementation
- [ ] DHCP client

### Phase 4: Advanced Features
- [ ] Multiple network interfaces
- [ ] E1000/RTL8139 driver completion
- [ ] Raw socket API
- [ ] Network statistics
- [ ] Performance optimization

## Code Structure

### Driver Files
```
drivers/net/
  ├── ne2000.c/h      # NE2000 driver (active)
  ├── e1000.c/h       # Intel E1000 driver
  ├── rtl8139.c/h     # Realtek RTL8139 driver
  ├── vmxnet3.c/h     # VMware virtual NIC
  └── ethernet.c/h    # Ethernet frame handling
```

### Integration Points
```
kernel/init/kernel.c:513    # ne2000_detect() call
kernel/shell/command.c:774  # NET command implementation
drivers/bus/pci.c:104       # get_io_base() helper
```

### Register Access Pattern
```c
// Read register
uint8_t value = ne2000_read(NE2000_ISR);

// Write register
ne2000_write(NE2000_CR, 0x22);  // Start + NoDMA

// I/O operations
outb(io_base + offset, value);
value = inb(io_base + offset);
```

## Debugging Tips

### 1. Check PCI Detection
Look for "NE2000 network card found at XX:XX.X" during boot

### 2. Verify IO Base
Should be non-zero (typically 0xC000 in QEMU)

### 3. Monitor ISR
```c
printf("ISR: 0x%02X\n", ne2000_read(NE2000_ISR));
```
- Bit 0: Packet received
- Bit 1: Packet transmitted
- Bit 6: Remote DMA complete

### 4. Check MAC Address
Should not be all zeros or all FFs

### 5. QEMU Monitor
```bash
# In QEMU monitor (Ctrl+Alt+2):
info network
info pci
```

## Performance Considerations

### Current Bottlenecks
1. **Polling**: Driver uses busy-wait loops
2. **No DMA**: Manual byte-by-byte transfers
3. **Single Buffer**: No packet queuing
4. **No Optimization**: Reference implementation

### Improvement Strategies
1. Use interrupt-driven I/O
2. Implement proper DMA transfers
3. Add multiple buffer rings
4. Optimize packet processing path
5. Add zero-copy mechanisms

## Security Considerations

### Current Issues
1. No input validation on packet size
2. No buffer overflow protection
3. No MAC filtering
4. Promiscuous mode possible
5. No encryption/authentication

### Required Hardening
- Input sanitization
- Bounds checking
- Rate limiting
- Access control
- Audit logging

## References

### Documentation
- NE2000 Programming Guide
- RTL8029 Datasheet (Realtek)
- Intel 8254x Family of Gigabit Ethernet Controllers
- PCI Local Bus Specification

### Code Examples
- Linux kernel drivers/net/ethernet/
- NetBSD sys/dev/ic/ne2000.c
- FreeBSD sys/dev/ed/

### Network Standards
- RFC 791 (IP)
- RFC 792 (ICMP)
- RFC 793 (TCP)
- RFC 768 (UDP)
- RFC 826 (ARP)

## Contributing

To add network features:

1. **Test existing functionality**: Use NET commands
2. **Read the datasheets**: Understand hardware behavior
3. **Add new functions**: Follow existing patterns
4. **Update this document**: Keep documentation current
5. **Test thoroughly**: Use both loopback and real network

## Conclusion

The network implementation is at a functional foundation level. The NE2000 driver can detect hardware, configure registers, and send packets in loopback mode. Next steps are to enable real network transmission, implement packet reception, and begin building a basic network stack.

For production use, consider:
- Switching to E1000 (better performance)
- Integrating lwIP network stack
- Implementing proper buffer management
- Adding comprehensive error handling
