#!/usr/bin/env python3
"""Prove symmetric Ring-3 boot-slot confirmation and rollback in QEMU."""

from __future__ import annotations

import argparse
import os
import struct
import subprocess
from pathlib import Path

try:
    from scripts.run_qemu_smoke import qemu_command
    from scripts.update_native_boot_slot import update_inactive_slot
    from scripts.validate_boot_manifest import validate_image as validate_boot_image
except ModuleNotFoundError:
    from run_qemu_smoke import qemu_command
    from update_native_boot_slot import update_inactive_slot
    from validate_boot_manifest import validate_image as validate_boot_image


SECTOR_SIZE = 512
BACKUP_MANIFEST_LBA = 96
SIGNATURE_OFFSET = 80
CHECKSUM_OFFSET = 44
# The standard REIST resilience probe deliberately executes UD2 and proves its
# supervised recovery during every normal boot.  Its generic user-exception
# diagnostic is therefore not a boot-success failure marker.
ERROR_MARKERS = ("Boot control validation/write failed", "PANIC:", "FATAL:")


def _text(value: str | bytes | None) -> str:
    if value is None:
        return ""
    if isinstance(value, bytes):
        return value.decode("utf-8", errors="replace")
    return value


def _run_once(qemu: Path, image: Path, timeout: float) -> str:
    try:
        completed = subprocess.run(
            qemu_command(qemu, image, memory="128M", persistent=True),
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=timeout,
            check=False,
        )
        return completed.stdout
    except subprocess.TimeoutExpired as error:
        return _text(error.stdout)


def _require(transcript: str, markers: tuple[str, ...], boot: int) -> None:
    if any(marker in transcript for marker in ERROR_MARKERS):
        raise ValueError(f"boot {boot} emitted a fatal marker")
    position = -1
    for marker in markers:
        current = transcript.find(marker, position + 1)
        if current < 0:
            raise ValueError(f"boot {boot} marker is missing: {marker}")
        position = current


