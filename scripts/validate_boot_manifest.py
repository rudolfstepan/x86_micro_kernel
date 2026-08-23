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
BACKUP_MANIFEST_RELATIVE_LBA = 96
BOOT_CONTROL_PRIMARY_RELATIVE_LBA = 97
BOOT_CONTROL_SECONDARY_RELATIVE_LBA = 98
KERNEL_B_RELATIVE_LBA = 3136
BOOT_CONTROL_MAGIC = b"REISTBC1"
BOOT_CONTROL_VERSION = 1
BOOT_CONTROL_HEADER_SIZE = 64
BOOT_CONTROL_CRC_OFFSET = 60
BOOT_CONTROL_SLOT_A = 0
BOOT_CONTROL_SLOT_B = 1
BOOT_CONTROL_SLOT_NONE = 0xFF
BOOT_CONTROL_ATTEMPT_LIMIT = 2
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
    slot_count: int
    boot_control_sequence: int | None
    active_slot: int | None
    pending_slot: int | None
    attempts_remaining: int | None


@dataclass(frozen=True)
class BootControlInfo:
    sequence: int
    active_slot: int
    pending_slot: int
    attempts_remaining: int
    attempt_limit: int
    successful_mask: int
    source_lba: int
    raw: bytes


def _additive_checksum_valid(sector: bytes) -> bool:
    return (sum(struct.unpack("<128I", sector)) & 0xFFFFFFFF) == 0


def parse_boot_control_record(sector: bytes, source_lba: int) -> BootControlInfo:
    if len(sector) != SECTOR_SIZE:
        raise ValueError("boot-control sector is truncated")
    (magic, version, header_size, sequence, active_slot, pending_slot,
     attempts_remaining, attempt_limit, successful_mask) = struct.unpack_from(
        "<8sIIQBBBBI", sector
    )
    if magic != BOOT_CONTROL_MAGIC:
        raise ValueError("boot-control magic is invalid")
    if version != BOOT_CONTROL_VERSION or header_size != BOOT_CONTROL_HEADER_SIZE:
        raise ValueError("boot-control version or header size is unsupported")
    if sector[32:BOOT_CONTROL_CRC_OFFSET] != bytes(
            BOOT_CONTROL_CRC_OFFSET - 32) or sector[64:] != bytes(448):
        raise ValueError("boot-control reserved bytes are nonzero")
    stored_crc = struct.unpack_from("<I", sector, BOOT_CONTROL_CRC_OFFSET)[0]
    checked = bytearray(sector)
    struct.pack_into("<I", checked, BOOT_CONTROL_CRC_OFFSET, 0)
    if (binascii.crc32(checked) & 0xFFFFFFFF) != stored_crc:
        raise ValueError("boot-control CRC32 is invalid")
    if not 1 <= sequence <= 0xFFFFFFFFFFFFFFFF:
        raise ValueError("boot-control sequence is invalid")
    if active_slot != BOOT_CONTROL_SLOT_A:
        raise ValueError("boot-control v1 active slot is unsupported")
    if pending_slot not in (BOOT_CONTROL_SLOT_NONE, BOOT_CONTROL_SLOT_B):
        raise ValueError("boot-control pending slot is unsupported")
    if attempt_limit != BOOT_CONTROL_ATTEMPT_LIMIT:
        raise ValueError("boot-control attempt limit is unsupported")
    if pending_slot == BOOT_CONTROL_SLOT_NONE and attempts_remaining != 0:
        raise ValueError("confirmed boot-control state retains attempts")
    if pending_slot == BOOT_CONTROL_SLOT_B and attempts_remaining > attempt_limit:
        raise ValueError("boot-control attempts exceed the fixed limit")
    if successful_mask & ~0x03 or not successful_mask & 0x01:
        raise ValueError("boot-control successful-slot mask is invalid")
    return BootControlInfo(
        sequence, active_slot, pending_slot, attempts_remaining,
        attempt_limit, successful_mask, source_lba, sector,
    )


