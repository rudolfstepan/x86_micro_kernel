#!/usr/bin/env python3
"""Create a raw 1.44-MB BIOS floppy image containing stage 2 and the kernel."""

from __future__ import annotations

import argparse
import binascii
import struct
from collections.abc import Mapping
from pathlib import Path

try:
    from scripts.create_native_boot_image import (
        KERNEL_RELATIVE_LBA,
        SECTOR_SIZE,
        STAGE2_MAX_SECTORS,
        STAGE2_RELATIVE_LBA,
        create_manifest,
        sectors_for,
        validate_elf32,
    )
except ModuleNotFoundError:
    from create_native_boot_image import (
        KERNEL_RELATIVE_LBA,
        SECTOR_SIZE,
        STAGE2_MAX_SECTORS,
        STAGE2_RELATIVE_LBA,
        create_manifest,
        sectors_for,
        validate_elf32,
    )

FLOPPY_SECTORS = 2880
FLOPPY_SIZE = FLOPPY_SECTORS * SECTOR_SIZE
MANIFEST_LBA = 1


def encode_83(name: str) -> bytes:
    parts = name.upper().split(".")
    if len(parts) > 2 or not parts[0] or len(parts[0]) > 8 or \
            (len(parts) == 2 and len(parts[1]) > 3):
        raise ValueError(f"invalid FAT12 8.3 filename: {name}")
    allowed = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_$~!#%&-{}()@'`"
    if any(ch not in allowed for part in parts for ch in part):
        raise ValueError(f"invalid FAT12 filename character: {name}")
    stem = parts[0].ljust(8)
    extension = (parts[1] if len(parts) == 2 else "").ljust(3)
    return (stem + extension).encode("ascii")


def set_fat12_entry(fat: bytearray, cluster: int, value: int) -> None:
    offset = cluster + cluster // 2
    if cluster & 1:
        fat[offset] = (fat[offset] & 0x0F) | ((value << 4) & 0xF0)
        fat[offset + 1] = (value >> 4) & 0xFF
    else:
        fat[offset] = value & 0xFF
        fat[offset + 1] = (fat[offset + 1] & 0xF0) | ((value >> 8) & 0x0F)


