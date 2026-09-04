#!/usr/bin/env python3
"""Prove bounded native EXT2 symlinks through packaged Ring-3 tools."""

from __future__ import annotations

import argparse
import hashlib
import queue
import subprocess
import sys
import threading
import time
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_qemu_smoke as smoke


BLOCK_SIZE = 1024
BLOCKS = 8192
SECTORS = BLOCKS * 2
JOURNAL_SECTORS = 26
MOUNT = "/mnt/hdd1"
PAYLOAD = "SYMLINK TARGET"


def put16(image: bytearray, offset: int, value: int) -> None:
    image[offset:offset + 2] = value.to_bytes(2, "little")


def put32(image: bytearray, offset: int, value: int) -> None:
    image[offset:offset + 4] = value.to_bytes(4, "little")


def get16(image: bytearray | bytes, offset: int) -> int:
    return int.from_bytes(image[offset:offset + 2], "little")


def inode_offset(number: int) -> int:
    return 5 * BLOCK_SIZE + (number - 1) * 128


def make_inode(image: bytearray, number: int, mode: int, size: int,
               block: int = 0) -> None:
    offset = inode_offset(number)
    put16(image, offset, mode)
    put32(image, offset + 4, size)
    put32(image, offset + 8, 1_700_000_001)
    put32(image, offset + 12, 1_700_000_002)
    put32(image, offset + 16, 1_700_000_003)
    put16(image, offset + 26, 1)
    put32(image, offset + 28, 2 if block else 0)
    put32(image, offset + 40, block)
    put32(image, offset + 100, number)


def record_size(length: int) -> int:
    return (8 + length + 3) & ~3


def add_entry(image: bytearray, block: int, offset: int, inode: int,
              name: bytes, file_type: int, record_length: int) -> int:
    base = block * BLOCK_SIZE + offset
    put32(image, base, inode)
    put16(image, base + 4, record_length)
    image[base + 6] = len(name)
    image[base + 7] = file_type
    image[base + 8:base + 8 + len(name)] = name
    return offset + record_length


