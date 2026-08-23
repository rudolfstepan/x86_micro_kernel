#!/usr/bin/env python3
"""Prove persistent pending-slot decrement and bounded rollback in QEMU."""

from __future__ import annotations

import argparse
import subprocess
from pathlib import Path

try:
    from scripts.run_qemu_smoke import qemu_command
    from scripts.update_native_boot_slot import update_inactive_slot
    from scripts.validate_boot_manifest import validate_image
except ModuleNotFoundError:
    from run_qemu_smoke import qemu_command
    from update_native_boot_slot import update_inactive_slot
    from validate_boot_manifest import validate_image


ERROR_MARKER = "Boot control validation/write failed"
EXPECTED = (
    ("BOOT_CONTROL_PENDING_B attempts=1", 1, 1),
    ("BOOT_CONTROL_PENDING_B attempts=0", 1, 0),
    ("BOOT_CONTROL_ROLLBACK_A", 0xFF, 0),
)


def _text(value: str | bytes | None) -> str:
    if value is None:
        return ""
    if isinstance(value, bytes):
        return value.decode("utf-8", errors="replace")
    return value


def _run_once(qemu: Path, image: Path, timeout: float) -> str:
    command = qemu_command(
        qemu, image, memory="128M", persistent=True
    )
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
        return completed.stdout
    except subprocess.TimeoutExpired as error:
        return _text(error.stdout)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--kernel", type=Path, required=True)
    parser.add_argument("--signature", type=Path, required=True)
    parser.add_argument("--policy", type=Path, required=True)
    parser.add_argument("--openssl", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=4.0)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    args = parser.parse_args()
    if args.timeout <= 0 or args.timeout > 10:
        parser.error("--timeout must be in (0, 10] seconds")

    transcripts = []
    try:
        update_inactive_slot(
            args.image, args.kernel, args.signature, args.output,
            args.policy, args.openssl, args.root,
        )
        initial = validate_image(args.output, "hdd")
        if initial.pending_slot != 1 or initial.attempts_remaining != 2:
            raise ValueError("updater did not publish the two-attempt B trial")

        for index, (marker, pending, attempts) in enumerate(EXPECTED, 1):
            transcript = _run_once(args.qemu, args.output, args.timeout)
            transcripts.append(f"=== boot {index} ===\n{transcript}")
            if ERROR_MARKER in transcript:
                raise ValueError("stage 2 rejected or failed to persist control")
            if marker not in transcript:
                raise ValueError(f"boot {index} marker is missing: {marker}")
            state = validate_image(args.output, "hdd")
            if state.pending_slot != pending or \
                    state.attempts_remaining != attempts:
                raise ValueError(
                    f"boot {index} persisted unexpected state: "
                    f"pending={state.pending_slot} attempts={state.attempts_remaining}"
                )
        final = validate_image(args.output, "hdd")
        if final.boot_control_sequence != initial.boot_control_sequence + 3:
            raise ValueError("boot-control sequence did not advance once per boot")
    except (OSError, ValueError) as error:
        args.log.parent.mkdir(parents=True, exist_ok=True)
        args.log.write_text("\n".join(transcripts), encoding="utf-8")
        print(f"boot-control: FAIL: {error}")
        return 1

    args.log.parent.mkdir(parents=True, exist_ok=True)
    args.log.write_text("\n".join(transcripts), encoding="utf-8")
    print(
        "boot-control: PASS pending=B:1,B:0 rollback=A "
        f"sequence={final.boot_control_sequence} log={args.log}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