def _corrupt_backup_signature(image: Path, partition_lba: int) -> None:
    offset = (partition_lba + BACKUP_MANIFEST_LBA) * SECTOR_SIZE
    with image.open("r+b") as stream:
        stream.seek(offset)
        manifest = bytearray(stream.read(SECTOR_SIZE))
        if len(manifest) != SECTOR_SIZE:
            raise ValueError("backup manifest is truncated")
        manifest[SIGNATURE_OFFSET] ^= 0x01
        struct.pack_into("<I", manifest, CHECKSUM_OFFSET, 0)
        words = struct.unpack("<128I", manifest)
        struct.pack_into("<I", manifest, CHECKSUM_OFFSET,
                         (-sum(words)) & 0xFFFFFFFF)
        stream.seek(offset)
        if stream.write(manifest) != len(manifest):
            raise OSError("short backup-manifest write")
        stream.flush()
        os.fsync(stream.fileno())
        stream.seek(offset)
        if stream.read(SECTOR_SIZE) != manifest:
            raise OSError("backup-manifest read-back mismatch")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--kernel", type=Path, required=True)
    parser.add_argument("--signature", type=Path, required=True)
    parser.add_argument("--policy", type=Path, required=True)
    parser.add_argument("--openssl", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--reverse-output", type=Path, required=True)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=15.0)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    args = parser.parse_args()
    if args.timeout <= 0.0 or args.timeout > 30.0:
        parser.error("--timeout must be in (0, 30] seconds")

    transcripts: list[str] = []
    try:
        update_inactive_slot(
            args.image, args.kernel, args.signature, args.output,
            args.policy, args.openssl, args.root,
        )
        initial = validate_boot_image(args.output, "hdd")
        if initial.pending_slot != 1 or initial.attempts_remaining != 2:
            raise ValueError("updater did not publish pending B")

        first = _run_once(args.qemu, args.output, args.timeout)
        transcripts.append("=== confirm pending B ===\n" + first)
        _require(first, ("BOOT_CONTROL_PENDING_B attempts=1", "BOOT_OK"), 1)
        confirmed = validate_boot_image(args.output, "hdd")
        if (confirmed.active_slot, confirmed.pending_slot,
                confirmed.attempts_remaining, confirmed.successful_mask) != (
                    1, 0xFF, 0, 3):
            raise ValueError("Ring-3 did not persist confirmed B")
        if confirmed.boot_control_sequence != initial.boot_control_sequence + 2:
            raise ValueError("trial decrement and acknowledgement sequence drift")

        second = _run_once(args.qemu, args.output, args.timeout)
        transcripts.append("=== reboot confirmed B ===\n" + second)
        _require(second, ("BOOT_CONTROL_ACTIVE_B", "BOOT_OK"), 2)
        stable = validate_boot_image(args.output, "hdd")
        if stable.boot_control_sequence != confirmed.boot_control_sequence:
            raise ValueError("confirmed B reboot unexpectedly mutated control")

        update_inactive_slot(
            args.output, args.kernel, args.signature, args.reverse_output,
            args.policy, args.openssl, args.root,
        )
        reverse_initial = validate_boot_image(args.reverse_output, "hdd")
        if (reverse_initial.active_slot, reverse_initial.pending_slot,
                reverse_initial.attempts_remaining,
                reverse_initial.successful_mask) != (1, 0, 2, 2):
            raise ValueError("updater did not publish inactive pending A")

        third = _run_once(args.qemu, args.reverse_output, args.timeout)
        transcripts.append("=== confirm pending A ===\n" + third)
        _require(third, ("BOOT_CONTROL_PENDING_A attempts=1", "BOOT_OK"), 3)
        confirmed_a = validate_boot_image(args.reverse_output, "hdd")
        if (confirmed_a.active_slot, confirmed_a.pending_slot,
                confirmed_a.attempts_remaining,
                confirmed_a.successful_mask) != (0, 0xFF, 0, 3):
            raise ValueError("Ring-3 did not persist confirmed A")
        if confirmed_a.boot_control_sequence != \
                reverse_initial.boot_control_sequence + 2:
            raise ValueError("reverse trial acknowledgement sequence drift")

        fourth = _run_once(args.qemu, args.reverse_output, args.timeout)
        transcripts.append("=== reboot confirmed A ===\n" + fourth)
        _require(fourth, ("BOOT_OK",), 4)
        stable_a = validate_boot_image(args.reverse_output, "hdd")
        if stable_a.boot_control_sequence != confirmed_a.boot_control_sequence:
            raise ValueError("confirmed A reboot unexpectedly mutated control")

        _corrupt_backup_signature(args.output, stable.partition_lba)
        fifth = _run_once(args.qemu, args.output, args.timeout)
        transcripts.append("=== corrupt confirmed B rollback ===\n" + fifth)
        _require(fifth, ("BOOT_CONTROL_ACTIVE_B",
                         "BOOT_CONTROL_CONFIRMED_B_ROLLBACK_A", "BOOT_OK"), 5)
        rolled_back = validate_boot_image(args.output, "hdd")
        if (rolled_back.active_slot, rolled_back.pending_slot,
                rolled_back.attempts_remaining, rolled_back.successful_mask) != (
                    0, 0xFF, 0, 1):
            raise ValueError("confirmed-B failure did not persist rollback A")
        if rolled_back.boot_control_sequence != stable.boot_control_sequence + 1:
            raise ValueError("confirmed-B rollback sequence drift")
    except (OSError, ValueError) as error:
        args.log.parent.mkdir(parents=True, exist_ok=True)
        args.log.write_text("\n".join(transcripts), encoding="utf-8")
        print(f"boot-success: FAIL: {error}")
        return 1

    args.log.parent.mkdir(parents=True, exist_ok=True)
    args.log.write_text("\n".join(transcripts), encoding="utf-8")
    print("boot-success: PASS pending B and pending A confirmed, persistent "
          f"A selected, corrupt confirmed B rolled back to A log={args.log}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