def mark_allocated(bitmap: memoryview, count: int) -> None:
    for bit in range(count):
        bitmap[bit // 8] |= 1 << (bit & 7)


def volume_signature(superblock: bytes) -> int:
    stable = bytearray(superblock)
    for start, end in ((12, 20), (44, 56), (58, 60),
                       (64, 68), (232, 236)):
        stable[start:end] = b"\0" * (end - start)
    value = 2_166_136_261
    for byte in stable:
        value = ((value ^ byte) * 16_777_619) & 0xFFFFFFFF
    return value or 1


def journal_header(signature: int, target_sector: int,
                   before: bytes, final: bytes) -> bytes:
    header = bytearray(512)
    put32(header, 0, 0x4B4E4C53)
    put32(header, 4, 1)
    put32(header, 8, 1)  # ACTIVE: recovery must restore before-image.
    put32(header, 12, 7)
    put32(header, 16, 1)
    put32(header, 20, signature)
    put32(header, 32, target_sector)
    put32(header, 36, zlib.crc32(before) & 0xFFFFFFFF)
    put32(header, 40, zlib.crc32(final) & 0xFFFFFFFF)
    put32(header, 44, 1)  # Namespace publication sector.
    put32(header, 24, zlib.crc32(header) & 0xFFFFFFFF)
    return bytes(header)


def create_ext2_image(path: Path) -> None:
    image = bytearray(BLOCKS * BLOCK_SIZE)
    superblock = BLOCK_SIZE
    put32(image, superblock + 0, 128)
    put32(image, superblock + 4, BLOCKS)
    put32(image, superblock + 12, (BLOCKS - 1) - 37)
    put32(image, superblock + 16, 128 - 13)
    put32(image, superblock + 20, 1)
    put32(image, superblock + 24, 0)
    put32(image, superblock + 32, BLOCKS)
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
    put16(image, descriptor + 12, (BLOCKS - 1) - 37)
    put16(image, descriptor + 14, 128 - 13)
    mark_allocated(memoryview(image)[3 * BLOCK_SIZE:4 * BLOCK_SIZE], 37)
    mark_allocated(memoryview(image)[4 * BLOCK_SIZE:5 * BLOCK_SIZE], 13)

    make_inode(image, 2, 0x41ED, BLOCK_SIZE, 21)
    put16(image, inode_offset(2) + 26, 2)
    payload = (PAYLOAD + "\n").encode("ascii")
    make_inode(image, 12, 0x81A4, len(payload), 22)
    image[22 * BLOCK_SIZE:22 * BLOCK_SIZE + len(payload)] = payload
    make_inode(image, 13, 0x81A4, JOURNAL_SECTORS * 512, 24)
    for logical in range(12):
        put32(image, inode_offset(13) + 40 + logical * 4, 24 + logical)
    put32(image, inode_offset(13) + 40 + 12 * 4, 37)
    put32(image, 37 * BLOCK_SIZE, 36)
    put32(image, inode_offset(13) + 28, 28)

    offset = add_entry(image, 21, 0, 2, b".", 2, 12)
    offset = add_entry(image, 21, offset, 2, b"..", 2, 12)
    offset = add_entry(image, 21, offset, 12, b"target.txt", 1,
                       record_size(len(b"target.txt")))
    journal_offset = offset
    add_entry(image, 21, offset, 13, b".reist-symlink-journal", 1,
              BLOCK_SIZE - offset)

    root_sector = 21 * 2
    before = bytes(image[root_sector * 512:(root_sector + 1) * 512])
    actual = record_size(len(b".reist-symlink-journal"))
    old_record = get16(image, 21 * BLOCK_SIZE + journal_offset + 4)
    put16(image, 21 * BLOCK_SIZE + journal_offset + 4, actual)
    add_entry(image, 21, journal_offset + actual, 12, b"partial-link", 7,
              old_record - actual)
    final = bytes(image[root_sector * 512:(root_sector + 1) * 512])
    signature = volume_signature(
        bytes(image[BLOCK_SIZE:2 * BLOCK_SIZE]))
    header = journal_header(signature, root_sector, before, final)
    image[24 * BLOCK_SIZE:24 * BLOCK_SIZE + 512] = header
    image[24 * BLOCK_SIZE + 512:25 * BLOCK_SIZE] = header
    image[25 * BLOCK_SIZE:25 * BLOCK_SIZE + 512] = before
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(image)


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def qemu_command(qemu: Path, image: Path, ext2_image: Path) -> list[str]:
    command = smoke.qemu_command(qemu, image, persistent=True)
    system_drive = f"file={image},format=raw,if=ide,index=0,media=disk"
    try:
        system_drive_index = command.index(system_drive)
    except ValueError as error:
        raise RuntimeError("QEMU system drive contract changed") from error
    command[system_drive_index] = system_drive + ",snapshot=on"
    command.extend([
        "-drive", f"file={ext2_image},format=raw,if=ide,index=1,media=disk",
    ])
    return command


def run(qemu: Path, image: Path, disk: Path, timeout: float, log: Path) -> int:
    if image == disk:
        raise ValueError("EXT2 test disk must not replace the reference image")
    reference_digest = file_sha256(image)
    create_ext2_image(disk)
    initial_disk_digest = file_sha256(disk)
    initial_raw = disk.read_bytes()
    process = subprocess.Popen(
        qemu_command(qemu, image, disk), stdin=subprocess.PIPE,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
        encoding="utf-8", errors="replace", bufsize=0,
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
    prompt = -1

    def command(text: str, required: tuple[str, ...] = (),
                forbidden: tuple[str, ...] = ()) -> None:
        nonlocal error, prompt
        if error is not None:
            return
        smoke.inject_ps2_command(process, text)
        error, next_prompt = smoke.wait_for_line(
            process, chunks, transcript, finished, smoke.SHELL_PROMPT,
            deadline, after=prompt)
        if error is not None:
            return
        output = "".join(transcript)[prompt + 1:next_prompt]
        for marker in required:
            if marker not in output:
                error = f"{text!r} did not emit {marker!r}"
                return
        for marker in forbidden:
            if marker in output:
                error = f"{text!r} unexpectedly emitted {marker!r}"
                return
        prompt = next_prompt

    try:
        error, boot = smoke.wait_for_line(
            process, chunks, transcript, finished, smoke.BOOT_MARKER, deadline)
        if error is None:
            error, prompt = smoke.wait_for_line(
                process, chunks, transcript, finished, smoke.SHELL_PROMPT,
                deadline, after=boot)
        command(f"ls {MOUNT}", ("target.txt",), ("partial-link",))
        command(f"ln -s target.txt {MOUNT}/relative-link")
        command(f"readlink {MOUNT}/relative-link", ("target.txt",))
        command(f"cat {MOUNT}/relative-link", (PAYLOAD,))
        command(f"ln -s {MOUNT}/target.txt {MOUNT}/absolute-link")
        command(f"cat {MOUNT}/absolute-link", (PAYLOAD,))
        command(f"ln -s relative-link {MOUNT}/chain-link")
        command(f"cat {MOUNT}/chain-link", (PAYLOAD,))
        command(f"ln -s missing.txt {MOUNT}/dangling-link")
        command(f"readlink {MOUNT}/dangling-link", ("missing.txt",))
        command(f"cat {MOUNT}/dangling-link", ("cat: cannot open file",))
        command(f"ln -s cycle-b {MOUNT}/cycle-a")
        command(f"ln -s cycle-a {MOUNT}/cycle-b")
        command(f"cat {MOUNT}/cycle-a", ("cat: cannot open file",))
        command(f"del {MOUNT}/relative-link",
                forbidden=("del: file not found or cannot be removed",))
        command(f"readlink {MOUNT}/relative-link",
                ("readlink: path is not a readable symbolic link",))
        command(f"cat {MOUNT}/target.txt", (PAYLOAD,))
        command(f"rename {MOUNT}/absolute-link {MOUNT}/renamed-link",
                forbidden=("rename: operation unsupported or failed",))
        command(f"readlink {MOUNT}/absolute-link",
                ("readlink: path is not a readable symbolic link",))
        command(f"readlink {MOUNT}/renamed-link", (MOUNT + "/target.txt",))
        command(f"cat {MOUNT}/target.txt", (PAYLOAD,))
        command("svcctl restart 5", ("COMPONENT RESTART_OK component=5",))
        command(f"readlink {MOUNT}/renamed-link", (MOUNT + "/target.txt",))
        command(f"readlink {MOUNT}/chain-link", ("relative-link",))
        command(f"cat {MOUNT}/chain-link", ("cat: cannot open file",))
        command(f"rename {MOUNT}/target.txt {MOUNT}/moved.txt",
                forbidden=("rename: operation unsupported or failed",))
        command(f"cat {MOUNT}/target.txt", ("cat: cannot open file",))
        command(f"cat {MOUNT}/moved.txt", (PAYLOAD,))
        command("svcctl restart 5", ("COMPONENT RESTART_OK component=5",))
        command(f"cat {MOUNT}/moved.txt", (PAYLOAD,))
        command(f"readlink {MOUNT}/renamed-link", (MOUNT + "/target.txt",))
        command(f"cat {MOUNT}/renamed-link", ("cat: cannot open file",))
        command(f"del {MOUNT}/moved.txt",
                forbidden=("del: file not found or cannot be removed",))
        command(f"cat {MOUNT}/moved.txt", ("cat: cannot open file",))
        command("svcctl restart 5", ("COMPONENT RESTART_OK component=5",))
        command(f"cat {MOUNT}/moved.txt", ("cat: cannot open file",))
        command(f"readlink {MOUNT}/renamed-link", (MOUNT + "/target.txt",))
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
        error = error or "reference image changed"
    if file_sha256(disk) == initial_disk_digest:
        error = error or "EXT2 test disk did not persist link transactions"
    raw = disk.read_bytes()
    if any(raw[inode_offset(12):inode_offset(12) + 128]):
        error = error or "regular EXT2 inode was not cleared by unlink"
    if raw[22 * BLOCK_SIZE:23 * BLOCK_SIZE] != \
            initial_raw[22 * BLOCK_SIZE:23 * BLOCK_SIZE]:
        error = error or "regular EXT2 data block changed during unlink"
    block_bit = 22 - 1
    if raw[3 * BLOCK_SIZE + block_bit // 8] & (1 << (block_bit & 7)):
        error = error or "regular EXT2 data block remained allocated"
    inode_bit = 12 - 1
    if raw[4 * BLOCK_SIZE + inode_bit // 8] & (1 << (inode_bit & 7)):
        error = error or "regular EXT2 inode remained allocated"
    for offset in (24 * BLOCK_SIZE, 24 * BLOCK_SIZE + 512):
        header = raw[offset:offset + 512]
        recorded = int.from_bytes(header[24:28], "little")
        checked = bytearray(header)
        checked[24:28] = b"\0\0\0\0"
        if (int.from_bytes(header[0:4], "little") != 0x4B4E4C53 or
                int.from_bytes(header[8:12], "little") != 0 or
                zlib.crc32(checked) & 0xFFFFFFFF != recorded):
            error = error or "EXT2 symlink journal was not left clean"
    if error is not None:
        print(f"EXT2 SYMLINK FAIL: {error}; log={log}")
        return 1
    print(f"EXT2 SYMLINK PASS log={log}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--disk", type=Path,
                        default=Path("build/ext2-symlink.img"))
    parser.add_argument("--log", type=Path,
                        default=Path("build/guest-ext2-symlink.log"))
    parser.add_argument("--timeout", type=float, default=180.0)
    args = parser.parse_args()
    if not args.qemu.is_file() or not args.image.is_file() or args.timeout <= 0:
        parser.error("qemu/image must exist and timeout must be positive")
    return run(args.qemu.resolve(), args.image.resolve(), args.disk.resolve(),
               args.timeout, args.log.resolve())


if __name__ == "__main__":
    raise SystemExit(main())
