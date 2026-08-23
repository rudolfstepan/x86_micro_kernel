#!/usr/bin/env python3
"""Prove that BIOS stage 2 rejects a CRC-valid SHA-256 kernel mismatch."""

from __future__ import annotations

import argparse
import binascii
import hashlib
import shutil
import struct
import subprocess
from pathlib import Path

try:
    from scripts.run_qemu_smoke import qemu_command
except ModuleNotFoundError:
    from run_qemu_smoke import qemu_command


SECTOR_SIZE = 512
BOOT_PARTITION_TYPE = 0xDA
SHA_FAILURE_MARKER = "Kernel SHA-256 verification failed"
BOOT_MARKER = "BOOT_OK"


def _active_boot_partition(mbr: bytes) -> tuple[int, int]:
    if len(mbr) != SECTOR_SIZE or mbr[510:512] != b"\x55\xaa":
        raise ValueError("image has no valid MBR")
    matches = []
    for index in range(4):
        entry = mbr[446 + index * 16:462 + index * 16]
        if entry[0] == 0x80 and entry[4] == BOOT_PARTITION_TYPE:
            matches.append(struct.unpack_from("<II", entry, 8))
    if len(matches) != 1:
        raise ValueError("image must have exactly one active 0xDA partition")
    return matches[0]


def _refresh_manifest_checksum(manifest: bytearray) -> None:
    struct.pack_into("<I", manifest, 44, 0)
    checksum = (-sum(struct.unpack("<128I", manifest))) & 0xFFFFFFFF
    struct.pack_into("<I", manifest, 44, checksum)


def create_crc_valid_sha_mismatch(source: Path, output: Path) -> int:
    """Copy source and mutate one kernel byte while preserving manifest SHA."""
    output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, output)
    with output.open("r+b") as image:
        partition_lba, partition_sectors = _active_boot_partition(
            image.read(SECTOR_SIZE)
        )
        manifest_offset = partition_lba * SECTOR_SIZE
        image.seek(manifest_offset)
        manifest = bytearray(image.read(SECTOR_SIZE))
        if len(manifest) != SECTOR_SIZE or manifest[:8] != b"X86BOOT2":
            raise ValueError("image has no manifest v2")
        kernel_lba, kernel_size = struct.unpack_from("<II", manifest, 24)
        kernel_sectors = (kernel_size + SECTOR_SIZE - 1) // SECTOR_SIZE
        if (kernel_size < 4096 or
                kernel_lba + kernel_sectors > partition_sectors):
            raise ValueError("manifest kernel extent is invalid")
        kernel_offset = (partition_lba + kernel_lba) * SECTOR_SIZE
        image.seek(kernel_offset)
        kernel = bytearray(image.read(kernel_size))
        if len(kernel) != kernel_size:
            raise ValueError("kernel payload is truncated")
        original_digest = bytes(manifest[48:80])
        mutation_offset = kernel_size // 2
        kernel[mutation_offset] ^= 0x01
        if hashlib.sha256(kernel).digest() == original_digest:
            raise AssertionError("kernel mutation did not change SHA-256")
        image.seek(kernel_offset)
        image.write(kernel)
        struct.pack_into(
            "<I", manifest, 36, binascii.crc32(kernel) & 0xFFFFFFFF
        )
        _refresh_manifest_checksum(manifest)
        if bytes(manifest[48:80]) != original_digest:
            raise AssertionError("negative fixture changed manifest SHA-256")
        image.seek(manifest_offset)
        image.write(manifest)
    return mutation_offset


def _text(value: str | bytes | None) -> str:
    if value is None:
        return ""
    if isinstance(value, bytes):
        return value.decode("utf-8", errors="replace")
    return value


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=12.0)
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")

    try:
        offset = create_crc_valid_sha_mismatch(args.image, args.output)
        command = qemu_command(args.qemu, args.output, memory="128M")
        try:
            completed = subprocess.run(
                command,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                timeout=args.timeout,
                check=False,
            )
            transcript = completed.stdout
        except subprocess.TimeoutExpired as error:
            transcript = _text(error.stdout)
        args.log.parent.mkdir(parents=True, exist_ok=True)
        args.log.write_text(transcript, encoding="utf-8")
        if SHA_FAILURE_MARKER not in transcript:
            raise ValueError("stage-2 SHA-256 rejection marker is missing")
        if BOOT_MARKER in transcript:
            raise ValueError("tampered kernel reached BOOT_OK")
        if "Kernel CRC32 verification failed" in transcript:
            raise ValueError("negative image did not preserve CRC32 validity")
    except (OSError, ValueError) as error:
        print(f"boot-integrity: FAIL: {error}")
        return 1
    print(f"boot-integrity: PASS mutation_offset={offset} log={args.log}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
