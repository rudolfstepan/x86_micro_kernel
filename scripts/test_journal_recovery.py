#!/usr/bin/env python3
"""Inject an interrupted REIST undo transaction and verify boot recovery."""
from __future__ import annotations
import argparse, binascii, shutil, struct, subprocess, sys
from pathlib import Path

SECTOR_SIZE = 512
DATA_PARTITION_START = 8192
JOURNAL_MAGIC = 0x4A545352

def inject(source: Path, destination: Path) -> bytes:
    shutil.copyfile(source, destination)
    image = bytearray(destination.read_bytes())
    target, header_lba, data_lba = (DATA_PARTITION_START + n for n in (6, 8, 9))
    old = bytes(image[target * SECTOR_SIZE:(target + 1) * SECTOR_SIZE])
    image[data_lba * SECTOR_SIZE:(data_lba + 1) * SECTOR_SIZE] = old
    record = bytearray(SECTOR_SIZE)
    struct.pack_into("<6I", record, 0, JOURNAL_MAGIC, 1, 1, target,
                     binascii.crc32(old) & 0xFFFFFFFF, 1)
    struct.pack_into("<I", record, 24,
                     binascii.crc32(record[:24]) & 0xFFFFFFFF)
    image[header_lba * SECTOR_SIZE:(header_lba + 1) * SECTOR_SIZE] = record
    image[target * SECTOR_SIZE:(target + 1) * SECTOR_SIZE] = b"\xA5" * SECTOR_SIZE
    destination.write_bytes(image)
    return old

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", type=Path, default=Path("qemu-system-i386"))
    parser.add_argument("--image", required=True, type=Path)
    parser.add_argument("--work-image", required=True, type=Path)
    parser.add_argument("--log", required=True, type=Path)
    parser.add_argument("--timeout", type=float, default=90.0)
    args = parser.parse_args()
    if not args.image.is_file() or args.timeout <= 0: return 2
    args.work_image.parent.mkdir(parents=True, exist_ok=True)
    expected = inject(args.image, args.work_image)
    runner = Path(__file__).with_name("run_qemu_smoke.py")
    result = subprocess.run(
        [sys.executable, str(runner), "--qemu", str(args.qemu), "--image",
         str(args.work_image), "--persistent", "--watchdog", "--timeout",
         str(args.timeout), "--log", str(args.log)], check=False)
    if result.returncode != 0: return result.returncode
    image = args.work_image.read_bytes()
    target, header_lba = DATA_PARTITION_START + 6, DATA_PARTITION_START + 8
    restored = image[target * SECTOR_SIZE:(target + 1) * SECTOR_SIZE]
    state = struct.unpack_from("<I", image, header_lba * SECTOR_SIZE + 8)[0]
    if restored != expected or state != 0:
        print("journal-recovery: restored sector or CLEAN state mismatch", file=sys.stderr)
        return 1
    print("journal-recovery: PASS")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
