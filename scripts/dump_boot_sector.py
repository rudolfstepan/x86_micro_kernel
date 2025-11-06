#!/usr/bin/env python3
"""
Boot sector hex dump tool
Reads and displays the first sector of a disk image
"""

import sys
import struct
from pathlib import Path

def hex_dump(data, offset=0, length=None):
    """Pretty print hex dump"""
    if length:
        data = data[:length]
    
    for i in range(0, len(data), 16):
        # Offset
        print(f"{offset + i:08X}  ", end="")
        
        # Hex bytes
        hex_part = ""
        for j in range(16):
            if i + j < len(data):
                hex_part += f"{data[i+j]:02X} "
            else:
                hex_part += "   "
            if j == 7:
                hex_part += " "
        print(hex_part, end=" ")
        
        # ASCII representation
        ascii_part = ""
        for j in range(16):
            if i + j < len(data):
                byte = data[i + j]
                ascii_part += chr(byte) if 32 <= byte < 127 else "."
            else:
                ascii_part += " "
        print(f"|{ascii_part}|")

def parse_fat32_boot_sector(data):
    """Parse FAT32 boot sector fields"""
    print("\n=== FAT32 Boot Sector Parsed ===\n")
    
    # Jump instruction (0-2)
    print(f"Jump Boot:           {data[0]:02X} {data[1]:02X} {data[2]:02X}")
    
    # OEM Name (3-10)
    oem_name = data[3:11].decode('ascii', errors='ignore')
    print(f"OEM Name:            '{oem_name}'")
    
    # BPB (BIOS Parameter Block)
    bytes_per_sector = struct.unpack('<H', data[11:13])[0]
    sectors_per_cluster = data[13]
    reserved_sectors = struct.unpack('<H', data[14:16])[0]
    num_fats = data[16]
    root_entries = struct.unpack('<H', data[17:19])[0]
    total_sectors_16 = struct.unpack('<H', data[19:21])[0]
    media_type = data[21]
    fat_size_16 = struct.unpack('<H', data[22:24])[0]
    sectors_per_track = struct.unpack('<H', data[24:26])[0]
    num_heads = struct.unpack('<H', data[26:28])[0]
    hidden_sectors = struct.unpack('<I', data[28:32])[0]
    total_sectors_32 = struct.unpack('<I', data[32:36])[0]
    
    print(f"\nBPB (BIOS Parameter Block):")
    print(f"  Bytes per sector:    {bytes_per_sector}")
    print(f"  Sectors per cluster: {sectors_per_cluster}")
    print(f"  Reserved sectors:    {reserved_sectors}")
    print(f"  Number of FATs:      {num_fats}")
    print(f"  Root entries:        {root_entries} (0 for FAT32)")
    print(f"  Total sectors (16):  {total_sectors_16} (0 for FAT32)")
    print(f"  Media type:          0x{media_type:02X}")
    print(f"  FAT size (16):       {fat_size_16} (0 for FAT32)")
    print(f"  Sectors per track:   {sectors_per_track}")
    print(f"  Number of heads:     {num_heads}")
    print(f"  Hidden sectors:      {hidden_sectors}")
    print(f"  Total sectors (32):  {total_sectors_32}")
    
    # FAT32 Extended BPB
    fat_size_32 = struct.unpack('<I', data[36:40])[0]
    flags = struct.unpack('<H', data[40:42])[0]
    version = struct.unpack('<H', data[42:44])[0]
    root_cluster = struct.unpack('<I', data[44:48])[0]
    fs_info = struct.unpack('<H', data[48:50])[0]
    backup_boot = struct.unpack('<H', data[50:52])[0]
    
    print(f"\nFAT32 Extended BPB:")
    print(f"  FAT size (32):       {fat_size_32} sectors")
    print(f"  Flags:               0x{flags:04X}")
    print(f"  Version:             {version}")
    print(f"  Root cluster:        {root_cluster}")
    print(f"  FSInfo sector:       {fs_info}")
    print(f"  Backup boot sector:  {backup_boot}")
    
    # Drive info (offset 64-89)
    drive_number = data[64]
    boot_sig = data[66]
    volume_id = struct.unpack('<I', data[67:71])[0]
    volume_label = data[71:82].decode('ascii', errors='ignore').strip()
    fs_type = data[82:90].decode('ascii', errors='ignore').strip()
    
    print(f"\nDrive Information:")
    print(f"  Drive number:        0x{drive_number:02X}")
    print(f"  Boot signature:      0x{boot_sig:02X}")
    print(f"  Volume ID:           0x{volume_id:08X}")
    print(f"  Volume label:        '{volume_label}'")
    print(f"  FS type:             '{fs_type}'")
    
    # Boot signature at end of sector (510-511)
    boot_signature = struct.unpack('<H', data[510:512])[0]
    print(f"\nBoot Sector Signature: 0x{boot_signature:04X}", end="")
    if boot_signature == 0xAA55:
        print(" ✓ Valid")
    else:
        print(" ✗ Invalid (should be 0xAA55)")
    
    # Validation
    print(f"\n=== Validation ===\n")
    
    valid = True
    if bytes_per_sector not in [512, 1024, 2048, 4096]:
        print(f"✗ Invalid bytes per sector: {bytes_per_sector}")
        valid = False
    else:
        print(f"✓ Valid bytes per sector: {bytes_per_sector}")
    
    if sectors_per_cluster == 0 or (sectors_per_cluster & (sectors_per_cluster - 1)) != 0:
        print(f"✗ Invalid sectors per cluster: {sectors_per_cluster} (must be power of 2)")
        valid = False
    else:
        print(f"✓ Valid sectors per cluster: {sectors_per_cluster}")
    
    if root_cluster < 2:
        print(f"✗ Invalid root cluster: {root_cluster} (must be >= 2)")
        valid = False
    else:
        print(f"✓ Valid root cluster: {root_cluster}")
    
    if boot_signature != 0xAA55:
        print(f"✗ Invalid boot signature: 0x{boot_signature:04X}")
        valid = False
    else:
        print(f"✓ Valid boot signature: 0x{boot_signature:04X}")
    
    if 'FAT32' in fs_type:
        print(f"✓ FAT32 filesystem detected")
    elif 'FAT' in fs_type:
        print(f"⚠ FAT12/16 filesystem: '{fs_type}'")
    else:
        print(f"✗ Unknown filesystem type: '{fs_type}'")
        valid = False
    
    print(f"\n{'✓ Boot sector appears valid' if valid else '✗ Boot sector has errors'}")

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 dump_boot_sector.py <disk_image>")
        print("\nExample:")
        print("  python3 dump_boot_sector.py disk.img")
        sys.exit(1)
    
    image_path = Path(sys.argv[1])
    
    if not image_path.exists():
        print(f"Error: File not found: {image_path}")
        sys.exit(1)
    
    print(f"Reading boot sector from: {image_path}")
    print(f"File size: {image_path.stat().st_size} bytes\n")
    
    with open(image_path, 'rb') as f:
        boot_sector = f.read(512)
    
    if len(boot_sector) < 512:
        print(f"Error: File too small ({len(boot_sector)} bytes, need 512)")
        sys.exit(1)
    
    print("=== Boot Sector Hex Dump (First 128 bytes) ===\n")
    hex_dump(boot_sector, length=128)
    
    print("\n=== Boot Sector Hex Dump (Last 32 bytes) ===\n")
    hex_dump(boot_sector[480:], offset=480)
    
    parse_fat32_boot_sector(boot_sector)

if __name__ == '__main__':
    main()
