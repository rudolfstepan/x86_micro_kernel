#!/bin/bash
# Create an EXT2 filesystem disk image for the x86 microkernel

set -e  # Exit on error

DISK_IMAGE="ext2_disk.img"
DISK_SIZE="10M"
MOUNT_POINT="/tmp/ext2_mount"

echo "========================================"
echo " EXT2 Disk Image Creator"
echo "========================================"

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "Please run as root (use sudo)"
    exit 1
fi

# Clean up any existing image
if [ -f "$DISK_IMAGE" ]; then
    echo "Removing existing disk image..."
    rm -f "$DISK_IMAGE"
fi

# Create empty disk image
echo "Creating ${DISK_SIZE} disk image..."
dd if=/dev/zero of="$DISK_IMAGE" bs=1M count=$(echo $DISK_SIZE | sed 's/M//') status=progress

# Format with EXT2
echo "Formatting with EXT2 filesystem..."
mkfs.ext2 -F "$DISK_IMAGE"

# Create mount point
echo "Creating mount point..."
mkdir -p "$MOUNT_POINT"

# Mount the image
echo "Mounting disk image..."
mount -o loop "$DISK_IMAGE" "$MOUNT_POINT"

# Create test directory structure
echo "Creating test directories and files..."

# Create directories
mkdir -p "$MOUNT_POINT/bin"
mkdir -p "$MOUNT_POINT/etc"
mkdir -p "$MOUNT_POINT/usr"
mkdir -p "$MOUNT_POINT/usr/bin"
mkdir -p "$MOUNT_POINT/home"
mkdir -p "$MOUNT_POINT/tmp"

# Create test files
echo "This is a test file in root directory" > "$MOUNT_POINT/readme.txt"
echo "Welcome to the EXT2 filesystem!" > "$MOUNT_POINT/welcome.txt"

# Create files in subdirectories
echo "echo 'Hello from test script'" > "$MOUNT_POINT/bin/test.sh"
echo "Configuration file" > "$MOUNT_POINT/etc/config.conf"
echo "User data file" > "$MOUNT_POINT/home/userdata.txt"

# Create a larger test file
echo "Creating large test file..."
dd if=/dev/urandom of="$MOUNT_POINT/usr/testdata.bin" bs=1K count=100 2>/dev/null

# Set permissions
chmod 755 "$MOUNT_POINT/bin"
chmod 755 "$MOUNT_POINT/usr/bin"
chmod 644 "$MOUNT_POINT/readme.txt"
chmod 644 "$MOUNT_POINT/welcome.txt"

# Display filesystem info
echo ""
echo "========================================"
echo " Filesystem Information"
echo "========================================"
df -h "$MOUNT_POINT"
echo ""
dumpe2fs "$DISK_IMAGE" | grep -i "Block size"
dumpe2fs "$DISK_IMAGE" | grep -i "Inode size"
dumpe2fs "$DISK_IMAGE" | grep -i "Inode count"

# List contents
echo ""
echo "========================================"
echo " Directory Structure"
echo "========================================"
tree "$MOUNT_POINT" || find "$MOUNT_POINT" -type f -o -type d

# Unmount
echo ""
echo "Unmounting disk image..."
umount "$MOUNT_POINT"

# Remove mount point
rmdir "$MOUNT_POINT"

# Make image writable by all
chmod 666 "$DISK_IMAGE"

echo ""
echo "========================================"
echo " SUCCESS!"
echo "========================================"
echo "EXT2 disk image created: $DISK_IMAGE"
echo "Size: $DISK_SIZE"
echo ""
echo "To attach to QEMU, add to your run script:"
echo "  -drive file=$DISK_IMAGE,format=raw,if=ide"
echo ""
echo "The filesystem contains:"
echo "  /bin/          - Binary directory"
echo "  /etc/          - Configuration directory"
echo "  /usr/          - User programs directory"
echo "  /usr/bin/      - User binaries"
echo "  /home/         - Home directory"
echo "  /tmp/          - Temporary directory"
echo "  /readme.txt    - Test text file"
echo "  /welcome.txt   - Welcome message"
echo ""