def create_floppy_image(stage1: bytes, stage2: bytes, kernel: bytes,
                        data_files: Mapping[str, bytes] | None = None,
                        reist_fat12: bool = False) -> bytes:
    if len(stage1) != SECTOR_SIZE or stage1[510:] != b"\x55\xaa":
        raise ValueError("floppy stage 1 must be a signed 512-byte boot sector")
    stage2_sectors = sectors_for(len(stage2))
    if not 1 <= stage2_sectors <= STAGE2_MAX_SECTORS:
        raise ValueError("stage 2 exceeds the floppy loader limit")
    validate_elf32(kernel)

    stage2_lba = MANIFEST_LBA + STAGE2_RELATIVE_LBA
    kernel_lba = MANIFEST_LBA + KERNEL_RELATIVE_LBA
    if stage2_lba + stage2_sectors > kernel_lba:
        raise ValueError("stage 2 overlaps the floppy kernel extent")
    if kernel_lba + sectors_for(len(kernel)) > FLOPPY_SECTORS:
        available = (FLOPPY_SECTORS - kernel_lba) * SECTOR_SIZE
        raise ValueError(
            f"kernel is too large for a 1.44-MB floppy: {len(kernel)} > {available} bytes"
        )

    image = bytearray(FLOPPY_SIZE)
    image[:SECTOR_SIZE] = stage1
    manifest = create_manifest(stage2_sectors, kernel,
                               FLOPPY_SECTORS - MANIFEST_LBA)
    image[MANIFEST_LBA * SECTOR_SIZE:(MANIFEST_LBA + 1) * SECTOR_SIZE] = manifest
    image[stage2_lba * SECTOR_SIZE:stage2_lba * SECTOR_SIZE + len(stage2)] = stage2
    image[kernel_lba * SECTOR_SIZE:kernel_lba * SECTOR_SIZE + len(kernel)] = kernel

    # Keep every boot extent inside the FAT12 reserved area. This produces a
    # hybrid image which the BIOS can boot and the kernel can mount as A:.
    reserved_sectors = kernel_lba + sectors_for(len(kernel))
    if reist_fat12:
        reserved_sectors = max(reserved_sectors + 195, 196)
        if reserved_sectors >= FLOPPY_SECTORS:
            raise ValueError("REIST FAT12 journal leaves no data area")
    sectors_per_fat = 3
    fat_count = 2
    root_entries = 224
    struct.pack_into("<HBH", image, 11, SECTOR_SIZE, 1, reserved_sectors)
    struct.pack_into("<BHHBHHH", image, 16, fat_count, root_entries,
                     FLOPPY_SECTORS, 0xF0, sectors_per_fat, 18, 2)
    struct.pack_into("<II", image, 28, 0, 0)
    if reist_fat12:
        layout_base = reserved_sectors - 195
        remap_base = layout_base + 130
        image[54:62] = b"REIST12 "
        struct.pack_into("<I", image, 39, 0x52454953)
        journal = bytearray(SECTOR_SIZE)
        struct.pack_into("<IHHIQIII", journal, 0,
                         0x524A3132, 2, 32, 0x52454953, 1, 0, 0, 0)
        struct.pack_into("<I", journal, 28,
                         binascii.crc32(journal) & 0xFFFFFFFF)
        image[layout_base * SECTOR_SIZE:(layout_base + 1) * SECTOR_SIZE] = journal
        image[(layout_base + 1) * SECTOR_SIZE:
              (layout_base + 2) * SECTOR_SIZE] = journal
        remap = bytearray(SECTOR_SIZE)
        struct.pack_into("<IHHIQIII", remap, 0,
                         0x52504D31, 1, 16, 0x52454953, 1, 0, 0, 0)
        struct.pack_into("<I", remap, 28,
                         binascii.crc32(remap) & 0xFFFFFFFF)
        image[remap_base * SECTOR_SIZE:(remap_base + 1) * SECTOR_SIZE] = remap
        image[(remap_base + 1) * SECTOR_SIZE:
              (remap_base + 2) * SECTOR_SIZE] = remap
    root_sectors = (root_entries * 32 + SECTOR_SIZE - 1) // SECTOR_SIZE
    if reserved_sectors + fat_count * sectors_per_fat + root_sectors >= FLOPPY_SECTORS:
        raise ValueError("kernel leaves no usable FAT12 data area on the floppy")
    fat = bytearray(sectors_per_fat * SECTOR_SIZE)
    fat[:3] = b"\xf0\xff\xff"
    root = bytearray(root_sectors * SECTOR_SIZE)
    data_start = reserved_sectors + fat_count * sectors_per_fat + root_sectors
    files = {"README.TXT": (
        b"REIST OS rescue floppy\r\n"
        b"Bootloader and kernel occupy reserved FAT12 sectors.\r\n"
    )}
    if data_files:
        files.update(data_files)
    next_cluster = 2
    for entry_index, (name, contents) in enumerate(files.items()):
        encoded = encode_83(name)
        needed = max(1, sectors_for(len(contents)))
        if entry_index >= root_entries or data_start + next_cluster - 2 + needed > FLOPPY_SECTORS:
            raise ValueError(f"file does not fit on rescue floppy: {name}")
        first_cluster = next_cluster
        for index in range(needed):
            cluster = next_cluster + index
            successor = 0xFFF if index == needed - 1 else cluster + 1
            set_fat12_entry(fat, cluster, successor)
            offset = (data_start + cluster - 2) * SECTOR_SIZE
            chunk = contents[index * SECTOR_SIZE:(index + 1) * SECTOR_SIZE]
            image[offset:offset + len(chunk)] = chunk
        struct.pack_into("<11sB10sHHHI", root, entry_index * 32,
                         encoded, 0x20, bytes(10), 0, 0, first_cluster,
                         len(contents))
        next_cluster += needed

    for fat_index in range(fat_count):
        fat_offset = (reserved_sectors + fat_index * sectors_per_fat) * SECTOR_SIZE
        image[fat_offset:fat_offset + len(fat)] = fat
    root_offset = (reserved_sectors + fat_count * sectors_per_fat) * SECTOR_SIZE
    image[root_offset:root_offset + len(root)] = root
    return bytes(image)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage1", required=True, type=Path)
    parser.add_argument("--stage2", required=True, type=Path)
    parser.add_argument("--kernel", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--data-file", action="append", default=[],
                        metavar="NAME=PATH")
    parser.add_argument("--reist-fat12", action="store_true",
                        help="mark image and initialize the fixed REIST journal")
    args = parser.parse_args()
    data_files = {}
    for specification in args.data_file:
        if "=" not in specification:
            parser.error("--data-file requires NAME=PATH")
        name, path = specification.split("=", 1)
        data_files[name] = Path(path).read_bytes()
    image = create_floppy_image(args.stage1.read_bytes(),
                                args.stage2.read_bytes(),
                                args.kernel.read_bytes(), data_files,
                                args.reist_fat12)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(image)
    print(f"Bootable floppy image: {args.output.resolve()} ({len(image)} bytes)")


if __name__ == "__main__":
    main()
