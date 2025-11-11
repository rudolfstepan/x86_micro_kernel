#!/bin/bash
# Create bootable disk image with GRUB for x86 microkernel
# This replaces the need for ISO/CD-ROM booting

set -e

echo "=========================================="
echo "Creating Bootable Disk Image"
echo "=========================================="

DISK_IMAGE="boot_disk.img"
DISK_SIZE="100M"
MOUNT_POINT="/tmp/boot_disk_mount_$$"  # Unique mount point per process
KERNEL_BIN="build/kernel.bin"

# Check for required tools
REQUIRED_TOOLS="dd parted losetup mkfs.vfat grub-install"
MISSING_TOOLS=""

for tool in $REQUIRED_TOOLS; do
    if ! command -v $tool &> /dev/null; then
        MISSING_TOOLS="$MISSING_TOOLS $tool"
    fi
done

if [ -n "$MISSING_TOOLS" ]; then
    echo "Error: Missing required tools:$MISSING_TOOLS"
    echo ""
    echo "Install with:"
    echo "  sudo apt-get install grub-pc-bin grub2-common parted dosfstools"
    exit 1
fi

# Check if kernel exists
if [ ! -f "$KERNEL_BIN" ]; then
    echo "Error: Kernel binary not found at $KERNEL_BIN"
    echo "Run 'make kernel' first"
    exit 1
fi

# Check if disk image already exists
if [ -f "$DISK_IMAGE" ]; then
    echo "Bootable disk already exists, updating kernel only..."
    
    # Setup loop device to access the partition
    LOOP_DEV=$(sudo losetup -f --show -P "$DISK_IMAGE")
    sleep 1
    
    # Use mtools to update kernel directly on the partition device
    echo "Updating kernel..."
    MTOOLS_SKIP_CHECK=1 sudo mcopy -i "${LOOP_DEV}p1" -o "$KERNEL_BIN" ::/boot/kernel.bin
    
    # Detach loop device
    sudo losetup -d "$LOOP_DEV"
    
    echo ""
    echo "=========================================="
    echo "✅ Kernel updated in: $DISK_IMAGE"
    echo "=========================================="
    exit 0
fi

# Create new disk image
echo "Creating ${DISK_SIZE} disk image..."
dd if=/dev/zero of=$DISK_IMAGE bs=1M count=100 status=progress

# Create partition table and partition
echo "Creating partition table..."
parted -s $DISK_IMAGE mklabel msdos
parted -s $DISK_IMAGE mkpart primary fat32 1MiB 100%
parted -s $DISK_IMAGE set 1 boot on

# Setup loop device
echo "Setting up loop device..."
LOOP_DEV=$(sudo losetup -f --show -P $DISK_IMAGE)
echo "Loop device: $LOOP_DEV"

# Wait for partition device
sleep 1
PART_DEV="${LOOP_DEV}p1"

# Format partition as FAT32
echo "Formatting partition as FAT32..."
sudo mkfs.vfat -F 32 $PART_DEV

# Mount partition
echo "Mounting partition..."
sudo mkdir -p $MOUNT_POINT
sudo mount $PART_DEV $MOUNT_POINT

# Create boot directory structure
echo "Creating boot directory structure..."
sudo mkdir -p $MOUNT_POINT/boot/grub

# Copy kernel
echo "Copying kernel..."
sudo cp $KERNEL_BIN $MOUNT_POINT/boot/kernel.bin

# Create GRUB configuration
echo "Creating GRUB configuration..."
sudo tee $MOUNT_POINT/boot/grub/grub.cfg > /dev/null << 'EOF'
# Serial console configuration for nographic mode
serial --unit=0 --speed=115200
terminal_input serial console
terminal_output serial console

set timeout=1
set default=0

menuentry "x86 Microkernel" {
    multiboot /boot/kernel.bin
    boot
}
EOF

# Install GRUB with FAT32 support
echo "Installing GRUB bootloader with FAT32 support..."
sudo grub-install --target=i386-pc --boot-directory=$MOUNT_POINT/boot --modules="fat part_msdos multiboot" $LOOP_DEV

# Sync filesystem
echo "Syncing filesystem..."
sudo sync
sleep 1

# Unmount and cleanup
echo "Cleaning up..."
sudo umount $MOUNT_POINT || sudo umount -l $MOUNT_POINT
sleep 0.5
sudo losetup -d $LOOP_DEV
sudo rmdir $MOUNT_POINT 2>/dev/null || true

echo ""
echo "=========================================="
echo "✅ Bootable disk image created: $DISK_IMAGE"
echo "=========================================="
echo ""
echo "Usage:"
echo "  QEMU:   qemu-system-i386 -m 512M -drive file=$DISK_IMAGE,format=raw"
echo "  VMware: Add as existing hard disk in VM settings"
echo ""
echo "Note: This disk can boot directly without CD-ROM/ISO"
