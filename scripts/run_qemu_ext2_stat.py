#!/usr/bin/env python3
"""Run packaged stat/cat/ls clients against a deterministic EXT2 disk."""

from __future__ import annotations

import argparse
import hashlib
import queue
import subprocess
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_qemu_smoke as smoke


BLOCK_SIZE = 1024
BLOCKS = 8192
SECTORS = BLOCKS * 2
STAT_NAME = "Name: readme.txt"
STAT_SIZE = "Size: 15 bytes"
STAT_COMMAND = "stat /mnt/hdd1/readme.txt"
CAT_COMMAND = "cat /mnt/hdd1/readme.txt"
CAT_TEXT = "EXT2 AUTHORITY"
LS_COMMAND = "ls /mnt/hdd1"
LS_NAME = "readme.txt"


def put16(image: bytearray, offset: int, value: int) -> None:
    image[offset:offset + 2] = value.to_bytes(2, "little")


def put32(image: bytearray, offset: int, value: int) -> None:
    image[offset:offset + 4] = value.to_bytes(4, "little")


def inode_offset(number: int) -> int:
    return 5 * BLOCK_SIZE + (number - 1) * 128


def make_inode(image: bytearray, number: int, mode: int, size: int,
               block: int) -> None:
    offset = inode_offset(number)
    put16(image, offset, mode)
    put32(image, offset + 4, size)
    put32(image, offset + 8, 1_700_000_001)
    put32(image, offset + 12, 1_700_000_002)
    put32(image, offset + 16, 1_700_000_003)
    put16(image, offset + 26, 1)
    put32(image, offset + 28, 2)
    put32(image, offset + 40, block)


def add_entry(image: bytearray, block: int, offset: int, inode: int,
              name: bytes, file_type: int, record_length: int) -> int:
    base = block * BLOCK_SIZE + offset
    put32(image, base, inode)
    put16(image, base + 4, record_length)
    image[base + 6] = len(name)
    image[base + 7] = file_type
    image[base + 8:base + 8 + len(name)] = name
    return offset + record_length


def create_ext2_image(path: Path) -> None:
    image = bytearray(BLOCKS * BLOCK_SIZE)
    superblock = BLOCK_SIZE
    put32(image, superblock + 0, 128)
    put32(image, superblock + 4, BLOCKS)
    put32(image, superblock + 20, 1)
    put32(image, superblock + 24, 0)
    put32(image, superblock + 28, 0)
    put32(image, superblock + 32, BLOCKS)
    put32(image, superblock + 36, BLOCKS)
    put32(image, superblock + 40, 128)
    put16(image, superblock + 56, 0xEF53)
    put16(image, superblock + 58, 1)
    put32(image, superblock + 76, 1)
    put32(image, superblock + 84, 11)
    put16(image, superblock + 88, 128)
    put32(image, superblock + 96, 2)

    descriptor = 2 * BLOCK_SIZE
    put32(image, descriptor + 0, 3)
    put32(image, descriptor + 4, 4)
    put32(image, descriptor + 8, 5)

    make_inode(image, 2, 0x41ED, BLOCK_SIZE, 21)
    put16(image, inode_offset(2) + 26, 2)
    payload = b"EXT2 AUTHORITY\n"
    make_inode(image, 12, 0x81A4, len(payload), 22)

    offset = add_entry(image, 21, 0, 2, b".", 2, 12)
    offset = add_entry(image, 21, offset, 2, b"..", 2, 12)
    add_entry(image, 21, offset, 12, b"readme.txt", 1,
              BLOCK_SIZE - offset)
    image[22 * BLOCK_SIZE:22 * BLOCK_SIZE + len(payload)] = payload
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(image)


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def qemu_command(qemu: Path, image: Path, ext2_image: Path) -> list[str]:
    command = smoke.qemu_command(qemu, image)
    command.extend([
        "-drive",
        f"file={ext2_image},format=raw,if=ide,index=1,media=disk",
    ])
    return command


def wait_for_text(process: subprocess.Popen[str], chunks: queue.Queue[str],
                  transcript: list[str], finished: threading.Event,
                  expected: str, deadline: float, *, after: int = -1
                  ) -> tuple[str | None, int]:
    while time.monotonic() < deadline:
        smoke.drain(chunks, transcript)
        text = "".join(transcript)
        failed = smoke.failure_marker(text)
        if failed is not None:
            return f"guest emitted failure marker {failed!r}", -1
        position = text.find(expected, after + 1)
        if position >= 0:
            return None, position
        if process.poll() is not None:
            finished.wait(timeout=0.25)
            smoke.drain(chunks, transcript)
            text = "".join(transcript)
            position = text.find(expected, after + 1)
            if position >= 0:
                return None, position
            return f"QEMU exited before {expected}", -1
        try:
            transcript.append(chunks.get(timeout=0.05))
        except queue.Empty:
            pass
    return f"timeout before {expected}", -1


