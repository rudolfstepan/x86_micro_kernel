# VMware Workstation Setup Guide

## Quick Start

Build the kernel for VMware:
```bash
make build-vmware
```

This creates `kernel.iso` optimized for VMware Workstation with:
- Intel E1000 network driver enabled
- VMware-specific ATA timing (3s timeouts, longer delays)
- Proper drive selection delays for VMware's IDE emulation

## VMware VM Configuration

### 1. Create New Virtual Machine
- Select: **Custom (advanced)**
- Hardware compatibility: Workstation 16.x or later

### 2. Install Operating System
- Select: **I will install the operating system later**

### 3. Select Guest Operating System
- Guest OS: **Other**
- Version: **Other (32-bit)** ← Important!

### 4. Name and Location
- VM name: `x86_microkernel`
- Location: Choose your preferred directory

### 5. Processor Configuration
- Number of processors: **1**
- Number of cores per processor: **1**

### 6. Memory
- Memory for this VM: **64 MB** (sufficient for microkernel)
- Or use **128 MB** for more headroom

### 7. Network Type
- Select: **Use bridged networking** (for LAN access)
- Or: **Use NAT** (for internet through host)

### 8. I/O Controller
- Select: **LSI Logic** (recommended)

### 9. Virtual Disk Type
- Select: **IDE** (required - kernel uses ATA driver)

### 10. Disk
- **Create a new virtual disk** (or use existing if you have one)
- Disk size: 512 MB - 1 GB is sufficient
- Split disk into multiple files: Optional

### 11. CRITICAL: Configure Network Adapter
After creating the VM:
1. Go to **VM Settings** → **Network Adapter**
2. Click **Advanced**
3. **Adapter Type**: Select **Intel E1000** ← MUST DO THIS!
4. Click OK

### 12. Mount ISO
1. **VM Settings** → **CD/DVD (IDE)**
2. Select **Use ISO image**
3. Browse to: `kernel.iso`
4. Check **Connect at power on**

### 13. Boot Order (Optional)
1. **VM Settings** → **Options** → **Boot Options**
2. Ensure CD-ROM is first in boot order

## Network Configuration

Your kernel includes Intel E1000 driver which VMware emulates perfectly.

### In the Kernel Shell:
```bash
# Configure network interface
ifconfig 10.0.2.15 255.255.255.0 10.0.2.1

# Check network status
net status

# Listen for packets
net listen 10

# Send test packet
net send
```

### From Host (if using bridged networking):
```bash
# Ping the kernel (if ICMP reply is implemented)
ping 10.0.2.15

# Check ARP
arp -a | grep 10.0.2.15

# Monitor traffic
sudo tcpdump -i vmnet0 host 10.0.2.15
```

## Troubleshooting

### Network Not Working
1. **Check E1000**: Verify network adapter is Intel E1000 (not E1000e or other)
2. **Driver Init**: In kernel, check for "E1000 network card detected" message
3. **Network Mode**: Try switching between Bridged and NAT

### Kernel Won't Boot
1. **Check Guest OS**: Must be "Other (32-bit)"
2. **Check ISO**: Rebuild with `make build-vmware`
3. **Memory**: Increase to 128 MB if crashes

### Display Issues
1. Remove 3D acceleration (VM Settings → Display)
2. Use standard VGA
3. Try rebuilding with framebuffer: `make build-qemu-fb`

### Keyboard Not Responding
1. Click inside VM window to capture keyboard
2. Press Ctrl+G to release mouse/keyboard from VM
3. Check command loop is running (should see `>` prompt)

## Disk Images

To use the existing FAT32 disk images in VMware:

1. **VM Settings** → **Hard Disk** → **Remove** (default disk)
2. **Add** → **Hard Disk** → **Use an existing virtual disk**
3. Browse to: `disk.img` from the project
4. Select **IDE** as controller type

The kernel will auto-detect and mount the disk.

## Comparison: QEMU vs VMware

| Feature | QEMU | VMware |
|---------|------|--------|
| Boot Speed | Fast | Slower |
| Network | NE2000/E1000 | E1000 (better) |
| Debugging | GDB support | VMware debugger |
| Performance | Good | Excellent |
| Hardware Accuracy | Approximate | Very accurate |
| USB Support | Limited | Full support |

## Build Targets

```bash
# Build for VMware (current target)
make build-vmware

# Build for QEMU
make build-qemu

# Build for real hardware
make build-real-hw

# Show all options
make help
```

## Testing Network Stack

Once VM is running:

1. **Configure IP**:
   ```
   ifconfig 10.0.2.15 255.255.255.0 10.0.2.1
   ```

2. **Check Status**:
   ```
   net status
   net info
   ```

3. **Test Connectivity** (from host):
   ```bash
   # ARP scan
   sudo arping -I vmnet8 10.0.2.15
   
   # Ping (if implemented)
   ping 10.0.2.15
   ```

4. **Packet Capture** (from host):
   ```bash
   sudo tcpdump -i vmnet8 -XX
   ```

## Troubleshooting

### Keyboard Not Working

**Problem**: Keyboard input not recognized in VMware

**Solution**: The kernel now includes VMware-specific keyboard controller initialization:
- Enables keyboard controller (command 0xAE)
- Enables keyboard scanning (command 0xF4)
- Waits for proper acknowledgment

If keyboard still doesn't work:
1. Click inside VM window to ensure it has focus
2. Check VMware Tools is NOT installed (can interfere)
3. Verify "Grab keyboard and mouse on focus" is enabled in VM settings
4. Try disconnecting/reconnecting keyboard in VM menu

### ATA Drive Detection

**Problem**: Drives not detected or timeout errors

**Solution**: VMware build uses relaxed timing (3s timeouts vs 30s)
- Use `make build-vmware` for proper timing
- Check boot messages for drive detection
- Verify IDE controller (not SCSI) in VM settings

### Network Not Working

**Problem**: E1000 adapter not detected

**Solution**:
1. Verify network adapter is "Intel E1000" in VM settings
2. Check kernel boot message: "Intel E1000 detected"
3. Use `net status` to verify adapter state
4. Ensure bridge/NAT is configured correctly

## Notes

- VMware Workstation 16.x or later recommended
- Intel E1000 driver is production-ready and well-tested
- The kernel uses VGA text mode by default (80x25)
- Framebuffer support available with `build-qemu-fb` target
- Network stack supports: ARP, IP, ICMP (TCP/UDP in development)

## Resources

- Project README: `README.md`
- QEMU Setup: `QUICKSTART.md`
- Network Stack: `TAP_NETWORKING.md`
- Build System: `make help`
