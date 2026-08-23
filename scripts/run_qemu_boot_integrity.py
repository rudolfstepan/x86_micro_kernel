#!/usr/bin/env python3
"""Prove that BIOS stage 2 rejects digest and RSA-PSS signature mutations."""

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
SIGNATURE_FAILURE_MARKER = "Kernel RSA-PSS verification failed"
BOOT_MARKER = "BOOT_OK"
MANIFEST_VERSION = 3
MANIFEST_HEADER_SIZE = 336
MANIFEST_SIGNATURE_OFFSET = 80
MANIFEST_SIGNATURE_SIZE = 256


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
        if len(manifest) != SECTOR_SIZE or manifest[:8] != b"X86BOOT2" or \
                struct.unpack_from("<II", manifest, 8) != (
                    MANIFEST_VERSION, MANIFEST_HEADER_SIZE
                ):
            raise ValueError("image has no signed manifest v3")
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


def create_signature_mismatch(source: Path, output: Path) -> int:
    """Copy source and mutate only the checksummed embedded signature."""
    output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, output)
    with output.open("r+b") as image:
        partition_lba, _ = _active_boot_partition(image.read(SECTOR_SIZE))
        manifest_offset = partition_lba * SECTOR_SIZE
        image.seek(manifest_offset)
        manifest = bytearray(image.read(SECTOR_SIZE))
        if len(manifest) != SECTOR_SIZE or manifest[:8] != b"X86BOOT2" or \
                struct.unpack_from("<II", manifest, 8) != (
                    MANIFEST_VERSION, MANIFEST_HEADER_SIZE
                ):
            raise ValueError("image has no signed manifest v3")
        original_digest = bytes(manifest[48:80])
        mutation_offset = MANIFEST_SIGNATURE_OFFSET + \
            MANIFEST_SIGNATURE_SIZE // 2
        manifest[mutation_offset] ^= 0x01
        _refresh_manifest_checksum(manifest)
        if bytes(manifest[48:80]) != original_digest:
            raise AssertionError("signature fixture changed the kernel digest")
        image.seek(manifest_offset)
        image.write(manifest)
    return mutation_offset


def _run_rejection(qemu: Path, image: Path, timeout: float,
                   expected_marker: str) -> str:
    command = qemu_command(qemu, image, memory="128M")
    try:
        completed = subprocess.run(
            command,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=timeout,
            check=False,
        )
        transcript = completed.stdout
    except subprocess.TimeoutExpired as error:
        transcript = _text(error.stdout)
    if expected_marker not in transcript:
        raise ValueError(f"stage-2 rejection marker is missing: {expected_marker}")
    if BOOT_MARKER in transcript:
        raise ValueError("tampered image reached BOOT_OK")
    return transcript


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
        transcript = _run_rejection(
            args.qemu, args.output, args.timeout, SHA_FAILURE_MARKER
        )
        args.log.parent.mkdir(parents=True, exist_ok=True)
        args.log.write_text(transcript, encoding="utf-8")
        if "Kernel CRC32 verification failed" in transcript:
            raise ValueError("negative image did not preserve CRC32 validity")

        signature_output = args.output.with_name(
            f"{args.output.stem}-signature{args.output.suffix}"
        )
        signature_log = args.log.with_name(
            f"{args.log.stem}-signature{args.log.suffix}"
        )
        signature_offset = create_signature_mismatch(
            args.image, signature_output
        )
        signature_transcript = _run_rejection(
            args.qemu, signature_output, args.timeout, SIGNATURE_FAILURE_MARKER
        )
        signature_log.write_text(signature_transcript, encoding="utf-8")
        if SHA_FAILURE_MARKER in signature_transcript:
            raise ValueError("signature fixture changed kernel SHA-256 validity")
    except (OSError, ValueError) as error:
        print(f"boot-integrity: FAIL: {error}")
        return 1
    print(
        f"boot-integrity: PASS sha_offset={offset} "
        f"signature_offset={signature_offset} log={args.log}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