def run(qemu: Path, image: Path, ext2_image: Path, timeout: float,
        log: Path) -> int:
    if image == ext2_image:
        raise ValueError("EXT2 test disk must not replace the reference image")
    reference_digest = file_sha256(image)
    create_ext2_image(ext2_image)
    ext2_digest = file_sha256(ext2_image)
    process = subprocess.Popen(
        qemu_command(qemu, image, ext2_image),
        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, text=True, encoding="utf-8",
        errors="replace", bufsize=0,
    )
    assert process.stdout is not None
    chunks: queue.Queue[str] = queue.Queue()
    transcript: list[str] = []
    finished = threading.Event()
    reader = threading.Thread(target=smoke.reader,
                              args=(process.stdout, chunks, finished),
                              daemon=True)
    reader.start()
    deadline = time.monotonic() + timeout
    error: str | None = None
    try:
        error, marker = smoke.wait_for_line(
            process, chunks, transcript, finished, smoke.BOOT_MARKER, deadline)
        if error is None:
            error, prompt = smoke.wait_for_line(
                process, chunks, transcript, finished, smoke.SHELL_PROMPT,
                deadline, after=marker)
        if error is None:
            smoke.inject_ps2_command(process, STAT_COMMAND)
            error, name_marker = smoke.wait_for_line(
                process, chunks, transcript, finished, STAT_NAME, deadline,
                after=prompt)
        if error is None:
            error, size_marker = smoke.wait_for_line(
                process, chunks, transcript, finished, STAT_SIZE, deadline,
                after=name_marker)
        if error is None:
            error, stat_prompt = smoke.wait_for_line(
                process, chunks, transcript, finished, smoke.SHELL_PROMPT,
                deadline, after=size_marker)
        if error is None:
            smoke.inject_ps2_command(process, CAT_COMMAND)
            error, cat_marker = smoke.wait_for_line(
                process, chunks, transcript, finished, CAT_TEXT, deadline,
                after=stat_prompt)
        if error is None:
            error, cat_prompt = smoke.wait_for_line(
                process, chunks, transcript, finished, smoke.SHELL_PROMPT,
                deadline, after=cat_marker)
        if error is None:
            smoke.inject_ps2_command(process, LS_COMMAND)
            error, ls_marker = wait_for_text(
                process, chunks, transcript, finished, LS_NAME, deadline,
                after=cat_prompt)
        if error is None:
            error, _ = smoke.wait_for_line(
                process, chunks, transcript, finished, smoke.SHELL_PROMPT,
                deadline, after=ls_marker)
    except (OSError, RuntimeError, ValueError) as caught:
        error = str(caught)
    finally:
        smoke.stop_process(process)
        finished.wait(timeout=1.0)
        smoke.drain(chunks, transcript)
        reader.join(timeout=1.0)
        log.parent.mkdir(parents=True, exist_ok=True)
        log.write_text("".join(transcript), encoding="utf-8")
    if file_sha256(image) != reference_digest:
        print(f"EXT2 STAT FAIL: reference image changed; log={log}")
        return 1
    if file_sha256(ext2_image) != ext2_digest:
        print(f"EXT2 STAT FAIL: test disk changed; log={log}")
        return 1
    if error is not None:
        print(f"EXT2 STAT FAIL: {error}; log={log}")
        return 1
    print(f"EXT2 STAT PASS log={log}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--disk", type=Path,
                        default=Path("build/ext2-stat.img"))
    parser.add_argument("--log", type=Path,
                        default=Path("build/guest-ext2-stat.log"))
    parser.add_argument("--timeout", type=float, default=150.0)
    args = parser.parse_args()
    if not args.qemu.is_file() or not args.image.is_file() or args.timeout <= 0:
        parser.error("qemu/image must exist and timeout must be positive")
    return run(args.qemu.resolve(), args.image.resolve(), args.disk.resolve(),
               args.timeout, args.log.resolve())


if __name__ == "__main__":
    raise SystemExit(main())
