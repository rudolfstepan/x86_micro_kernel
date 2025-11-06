#!/bin/bash
# Script to mount and populate disk images with test files

MOUNT_DIR="/mnt/kernel_disk"

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "This script must be run as root (use sudo)"
    exit 1
fi

# Create mount directory
mkdir -p "$MOUNT_DIR"

# Function to mount and populate a disk
mount_and_populate() {
    local disk_image=$1
    local disk_name=$2
    
    echo "Mounting $disk_image..."
    mount -o loop "$disk_image" "$MOUNT_DIR"
    
    if [ $? -eq 0 ]; then
        echo "Creating test directory structure on $disk_name..."
        mkdir -p "$MOUNT_DIR/TEST"
        mkdir -p "$MOUNT_DIR/DOCS"
        mkdir -p "$MOUNT_DIR/BIN"
        
        # Create some test files
        echo "Hello from $disk_name!" > "$MOUNT_DIR/README.TXT"
        echo "Test file in TEST directory" > "$MOUNT_DIR/TEST/TEST.TXT"
        echo "Documentation file" > "$MOUNT_DIR/DOCS/MANUAL.TXT"
        
        # List contents
        echo "Contents of $disk_name:"
        ls -la "$MOUNT_DIR"
        
        # Unmount
        echo "Unmounting $disk_image..."
        umount "$MOUNT_DIR"
        echo "✓ $disk_name populated successfully"
    else
        echo "✗ Failed to mount $disk_image"
    fi
    echo ""
}

# Populate disk.img
if [ -f "disk.img" ]; then
    mount_and_populate "disk.img" "disk.img (hdd0)"
else
    echo "disk.img not found"
fi

# Populate disk1.img
if [ -f "disk1.img" ]; then
    mount_and_populate "disk1.img" "disk1.img (hdd1)"
else
    echo "disk1.img not found"
fi

# Populate floppy.img
if [ -f "floppy.img" ]; then
    mount_and_populate "floppy.img" "floppy.img (fd0)"
else
    echo "floppy.img not found"
fi

# Cleanup
rmdir "$MOUNT_DIR" 2>/dev/null

echo "All disk images populated!"
echo ""
echo "You can now run: make run"