def read_boot_control(image, partition_lba: int) -> BootControlInfo:
    valid: list[BootControlInfo] = []
    errors: list[str] = []
    for relative_lba in (
            BOOT_CONTROL_PRIMARY_RELATIVE_LBA,
            BOOT_CONTROL_SECONDARY_RELATIVE_LBA):
        image.seek((partition_lba + relative_lba) * SECTOR_SIZE)
        sector = image.read(SECTOR_SIZE)
        try:
            valid.append(parse_boot_control_record(sector, relative_lba))
        except ValueError as error:
            errors.append(f"LBA {relative_lba}: {error}")
    if not valid:
        raise ValueError("both boot-control records are invalid: " + "; ".join(errors))
    if len(valid) == 1:
        return valid[0]
    first, second = valid
    if first.sequence == second.sequence:
        if first.raw != second.raw:
            raise ValueError("equal-sequence boot-control records disagree")
        return first
    older, newer = sorted(valid, key=lambda item: item.sequence)
    if newer.sequence - older.sequence != 1:
        raise ValueError("boot-control sequence gap is ambiguous")
    return newer


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


def _validate_slot(image, image_size: int, partition_lba: int,
                   expected_partition_sectors: int, manifest_lba: int,
                   expected_kernel_lba: int, next_extent_lba: int,
                   stage2_limit_lba: int) -> tuple[int, bytes, bytes]:
    image.seek((partition_lba + manifest_lba) * SECTOR_SIZE)
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
    if not 1 <= stage2_sectors <= STAGE2_MAX_SECTORS or \
            stage2_lba + stage2_sectors > stage2_limit_lba:
        raise ValueError("manifest stage-2 extent is invalid")
    if kernel_lba != expected_kernel_lba:
        raise ValueError("manifest kernel offset is unsupported")
    if kernel_size < MIN_KERNEL_SIZE:
        raise ValueError("manifest kernel is smaller than 4 KiB")
    if kernel_size > MAX_KERNEL_SIZE:
        raise ValueError("manifest kernel exceeds the 64 MiB validator maximum")
    kernel_sectors = (kernel_size + SECTOR_SIZE - 1) // SECTOR_SIZE
    if kernel_lba + kernel_sectors > next_extent_lba or \
            kernel_lba + kernel_sectors > partition_sectors:
        raise ValueError("manifest kernel extent exceeds its fixed slot")

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
    if kernel_offset + kernel_size > image_size:
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
    return kernel_size, actual_digest, signature


def validate_image(path: Path, layout: str = "auto") -> ManifestInfo:
    size = path.stat().st_size
    if size == 0 or size % SECTOR_SIZE:
        raise ValueError("image size is not a non-zero multiple of 512 bytes")
    image_sectors = size // SECTOR_SIZE

    with path.open("rb") as image:
        actual_layout, partition_lba, expected_partition_sectors = _locate_manifest(
            image, image_sectors, layout
        )
        if actual_layout == "hdd":
            slots = (
                (0, KERNEL_RELATIVE_LBA, KERNEL_B_RELATIVE_LBA),
                (BACKUP_MANIFEST_RELATIVE_LBA, KERNEL_B_RELATIVE_LBA,
                 expected_partition_sectors),
            )
            stage2_limit = BACKUP_MANIFEST_RELATIVE_LBA
            boot_control = read_boot_control(image, partition_lba)
        else:
            slots = ((0, KERNEL_RELATIVE_LBA, expected_partition_sectors),)
            stage2_limit = KERNEL_RELATIVE_LBA
            boot_control = None
        results = [
            _validate_slot(
                image, size, partition_lba, expected_partition_sectors,
                manifest_lba, kernel_lba, next_extent_lba, stage2_limit,
            )
            for manifest_lba, kernel_lba, next_extent_lba in slots
        ]
        kernel_size, actual_digest, signature = results[0]

    return ManifestInfo(
        actual_layout, partition_lba, expected_partition_sectors, kernel_size,
        actual_digest.hex(), hashlib.sha256(signature).hexdigest(), len(results),
        None if boot_control is None else boot_control.sequence,
        None if boot_control is None else boot_control.active_slot,
        None if boot_control is None else boot_control.pending_slot,
        None if boot_control is None else boot_control.attempts_remaining,
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
        f"slots={info.slot_count} sha256={info.kernel_sha256} "
        f"signature_sha256={info.signature_sha256} "
        f"control_sequence={info.boot_control_sequence} "
        f"active={info.active_slot} pending={info.pending_slot} "
        f"attempts={info.attempts_remaining}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
