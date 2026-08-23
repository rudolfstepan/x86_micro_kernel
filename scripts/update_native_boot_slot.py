#!/usr/bin/env python3
"""Prepare a signed kernel in the inactive BIOS slot and publish pending state."""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import struct
import tempfile
from collections.abc import Callable
from pathlib import Path

try:
    from scripts.create_native_boot_image import (
        BOOT_CONTROL_ATTEMPT_LIMIT,
        BACKUP_MANIFEST_RELATIVE_LBA,
        BOOT_CONTROL_PRIMARY_RELATIVE_LBA,
        BOOT_CONTROL_SECONDARY_RELATIVE_LBA,
        BOOT_CONTROL_SLOT_A,
        BOOT_CONTROL_SLOT_B,
        BOOT_CONTROL_VERSION_V2,
        KERNEL_RELATIVE_LBA,
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
    from scripts.verify_boot_update_bundle import verify_update_bundle
except ModuleNotFoundError:
    from create_native_boot_image import (
        BOOT_CONTROL_ATTEMPT_LIMIT,
        BACKUP_MANIFEST_RELATIVE_LBA,
        BOOT_CONTROL_PRIMARY_RELATIVE_LBA,
        BOOT_CONTROL_SECONDARY_RELATIVE_LBA,
        BOOT_CONTROL_SLOT_A,
        BOOT_CONTROL_SLOT_B,
        BOOT_CONTROL_VERSION_V2,
        KERNEL_RELATIVE_LBA,
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
    from verify_boot_update_bundle import verify_update_bundle


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
    if source.active_slot not in (BOOT_CONTROL_SLOT_A, BOOT_CONTROL_SLOT_B) or \
            source.boot_control_sequence is None:
        raise ValueError("source image lacks a confirmed boot slot")
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
    with source_image.open("rb") as image:
        selected = read_boot_control(image, source.partition_lba)
    if selected.active_slot != source.active_slot or \
            selected.sequence != source.boot_control_sequence:
        raise ValueError("source boot-control selection changed")
    target_slot = BOOT_CONTROL_SLOT_B if selected.active_slot == \
        BOOT_CONTROL_SLOT_A else BOOT_CONTROL_SLOT_A
    if target_slot == BOOT_CONTROL_SLOT_A and \
            selected.version != BOOT_CONTROL_VERSION_V2:
        raise ValueError("reverse inactive-slot updates require boot-control v2")
    target_manifest_lba = BACKUP_MANIFEST_RELATIVE_LBA \
        if target_slot == BOOT_CONTROL_SLOT_B else 0
    target_kernel_lba = KERNEL_B_RELATIVE_LBA \
        if target_slot == BOOT_CONTROL_SLOT_B else KERNEL_RELATIVE_LBA
    target_limit_lba = source.partition_sectors \
        if target_slot == BOOT_CONTROL_SLOT_B else KERNEL_B_RELATIVE_LBA
    if target_kernel_lba + sectors_for(len(kernel)) > target_limit_lba:
        raise ValueError("signed kernel exceeds inactive slot")

    output_image.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source_image, output_image)
    with output_image.open("r+b") as image:
        partition_offset = source.partition_lba * SECTOR_SIZE
        image.seek(partition_offset)
        manifest_a = image.read(SECTOR_SIZE)
        stage2_sectors = struct.unpack_from("<I", manifest_a, 20)[0]
        target_manifest = create_manifest(
            stage2_sectors, kernel, source.partition_sectors, signature,
            target_kernel_lba,
        )

        _durable_write(
            image,
            partition_offset + target_kernel_lba * SECTOR_SIZE,
            kernel,
        )
        if boundary_hook is not None:
            boundary_hook(f"kernel-{'b' if target_slot else 'a'}", output_image)
        _durable_write(
            image,
            partition_offset + target_manifest_lba * SECTOR_SIZE,
            target_manifest,
        )
        if boundary_hook is not None:
            boundary_hook(f"manifest-{'b' if target_slot else 'a'}", output_image)

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
            active_slot=selected.active_slot,
            pending_slot=target_slot,
            attempts_remaining=BOOT_CONTROL_ATTEMPT_LIMIT,
            successful_mask=selected.successful_mask & ~(1 << target_slot),
            version=selected.version,
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
    if final.active_slot != selected.active_slot or \
            final.pending_slot != target_slot or \
            final.attempts_remaining != BOOT_CONTROL_ATTEMPT_LIMIT or \
            final.successful_mask != \
            (selected.successful_mask & ~(1 << target_slot)) or \
            final.boot_control_sequence != source.boot_control_sequence + 1:
        raise ValueError("inactive pending slot was not published atomically")


def update_inactive_slot_from_bundle(
        source_image: Path,
        bundle_path: Path,
        output_image: Path,
        policy_path: Path,
        openssl: Path,
        root: Path,
        boundary_hook: BoundaryHook | None = None) -> None:
    verified = verify_update_bundle(bundle_path, policy_path, openssl, root)
    with tempfile.TemporaryDirectory(prefix="reist-update-apply-") as directory:
        kernel_path = Path(directory) / "kernel.bin"
        signature_path = Path(directory) / "kernel.bin.sig"
        kernel_path.write_bytes(verified.kernel)
        signature_path.write_bytes(verified.signature)

        def preverified(
                artifact: Path, signature: Path, _policy: Path,
                _openssl: Path, _root: Path) -> str:
            if artifact.read_bytes() != verified.kernel or \
                    signature.read_bytes() != verified.signature:
                raise ValueError("verified bundle extraction changed")
            return verified.kernel_sha256

        update_inactive_slot(
            source_image, kernel_path, signature_path, output_image,
            policy_path, openssl, root, verifier=preverified,
            boundary_hook=boundary_hook,
        )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--kernel", type=Path)
    parser.add_argument("--signature", type=Path)
    parser.add_argument("--bundle", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--policy", type=Path, required=True)
    parser.add_argument("--openssl", type=Path, default=Path("openssl"))
    parser.add_argument("--root", type=Path, default=Path.cwd())
    args = parser.parse_args()
    if args.bundle is None and (args.kernel is None or args.signature is None):
        parser.error("either --bundle or both --kernel and --signature are required")
    if args.bundle is not None and \
            (args.kernel is not None or args.signature is not None):
        parser.error("--bundle cannot be combined with --kernel or --signature")
    try:
        if args.bundle is not None:
            update_inactive_slot_from_bundle(
                args.image, args.bundle, args.output, args.policy,
                args.openssl, args.root,
            )
        else:
            update_inactive_slot(
                args.image, args.kernel, args.signature, args.output,
                args.policy, args.openssl, args.root,
            )
        info = validate_image(args.output, "hdd")
    except (OSError, ValueError) as error:
        print(f"BOOT UPDATE FAIL: {error}")
        return 1
    print(
        f"BOOT UPDATE PASS: pending={'B' if info.pending_slot else 'A'} "
        f"attempts={info.attempts_remaining} "
        f"sequence={info.boot_control_sequence} output={args.output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
