#!/usr/bin/env python3
"""Independently validate a REIST BIOS boot image manifest and kernel."""

from __future__ import annotations

import argparse
import binascii
import hashlib
import struct
from dataclasses import dataclass
from pathlib import Path


SECTOR_SIZE = 512
MANIFEST_MAGIC = b"X86BOOT2"
MANIFEST_VERSION = 3
MANIFEST_HEADER_SIZE = 336
MANIFEST_FLAG_SIGNED = 0x00000001
MANIFEST_SIGNATURE_OFFSET = 80
MANIFEST_SIGNATURE_SIZE = 256
MANIFEST_LBA_FLOPPY = 1
BOOT_PARTITION_TYPE = 0xDA
STAGE2_RELATIVE_LBA = 1
STAGE2_MAX_SECTORS = 64
KERNEL_RELATIVE_LBA = 128
MIN_KERNEL_SIZE = 4096
MAX_KERNEL_SIZE = 64 * 1024 * 1024
HASH_CHUNK_SIZE = 64 * 1024


@dataclass(frozen=True)
class ManifestInfo:
    layout: str
    partition_lba: int
    partition_sectors: int
    kernel_size: int
    kernel_sha256: str
    signature_sha256: str


def _additive_checksum_valid(sector: bytes) -> bool:
    return (sum(struct.unpack("<128I", sector)) & 0xFFFFFFFF) == 0


def _locate_manifest(image, image_sectors: int, layout: str) -> tuple[str, int, int]:
    image.seek(0)
    sector0 = image.read(SECTOR_SIZE)
    if len(sector0) != SECTOR_SIZE:
        raise ValueError("image is smaller than one sector")

    active = []
    if sector0[510:512] == b"\x55\xaa":
        for index in range(4):
            entry = sector0[446 + index * 16:462 + index * 16]
            if entry[0] == 0x80:
                active.append((entry[4], *struct.unpack_from("<II", entry, 8)))

    if layout == "auto":
        layout = "hdd" if active else "floppy"
    if layout == "hdd":
        boot = [entry for entry in active if entry[0] == BOOT_PARTITION_TYPE]
        if len(active) != 1 or len(boot) != 1:
            raise ValueError("HDD must have exactly one active 0xDA boot partition")
        _, partition_lba, partition_sectors = boot[0]
        if partition_lba == 0 or partition_sectors == 0:
            raise ValueError("boot partition has an empty extent")
        if partition_lba + partition_sectors > image_sectors:
            raise ValueError("boot partition exceeds image bounds")
        return layout, partition_lba, partition_sectors
    if layout == "floppy":
        if image_sectors <= MANIFEST_LBA_FLOPPY:
            raise ValueError("floppy image has no manifest sector")
        return layout, MANIFEST_LBA_FLOPPY, image_sectors - MANIFEST_LBA_FLOPPY
    raise ValueError(f"unsupported layout: {layout}")


def validate_image(path: Path, layout: str = "auto") -> ManifestInfo:
    size = path.stat().st_size
    if size == 0 or size % SECTOR_SIZE:
        raise ValueError("image size is not a non-zero multiple of 512 bytes")
    image_sectors = size // SECTOR_SIZE

    with path.open("rb") as image:
        actual_layout, partition_lba, expected_partition_sectors = _locate_manifest(
            image, image_sectors, layout
        )
        image.seek(partition_lba * SECTOR_SIZE)
        manifest = image.read(SECTOR_SIZE)
        if len(manifest) != SECTOR_SIZE:
            raise ValueError("manifest sector is truncated")

        fields = struct.unpack_from("<8sIIIIIIIIII", manifest)
        (magic, version, header_size, stage2_lba, stage2_sectors,
         kernel_lba, kernel_size, partition_sectors, kernel_crc,
         flags, _checksum) = fields
        if magic != MANIFEST_MAGIC:
            raise ValueError("manifest magic is not X86BOOT2")
        if version != MANIFEST_VERSION or header_size != MANIFEST_HEADER_SIZE:
            raise ValueError("manifest version or header size is unsupported")
        if flags != MANIFEST_FLAG_SIGNED:
            raise ValueError("manifest does not require the signed-kernel profile")
        if not _additive_checksum_valid(manifest):
            raise ValueError("manifest additive checksum is invalid")
        if partition_sectors != expected_partition_sectors:
            raise ValueError("manifest partition extent disagrees with image layout")
        if stage2_lba != STAGE2_RELATIVE_LBA:
            raise ValueError("manifest stage-2 offset is unsupported")
        if not 1 <= stage2_sectors <= STAGE2_MAX_SECTORS:
            raise ValueError("manifest stage-2 extent is invalid")
        if kernel_lba != KERNEL_RELATIVE_LBA:
            raise ValueError("manifest kernel offset is unsupported")
        if stage2_lba + stage2_sectors > kernel_lba:
            raise ValueError("stage-2 extent overlaps the kernel")
        if kernel_size < MIN_KERNEL_SIZE:
            raise ValueError("manifest kernel is smaller than 4 KiB")
        if kernel_size > MAX_KERNEL_SIZE:
            raise ValueError("manifest kernel exceeds the 64 MiB validator maximum")
        kernel_sectors = (kernel_size + SECTOR_SIZE - 1) // SECTOR_SIZE
        if kernel_lba + kernel_sectors > partition_sectors:
            raise ValueError("manifest kernel extent exceeds boot partition")

        expected_digest = manifest[48:80]
        if expected_digest == bytes(32):
            raise ValueError("manifest SHA-256 digest is missing")
        signature = manifest[
            MANIFEST_SIGNATURE_OFFSET:
            MANIFEST_SIGNATURE_OFFSET + MANIFEST_SIGNATURE_SIZE
        ]
        if len(signature) != MANIFEST_SIGNATURE_SIZE or \
                signature == bytes(MANIFEST_SIGNATURE_SIZE):
            raise ValueError("manifest RSA-PSS signature is missing")
        kernel_offset = (partition_lba + kernel_lba) * SECTOR_SIZE
        if kernel_offset + kernel_size > size:
            raise ValueError("kernel payload is truncated")
        image.seek(kernel_offset)
        digest = hashlib.sha256()
        actual_crc = 0
        remaining = kernel_size
        while remaining:
            chunk = image.read(min(remaining, HASH_CHUNK_SIZE))
            if not chunk:
                raise ValueError("kernel payload is truncated")
            digest.update(chunk)
            actual_crc = binascii.crc32(chunk, actual_crc)
            remaining -= len(chunk)
        actual_digest = digest.digest()
        if actual_digest != expected_digest:
            raise ValueError("kernel SHA-256 digest mismatch")
        if (actual_crc & 0xFFFFFFFF) != kernel_crc:
            raise ValueError("kernel CRC32 diagnostic mismatch")

    return ManifestInfo(
        actual_layout, partition_lba, partition_sectors, kernel_size,
        actual_digest.hex(), hashlib.sha256(signature).hexdigest()
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--layout", choices=("auto", "hdd", "floppy"), default="auto")
    args = parser.parse_args()
    try:
        info = validate_image(args.image, args.layout)
    except (OSError, ValueError) as error:
        print(f"BOOT MANIFEST FAIL: {error}")
        return 1
    print(
        f"BOOT MANIFEST PASS: layout={info.layout} kernel={info.kernel_size} "
        f"sha256={info.kernel_sha256} signature_sha256={info.signature_sha256}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
