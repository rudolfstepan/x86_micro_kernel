#!/usr/bin/env python3
"""QEMU proof for fdisk followed by REIST FAT32 provisioning."""
from __future__ import annotations

import argparse
import queue
import subprocess
import threading
import time
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_qemu_smoke as smoke


def make_empty_disk(path: Path, size: int = 128 * 1024 * 1024) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as stream:
        stream.seek(size - 1)
        stream.write(b"\0")


def inject_command(process: subprocess.Popen[str], value: str) -> None:
    if process.stdin is None:
        raise RuntimeError("QEMU monitor input unavailable")
    keys = {" ": "spc", "/": "slash", ".": "dot", "-": "minus"}
    process.stdin.write(smoke.QEMU_MUX_SWITCH)
    process.stdin.flush()
    time.sleep(smoke.KEY_INTERVAL_SECONDS)
    try:
        for character in value:
            key = character if character.isdigit() or "a" <= character <= "z" else keys.get(character)
            if key is None:
                raise ValueError("unsupported provisioning command key")
            process.stdin.write(f"sendkey {key}\n")
            process.stdin.flush()
            time.sleep(smoke.KEY_INTERVAL_SECONDS)
        process.stdin.write("sendkey ret\n")
        process.stdin.flush()
    finally:
        process.stdin.write(smoke.QEMU_MUX_SWITCH)
        process.stdin.flush()


def run(qemu: Path, image: Path, disk: Path, timeout: float, log: Path,
        format_mode: str) -> int:
    make_empty_disk(disk)
    command = smoke.qemu_command(qemu, image, nic="none", sata=True,
                                 auxiliary_sata_image=disk)
    process = subprocess.Popen(command, stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, encoding="utf-8", errors="replace",
                               bufsize=0)
    assert process.stdout is not None
    chunks: queue.Queue[str] = queue.Queue()
    transcript: list[str] = []
    finished = threading.Event()
    reader = threading.Thread(target=smoke.reader,
                              args=(process.stdout, chunks, finished), daemon=True)
    reader.start()
    deadline = time.monotonic() + timeout
    error = None
    position = -1
    try:
        error, position = smoke.wait_for_line(process, chunks, transcript,
                                              finished, smoke.SHELL_PROMPT,
                                              deadline)
        format_command = (f"format --reist-fat32 --{format_mode} 4 --confirm")
        format_marker = (
            "FORMAT: REIST FAT32 quick format verified"
            if format_mode == "quick" else
            "FORMAT: REIST FAT32 full format and bad-cluster blacklist verified"
        )
        commands = [
            ("fdisk", "hdd1p2  PART"),
            ("fdisk --create 0 12 --confirm", "fdisk: partition created"),
            (format_command, format_marker),
            ("mount 4 fat32 /mnt/provision", "ADMIN MOUNT_OK resource=4 path=/mnt/provision"),
        ]
        for command_text, marker in commands:
            if error is not None:
                break
            inject_command(process, command_text)
            error, position = smoke.wait_for_line(
                process, chunks, transcript, finished, marker, deadline,
                after=position)
            if error is None:
                error, position = smoke.wait_for_line(
                    process, chunks, transcript, finished, smoke.SHELL_PROMPT,
                    deadline, after=position)
    except (OSError, RuntimeError, TimeoutError) as caught:
        error = str(caught)
    finally:
        smoke.stop_process(process)
        finished.wait(timeout=1.0)
        smoke.drain(chunks, transcript)
        reader.join(timeout=1.0)
        log.parent.mkdir(parents=True, exist_ok=True)
        log.write_text("".join(transcript), encoding="utf-8")
    if error is not None:
        print(f"PARTITION PROVISIONING FAIL: {error}; log={log}")
        return 1
    print(f"PARTITION PROVISIONING PASS log={log}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--disk", type=Path, required=True)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=180.0)
    parser.add_argument("--format-mode", choices=("quick", "full"),
                        default="quick")
    args = parser.parse_args()
    return run(args.qemu.resolve(), args.image.resolve(), args.disk.resolve(),
               args.timeout, args.log.resolve(), args.format_mode)


if __name__ == "__main__":
    raise SystemExit(main())
