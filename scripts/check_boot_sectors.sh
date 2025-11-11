#!/bin/bash
# Quick test script to check boot sector reading in QEMU

echo "Analyzing disk images..."
echo ""
echo "=== disk.img (HDD0) ==="
python3 scripts/dump_boot_sector.py disk.img | grep -E "(Bytes per|Sectors per|Root cluster|Boot Sector Signature|Validation)"

echo ""
echo "=== disk1.img (HDD1) ==="
python3 scripts/dump_boot_sector.py disk1.img | grep -E "(Bytes per|Sectors per|Root cluster|Boot Sector Signature|Validation)"

echo ""
echo "=== floppy.img (FD0) ==="
python3 scripts/dump_boot_sector.py floppy.img | grep -E "(Bytes per|Sectors per|Root|Boot Sector Signature|Validation)"

echo ""
echo "All images should show:"
echo "  - Bytes per sector: 512"
echo "  - Valid boot signature: 0xAA55"
echo "  - Root cluster: 2 (FAT32) or valid root entries (FAT12)"
echo ""
echo "Now boot QEMU and try: MOUNT HDD0"
echo "Compare the debug output with the values above"
