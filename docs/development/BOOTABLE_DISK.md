# Bootable Disk Image Guide

## Overview

Instead of using a CD-ROM ISO file, you can create a bootable hard disk image with GRUB installed. This is useful for:

- **VMware** - Add as a virtual hard disk (more realistic than CD-ROM)
- **Real hardware** - Write to USB stick or actual hard drive
- **QEMU** - Boot directly from disk without ISO
- **Performance** - Potentially faster boot times

## Quick Start

### Create Bootable Disk

```bash
# Build kernel and create bootable disk image
make bootdisk
```

This creates `boot_disk.img` (100MB) with:
- MBR partition table
- Ext2 filesystem
- GRUB bootloader installed
- Kernel at `/boot/kernel.bin`
- Serial console support

### Run in QEMU

```bash
# Boot from disk image
make run-disk
```

### Use in VMware

1. **Create/Edit VM Settings**
2. **Add Hard Disk** → **Use existing virtual disk**
3. **Browse** → Select `boot_disk.img`
4. **Disk type**: IDE
5. **Save and Boot**

## Manual Creation

If you need to customize the disk image:

```bash
# 1. Build kernel first
make kernel

# 2. Run the disk creation script
sudo ./scripts/create_bootable_disk.sh

# The script will:
#   - Create 100MB disk image
#   - Partition with MBR
#   - Format as ext2
#   - Install GRUB
#   - Copy kernel
#   - Configure GRUB (serial console)
```

## Disk Structure

```
boot_disk.img (100MB)
├── MBR (Master Boot Record)
├── Partition 1 (ext2, bootable)
│   └── /boot/
│       ├── kernel.bin
│       └── grub/
│           ├── grub.cfg
│           └── [GRUB modules]
```

## GRUB Configuration

The bootable disk uses the same GRUB configuration as the ISO:

```
# Serial console (for nographic mode)
serial --unit=0 --speed=115200
terminal_input serial console
terminal_output serial console

set timeout=1
set default=0

menuentry "x86 Microkernel" {
    multiboot /boot/kernel.bin
    boot
}
```

## Customization

### Change Disk Size

Edit `scripts/create_bootable_disk.sh`:

```bash
DISK_SIZE="100M"  # Change to desired size (e.g., "200M", "1G")
```

### Change Filesystem

Currently uses ext2. To use ext4:

```bash
# In create_bootable_disk.sh, change:
sudo mkfs.ext2 -F $PART_DEV
# To:
sudo mkfs.ext4 -F $PART_DEV
```

### Add Files to Disk

After creation, you can mount and add files:

```bash
# Mount the disk
sudo losetup -f --show -P boot_disk.img  # Note the loop device (e.g., /dev/loop0)
sudo mount /dev/loop0p1 /mnt

# Add files
sudo cp myfile.txt /mnt/boot/

# Unmount
sudo umount /mnt
sudo losetup -d /dev/loop0
```

## Writing to Physical Media

### USB Drive

```bash
# WARNING: This will ERASE the USB drive!
# Replace /dev/sdX with your USB device

# Find your USB device
lsblk

# Write the image
sudo dd if=boot_disk.img of=/dev/sdX bs=4M status=progress
sudo sync

# The USB drive is now bootable
```

### Hard Drive

Same as USB, but use the hard drive device (e.g., `/dev/sda`).

**⚠️ WARNING**: This will erase all data on the target drive!

## VMware Setup

### Method 1: Attach Existing Disk

1. Open VM Settings
2. Add → Hard Disk → Use existing virtual disk
3. Browse to `boot_disk.img`
4. Change type to IDE
5. Move to first boot position (drag in settings)

### Method 2: Convert to VMDK

```bash
# Convert raw image to VMware format
qemu-img convert -f raw -O vmdk boot_disk.img boot_disk.vmdk

# Then attach boot_disk.vmdk in VMware
```

### Method 3: Raw Disk Mapping (Advanced)

VMware can use raw disk images directly, but this is platform-specific.

## Troubleshooting

### GRUB Not Found

**Problem**: "GRUB" or "BOOTMGR not found" error

**Solution**:
1. Ensure GRUB installation succeeded during creation
2. Check that partition is marked as bootable
3. Verify disk boots with `-boot c` flag in QEMU

```bash
# Test boot in QEMU
qemu-system-i386 -m 512M -boot c -drive file=boot_disk.img,format=raw
```

### Kernel Not Loading

**Problem**: GRUB loads but kernel doesn't boot

**Solution**:
1. Verify kernel is at `/boot/kernel.bin` in the image
2. Check GRUB configuration syntax
3. Ensure multiboot header is in kernel binary

```bash
# Check multiboot header
readelf -h build/kernel.bin | grep -i multiboot
```

### Permission Denied

**Problem**: Script fails with permission errors

**Solution**: Run with sudo (script needs root for loopback mounting)

```bash
sudo ./scripts/create_bootable_disk.sh
```

### VMware Won't Boot

**Problem**: VMware shows "Operating system not found"

**Solution**:
1. Verify disk is first in boot order
2. Check that disk type is IDE (not SCSI)
3. Ensure "Legacy BIOS" not UEFI boot mode

## Advantages over ISO

| Feature | ISO (CD-ROM) | Bootable Disk |
|---------|--------------|---------------|
| Boot speed | Slower | Faster |
| Realistic | No (CD is read-only) | Yes (normal disk) |
| USB deployment | No | Yes |
| VMware/VirtualBox | CD-ROM device | Hard disk device |
| Modifiable | No | Yes (can mount and edit) |
| Multi-boot | Limited | Yes (GRUB chainloading) |

## Comparison with ISO Boot

### ISO (kernel.iso)
```bash
make iso
qemu-system-i386 -cdrom kernel.iso -boot d
```

**Pros:**
- Simple to create
- Widely compatible
- Easy to share

**Cons:**
- Requires CD-ROM device
- Slower boot
- Read-only

### Disk (boot_disk.img)
```bash
make bootdisk
qemu-system-i386 -drive file=boot_disk.img -boot c
```

**Pros:**
- More realistic (actual disk boot)
- Can write to USB/HDD
- Modifiable filesystem
- Faster boot

**Cons:**
- Requires sudo for creation
- Larger file size
- More complex setup

## Related Documentation

- [Quick Start](QUICKSTART.md) - General setup guide
- [VMware Setup](../hardware/VMWARE.md) - VMware-specific configuration
- [Build Modes](BUILD_MODES.md) - Different build targets

## Advanced: Multi-Boot Setup

You can install multiple kernels on the disk:

```bash
# Mount the disk
sudo mount /dev/loop0p1 /mnt

# Add another kernel
sudo cp other_kernel.bin /mnt/boot/

# Edit GRUB config
sudo nano /mnt/boot/grub/grub.cfg
```

Add additional menu entries:

```
menuentry "x86 Microkernel v1" {
    multiboot /boot/kernel.bin
    boot
}

menuentry "x86 Microkernel v2" {
    multiboot /boot/other_kernel.bin
    boot
}
```

## Cleanup

Remove bootable disk image:

```bash
rm boot_disk.img
```

Or use make target:

```bash
make clean  # Removes all build artifacts including disk images
```
