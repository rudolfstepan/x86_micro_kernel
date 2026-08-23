#!/usr/bin/env python3
"""Prepare a signed kernel in inactive BIOS slot B and publish pending state."""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import struct
from collections.abc import Callable
from pathlib import Path

try:
    from scripts.create_native_boot_image import (
        BOOT_CONTROL_ATTEMPT_LIMIT,
        BACKUP_MANIFEST_RELATIVE_LBA,
        BOOT_CONTROL_PRIMARY_RELATIVE_LBA,
        BOOT_CONTROL_SECONDARY_RELATIVE_LBA,
        BOOT_CONTROL_SLOT_B,
        KERNEL_B_RELATIVE_LBA,
        SECTOR_SIZE,
        create_boot_control_record,
        create_manifest,
        sectors_for,
        validate_elf32,
    )
    from scripts.validate_boot_manifest import (
        parse_boot_control_record,
        read_boot_control,
        validate_image,
    )
    from scripts.verify_boot_signature import verify_signature
except ModuleNotFoundError:
    from create_native_boot_image import (
        BOOT_CONTROL_ATTEMPT_LIMIT,
        BACKUP_MANIFEST_RELATIVE_LBA,
        BOOT_CONTROL_PRIMARY_RELATIVE_LBA,
        BOOT_CONTROL_SECONDARY_RELATIVE_LBA,
        BOOT_CONTROL_SLOT_B,
        KERNEL_B_RELATIVE_LBA,
        SECTOR_SIZE,
        create_boot_control_record,
        create_manifest,
        sectors_for,
        validate_elf32,
    )
    from validate_boot_manifest import (
        parse_boot_control_record,
        read_boot_control,
        validate_image,
    )
    from verify_boot_signature import verify_signature


BoundaryHook = Callable[[str, Path], None]
SignatureVerifier = Callable[[Path, Path, Path, Path, Path], str]


def _durable_write(stream, offset: int, data: bytes) -> None:
    stream.seek(offset)
    if stream.write(data) != len(data):
        raise OSError("short boot update write")
    stream.flush()
    os.fsync(stream.fileno())
    stream.seek(offset)
    if stream.read(len(data)) != data:
        raise OSError("boot update read-back mismatch")


def _control_write_order(stream, partition_lba: int) -> tuple[int, int]:
    records = []
    invalid = []
    for lba in (
            BOOT_CONTROL_PRIMARY_RELATIVE_LBA,
            BOOT_CONTROL_SECONDARY_RELATIVE_LBA):
        stream.seek((partition_lba + lba) * SECTOR_SIZE)
        sector = stream.read(SECTOR_SIZE)
        try:
            records.append(parse_boot_control_record(sector, lba))
        except ValueError:
            invalid.append(lba)
    if invalid:
        if len(invalid) != 1 or len(records) != 1:
            raise ValueError("no recoverable boot-control write order")
        return invalid[0], records[0].source_lba
    first, second = records
    if first.sequence == second.sequence:
        return BOOT_CONTROL_SECONDARY_RELATIVE_LBA, \
            BOOT_CONTROL_PRIMARY_RELATIVE_LBA
    older, newer = sorted(records, key=lambda item: item.sequence)
    return older.source_lba, newer.source_lba


def update_inactive_slot(
        source_image: Path,
        kernel_path: Path,
        signature_path: Path,
        output_image: Path,
        policy_path: Path,
        openssl: Path,
        root: Path,
        verifier: SignatureVerifier = verify_signature,
        boundary_hook: BoundaryHook | None = None) -> None:
    source_resolved = source_image.resolve()
    output_resolved = output_image.resolve()
    if source_resolved == output_resolved:
        raise ValueError("source and output images must differ")
    source = validate_image(source_image, "hdd")
    if source.pending_slot != 0xFF or source.attempts_remaining != 0:
        raise ValueError("source image already has a pending boot update")
    if source.active_slot != 0 or source.boot_control_sequence is None:
        raise ValueError("boot-control v1 requires confirmed slot A")
    if source.boot_control_sequence == 0xFFFFFFFFFFFFFFFF:
        raise ValueError("boot-control sequence is exhausted")

    kernel = kernel_path.read_bytes()
    signature = signature_path.read_bytes()
    validate_elf32(kernel)
    verified_digest = verifier(
        kernel_path, signature_path, policy_path, openssl, root
    )
    if verified_digest != hashlib.sha256(kernel).hexdigest():
        raise ValueError("signature verifier returned a different artifact digest")
    if len(signature) != 256:
        raise ValueError("RSA-PSS signature is not exactly 256 bytes")
    if KERNEL_B_RELATIVE_LBA + sectors_for(len(kernel)) > source.partition_sectors:
        raise ValueError("signed kernel exceeds inactive slot B")

    output_image.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source_image, output_image)
    with output_image.open("r+b") as image:
        partition_offset = source.partition_lba * SECTOR_SIZE
        image.seek(partition_offset)
        manifest_a = image.read(SECTOR_SIZE)
        stage2_sectors = struct.unpack_from("<I", manifest_a, 20)[0]
        manifest_b = create_manifest(
            stage2_sectors, kernel, source.partition_sectors, signature,
            KERNEL_B_RELATIVE_LBA,
        )

        _durable_write(
            image,
            partition_offset + KERNEL_B_RELATIVE_LBA * SECTOR_SIZE,
            kernel,
        )
        if boundary_hook is not None:
            boundary_hook("kernel-b", output_image)
        _durable_write(
            image,
            partition_offset + BACKUP_MANIFEST_RELATIVE_LBA * SECTOR_SIZE,
            manifest_b,
        )
        if boundary_hook is not None:
            boundary_hook("manifest-b", output_image)

    staged = validate_image(output_image, "hdd")
    if staged.pending_slot != 0xFF:
        raise ValueError("inactive-slot validation changed boot-control state")

    with output_image.open("r+b") as image:
        selected = read_boot_control(image, source.partition_lba)
        first_lba, second_lba = _control_write_order(
            image, source.partition_lba
        )
        record = create_boot_control_record(
            sequence=selected.sequence + 1,
            pending_slot=BOOT_CONTROL_SLOT_B,
            attempts_remaining=BOOT_CONTROL_ATTEMPT_LIMIT,
            successful_mask=selected.successful_mask & 0x01,
        )
        for name, lba in (
                ("control-first", first_lba),
                ("control-second", second_lba)):
            _durable_write(
                image,
                (source.partition_lba + lba) * SECTOR_SIZE,
                record,
            )
            if boundary_hook is not None:
                boundary_hook(name, output_image)

    final = validate_image(output_image, "hdd")
    if final.pending_slot != BOOT_CONTROL_SLOT_B or \
            final.attempts_remaining != BOOT_CONTROL_ATTEMPT_LIMIT or \
            final.boot_control_sequence != source.boot_control_sequence + 1:
        raise ValueError("pending slot B was not published atomically")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--kernel", type=Path, required=True)
    parser.add_argument("--signature", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--policy", type=Path, required=True)
    parser.add_argument("--openssl", type=Path, default=Path("openssl"))
    parser.add_argument("--root", type=Path, default=Path.cwd())
    args = parser.parse_args()
    try:
        update_inactive_slot(
            args.image, args.kernel, args.signature, args.output,
            args.policy, args.openssl, args.root,
        )
        info = validate_image(args.output, "hdd")
    except (OSError, ValueError) as error:
        print(f"BOOT UPDATE FAIL: {error}")
        return 1
    print(
        "BOOT UPDATE PASS: pending=B "
        f"attempts={info.attempts_remaining} "
        f"sequence={info.boot_control_sequence} output={args.output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
