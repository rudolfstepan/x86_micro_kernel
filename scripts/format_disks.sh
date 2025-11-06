#!/bin/bash
# Script to format disk images for the x86 microkernel

echo "Formatting disk images..."

# Check for mkfs.fat or mkfs.vfat
if command -v /usr/sbin/mkfs.fat &> /dev/null; then
    MKFS="/usr/sbin/mkfs.fat"
elif command -v /usr/sbin/mkfs.vfat &> /dev/null; then
    MKFS="/usr/sbin/mkfs.vfat"
elif command -v mkfs.fat &> /dev/null; then
    MKFS="mkfs.fat"
elif command -v mkfs.vfat &> /dev/null; then
    MKFS="mkfs.vfat"
else
    echo "Error: mkfs.fat or mkfs.vfat not found. Install dosfstools package:"
    echo "  sudo apt-get install dosfstools"
    exit 1
fi

# Format disk.img as FAT32
echo "Creating FAT32 filesystem on disk.img (50MB)..."
if [ -f disk.img ]; then
    $MKFS -F 32 -n "DISK0" disk.img
    echo "✓ disk.img formatted as FAT32"
else
    echo "Creating disk.img (50MB)..."
    dd if=/dev/zero of=disk.img bs=1M count=50 2>/dev/null
    $MKFS -F 32 -n "DISK0" disk.img
    echo "✓ disk.img created and formatted as FAT32"
fi

# Format floppy.img as FAT12
echo "Creating FAT12 filesystem on floppy.img (1.44MB)..."
if [ -f floppy.img ]; then
    $MKFS -F 12 -n "FLOPPY" floppy.img
    echo "✓ floppy.img formatted as FAT12"
else
    echo "Creating floppy.img (1.44MB)..."
    dd if=/dev/zero of=floppy.img bs=1k count=1440 2>/dev/null
    $MKFS -F 12 -n "FLOPPY" floppy.img
    echo "✓ floppy.img created and formatted as FAT12"
fi

# Optional: Create a second disk image
if [ ! -f disk1.img ]; then
    echo "Creating disk1.img (50MB)..."
    dd if=/dev/zero of=disk1.img bs=1M count=50 2>/dev/null
    mkfs.fat -F 32 -n "DISK1" disk1.img
    echo "✓ disk1.img created and formatted as FAT32"
fi

echo ""
echo "Disk images formatted successfully!"
echo ""
echo "You can now mount and add files to these images:"
echo "  sudo mkdir -p /mnt/disk0"
echo "  sudo mount -o loop disk.img /mnt/disk0"
echo "  sudo cp <files> /mnt/disk0/"
echo "  sudo umount /mnt/disk0"
