# Build Configuration - QEMU vs Real Hardware

## Overview

The kernel can be built in two modes to handle differences between QEMU emulation and real hardware behavior, particularly regarding ATA/IDE timing and FAT32 validation.

## Build Targets

### QEMU Build (Default)
```bash
make build-qemu
# or
make TARGET=qemu all
```

**Defines:**
- `-DQEMU_BUILD` - QEMU emulation mode
- `-DATA_RELAXED_TIMING` - Relaxed ATA timing constraints

**Characteristics:**
- **ATA Timeouts**: 500ms (QEMU is fast)
- **Polling Delay**: 1ms between status checks
- **Detection Timeout**: 100ms
- **Error Checking**: Basic (QEMU is predictable)
- **FAT32 Validation**: Relaxed (only essential fields)
- **Cache Flush**: Skipped (QEMU handles this automatically)

**Best for:**
- Development and testing in QEMU
- Fast iteration cycles
- Debugging with QEMU monitor

### Real Hardware Build
```bash
make build-real-hw
# or
make TARGET=real_hw all
```

**Defines:**
- `-DREAL_HARDWARE` - Real hardware mode
- `-DATA_STRICT_TIMING` - Strict ATA timing requirements
- `-DFAT32_STRICT_VALIDATION` - Comprehensive boot sector validation

**Characteristics:**
- **ATA Timeouts**: 5000ms (hardware can be slow)
- **Polling Delay**: 10ms between checks (avoid bus hogging)
- **Detection Timeout**: 500ms
- **Error Checking**: Comprehensive (ERR bit, DF bit, status validation)
- **FAT32 Validation**: Strict (all boot sector fields validated)
- **Cache Flush**: Explicit FLUSH CACHE command after writes

**Best for:**
- Deployment to physical machines
- USB boot sticks
- CD-ROM/ISO burning for real hardware

## Code Differences

### ATA Driver (`drivers/block/ata.c`)

#### Timing Constants
```c
#ifdef QEMU_BUILD
    #define ATA_WAIT_TIMEOUT_MS 500      // Fast
    #define ATA_POLL_DELAY_MS 1          // Frequent polling
#elif defined(REAL_HARDWARE)
    #define ATA_WAIT_TIMEOUT_MS 5000     // Patient
    #define ATA_POLL_DELAY_MS 10         // Less bus stress
#endif
```

#### Status Checking
**QEMU Mode:**
```c
while (inb(ATA_STATUS(base)) & 0x80) {  // Just check BSY
    if (timeout) return false;
    delay_ms(1);
}
```

**Real Hardware Mode:**
```c
while (1) {
    uint8_t status = inb(ATA_STATUS(base));
    
    if (status & 0x01) {  // Check ERR bit
        printf("ATA Error: Drive returned error status\n");
        return false;
    }
    
    if (!(status & 0x80)) break;  // BSY clear
    
    if (timeout) return false;
    delay_ms(10);
}
```

#### Write Operations
**QEMU Mode:**
- Write data
- Return immediately

**Real Hardware Mode:**
- Write data
- Wait for completion
- Issue FLUSH CACHE command (0xE7)
- Wait for flush completion

### FAT32 Filesystem (`fs/fat32/fat32.c`)

#### QEMU Mode (Relaxed)
```c
// Only check critical fields
if (boot_sector.bytesPerSector == 0 || boot_sector.sectorsPerCluster == 0) {
    return FAILURE;
}
```

#### Real Hardware Mode (Strict)
```c
// Validate boot signature
if (boot_sector.bootSignature != 0xAA55) {
    return FAILURE;
}

// Validate bytes per sector (must be 512, 1024, 2048, or 4096)
if (boot_sector.bytesPerSector != 512 && ...) {
    return FAILURE;
}

// Validate sectors per cluster (must be power of 2)
if (boot_sector.sectorsPerCluster == 0 || 
    (boot_sector.sectorsPerCluster & (boot_sector.sectorsPerCluster - 1)) != 0) {
    return FAILURE;
}

// Validate root cluster (must be >= 2)
if (boot_sector.rootCluster < 2) {
    return FAILURE;
}
```

## Usage Examples

### Development Workflow (QEMU)
```bash
# Quick build and test
make build-qemu
make run

# In QEMU, drives mount faster and are more forgiving
```

### Production Deployment (Real Hardware)
```bash
# Build for physical machine
make build-real-hw

# Write to USB stick
sudo dd if=kernel.iso of=/dev/sdX bs=4M status=progress

# Or burn to CD
cdrecord -v dev=/dev/sr0 kernel.iso
```

### Testing Both Modes
```bash
# Test QEMU build
make build-qemu && make run
# Try: MOUNT HDD0, LS

# Test real hardware build (still in QEMU, but with strict checks)
make build-real-hw && make run
# Same commands, but with stricter validation
```

## Troubleshooting

### QEMU Build Issues
**Symptom:** Drives not detected
**Solution:** Check QEMU command line - ensure drives are attached with `-drive file=disk.img,if=ide,index=0`

**Symptom:** Mount fails
**Solution:** Run disk image tests: `make test`

### Real Hardware Build Issues
**Symptom:** "ATA Error: Drive returned error status"
**Solution:** Hardware incompatibility - try increasing `ATA_WAIT_TIMEOUT_MS`

**Symptom:** "Invalid boot signature"
**Solution:** Disk not properly formatted - run `bash scripts/format_disks.sh`

**Symptom:** "Timeout: Drive not ready"
**Solution:** Slow hardware - increase timeouts or check cable connections

## Performance Implications

### QEMU Mode
- **Pros:** Faster boots, immediate feedback
- **Cons:** May miss hardware-specific issues

### Real Hardware Mode
- **Pros:** Catches real-world problems, more robust
- **Cons:** Slower due to longer timeouts and extra validation

## Recommended Usage

| Scenario | Build Mode | Reason |
|----------|-----------|---------|
| Development | QEMU | Fast iteration |
| Debugging | QEMU | Better tool integration |
| Testing | Both | Ensure compatibility |
| CI/CD | QEMU | Speed and reproducibility |
| Deployment | Real HW | Production robustness |
| USB Boot | Real HW | Physical hardware requirements |

## Adding New Hardware-Specific Code

When adding features that behave differently on QEMU vs real hardware:

```c
#ifdef QEMU_BUILD
    // QEMU-specific implementation
    // - Faster, simpler
    // - Assumes perfect conditions
#elif defined(REAL_HARDWARE)
    // Real hardware implementation
    // - Slower, more checks
    // - Handles edge cases
#else
    // Default/fallback implementation
#endif
```

## Build System Variables

The Makefile exposes these for custom builds:

```bash
# Override target
make TARGET=qemu all

# Check current target
make help | grep "Current TARGET"

# See compiler flags
make print-vars
```

## Future Enhancements

Potential additions:
- USB-specific timing adjustments
- SATA vs IDE detection and adaptation
- AHCI mode support flags
- NVMe support for modern hardware
- Automatic hardware detection and mode selection
