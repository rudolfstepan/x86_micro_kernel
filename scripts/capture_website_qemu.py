#!/usr/bin/env python3
"""Capture reproducible REIST-OS website screenshots from a QEMU guest."""

from __future__ import annotations

import argparse
import binascii
import queue
import struct
import subprocess
import sys
import threading
import time
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_qemu_runtime_desktop as runtime_desktop
import run_qemu_sata_hotplug as sata_hotplug
import run_qemu_smoke as smoke


KEY_NAMES = {
    " ": "spc",
    "/": "slash",
    ".": "dot",
    "-": "minus",
}
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def make_empty_disk(path: Path, size: int = 128 * 1024 * 1024) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as stream:
        stream.seek(size - 1)
        stream.write(b"\0")


def reserve_qmp_port() -> int:
    return sata_hotplug.reserve_port()


def qemu_command(qemu: Path, image: Path, auxiliary: Path,
                 qmp_port: int) -> list[str]:
    return [
        str(qemu),
        "-accel", "tcg",
        "-machine", "pc",
        "-nodefaults",
        "-m", "512M",
        "-boot", "c",
        "-device", "ich9-ahci,id=reistahci",
        "-drive", f"file={auxiliary},format=raw,if=none,id=reistauxdisk",
        "-device", (
            "ide-hd,drive=reistauxdisk,bus=reistahci.0,bootindex=2"
        ),
        "-drive", f"file={image},format=raw,if=none,id=reistdisk",
        "-device", "ide-hd,drive=reistdisk,bus=reistahci.1,bootindex=1",
        "-vga", "std",
        "-display", "none",
        "-monitor", "none",
        "-serial", "stdio",
        "-qmp", f"tcp:127.0.0.1:{qmp_port},server=on,wait=off",
        "-snapshot",
        "-no-reboot",
        "-no-shutdown",
    ]


def inject_command(qmp: sata_hotplug.QmpClient, value: str,
                   deadline: float) -> None:
    for character in value:
        if character.isdigit() or "a" <= character <= "z":
            key = character
        else:
            key = KEY_NAMES.get(character)
        if key is None:
            raise ValueError(f"unsupported guest command character: {character!r}")
        qmp.execute("human-monitor-command", {
            "command-line": f"sendkey {key}",
        }, deadline)
        time.sleep(smoke.KEY_INTERVAL_SECONDS)
    qmp.execute("human-monitor-command", {
        "command-line": "sendkey ret",
    }, deadline)


def wait_for(process: subprocess.Popen[str], chunks: queue.Queue[str],
             transcript: list[str], finished: threading.Event,
             marker: str, deadline: float, after: int = -1) -> int:
    error, position = smoke.wait_for_line(
        process, chunks, transcript, finished, marker, deadline, after=after
    )
    if error is not None:
        raise RuntimeError(error)
    return position


def run_command(qmp: sata_hotplug.QmpClient,
                process: subprocess.Popen[str], chunks: queue.Queue[str],
                transcript: list[str], finished: threading.Event,
                command: str, deadline: float, position: int,
                marker: str | None = None) -> int:
    inject_command(qmp, command, deadline)
    marker_position = position
    if marker is not None:
        marker_position = wait_for(
            process, chunks, transcript, finished, marker, deadline,
            after=position,
        )
    return wait_for(
        process, chunks, transcript, finished, smoke.SHELL_PROMPT, deadline,
        after=marker_position,
    )


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    checksum = binascii.crc32(kind)
    checksum = binascii.crc32(payload, checksum) & 0xFFFFFFFF
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(
        ">I", checksum
    )


def ppm_to_png(path: Path) -> None:
    data = path.read_bytes()
    if data.startswith(PNG_SIGNATURE):
        return
    if not data.startswith(b"P6"):
        raise RuntimeError(f"unsupported QEMU screenshot format: {path}")

    cursor = 2
    tokens: list[bytes] = []
    while len(tokens) < 3:
        while cursor < len(data) and data[cursor] in b" \t\r\n":
            cursor += 1
        if cursor < len(data) and data[cursor] == ord("#"):
            while cursor < len(data) and data[cursor] not in b"\r\n":
                cursor += 1
            continue
        start = cursor
        while cursor < len(data) and data[cursor] not in b" \t\r\n":
            cursor += 1
        tokens.append(data[start:cursor])
    while cursor < len(data) and data[cursor] in b" \t\r\n":
        cursor += 1

    width, height, maximum = (int(token) for token in tokens)
    pixels = data[cursor:]
    expected = width * height * 3
    if maximum != 255 or width <= 0 or height <= 0 or len(pixels) != expected:
        raise RuntimeError(f"invalid QEMU PPM screenshot: {path}")
    scanlines = b"".join(
        b"\0" + pixels[row * width * 3:(row + 1) * width * 3]
        for row in range(height)
    )
    png = bytearray(PNG_SIGNATURE)
    png.extend(png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height,
                                                   8, 2, 0, 0, 0)))
    png.extend(png_chunk(b"IDAT", zlib.compress(scanlines, level=9)))
    png.extend(png_chunk(b"IEND", b""))
    path.write_bytes(png)


def capture(qmp: sata_hotplug.QmpClient, output: Path,
            deadline: float) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    output.unlink(missing_ok=True)
    time.sleep(0.25)
    qmp.execute("screendump", {"filename": str(output)}, deadline)
    if not output.is_file() or output.stat().st_size < len(PNG_SIGNATURE):
        raise RuntimeError(f"QEMU did not create screenshot {output}")
    ppm_to_png(output)
    if output.read_bytes()[:len(PNG_SIGNATURE)] != PNG_SIGNATURE:
        raise RuntimeError(f"QEMU screenshot is not PNG: {output}")


def capture_text_screens(qemu: Path, image: Path, output_dir: Path,
                         work_dir: Path, timeout: float, log: Path) -> None:
    auxiliary = work_dir / "website-capture-empty.img"
    make_empty_disk(auxiliary)
    qmp_port = reserve_qmp_port()
    process = subprocess.Popen(
        qemu_command(qemu, image, auxiliary, qmp_port),
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        bufsize=0,
    )
    assert process.stdout is not None
    chunks: queue.Queue[str] = queue.Queue()
    transcript: list[str] = []
    finished = threading.Event()
    reader = threading.Thread(
        target=smoke.reader, args=(process.stdout, chunks, finished), daemon=True
    )
    reader.start()
    deadline = time.monotonic() + timeout
    qmp: sata_hotplug.QmpClient | None = None
    try:
        qmp = sata_hotplug.QmpClient(qmp_port, deadline)
        position = wait_for(
            process, chunks, transcript, finished, smoke.SHELL_PROMPT, deadline
        )
        capture(qmp, output_dir / "reist-boot-shell.png", deadline)

        position = run_command(
            qmp, process, chunks, transcript, finished, "drives", deadline,
            position,
        )
        capture(qmp, output_dir / "reist-drives.png", deadline)

        position = run_command(
            qmp, process, chunks, transcript, finished, "cls", deadline,
            position,
        )
        position = run_command(
            qmp, process, chunks, transcript, finished, "sysinfo", deadline,
            position,
        )
        capture(qmp, output_dir / "reist-system-info.png", deadline)

        position = run_command(
            qmp, process, chunks, transcript, finished, "cls", deadline,
            position,
        )
        position = run_command(
            qmp, process, chunks, transcript, finished, "devctl list", deadline,
            position,
        )
        capture(qmp, output_dir / "reist-maintenance.png", deadline)

        position = run_command(
            qmp, process, chunks, transcript, finished, "cls", deadline,
            position,
        )
        position = run_command(
            qmp, process, chunks, transcript, finished, "fdisk", deadline,
            position,
        )
        position = run_command(
            qmp, process, chunks, transcript, finished,
            "fdisk --create 0 12 --confirm", deadline, position,
            marker="fdisk: partition created",
        )
        capture(qmp, output_dir / "reist-fdisk.png", deadline)

        position = run_command(
            qmp, process, chunks, transcript, finished, "cls", deadline,
            position,
        )
        position = run_command(
            qmp, process, chunks, transcript, finished,
            "format --reist-fat32 --quick 4 --confirm", deadline, position,
            marker="FORMAT: REIST FAT32 quick format verified",
        )
        position = run_command(
            qmp, process, chunks, transcript, finished,
            "mount 4 fat32 /mnt/provision", deadline, position,
            marker="ADMIN MOUNT_OK resource=4 path=/mnt/provision",
        )
        capture(qmp, output_dir / "reist-format-fat32.png", deadline)
    finally:
        if qmp is not None:
            qmp.close()
        smoke.stop_process(process)
        finished.wait(timeout=1.0)
        smoke.drain(chunks, transcript)
        reader.join(timeout=1.0)
        log.parent.mkdir(parents=True, exist_ok=True)
        log.write_text("".join(transcript), encoding="utf-8")


def capture_desktop(qemu: Path, image: Path, output_dir: Path,
                    timeout: float, log: Path,
                    surface_probe: bool = False) -> None:
    """Capture the desktop after runtime activation from a VGA shell."""
    filename = ("reist-desktop-apps.png" if surface_probe
                else "reist-desktop.png")
    screenshot = output_dir / filename
    screenshot.parent.mkdir(parents=True, exist_ok=True)
    screenshot.unlink(missing_ok=True)
    status = runtime_desktop.run(
        qemu, image, screenshot, timeout,
        False, False, surface_probe, False,
        False, False, False, False, None, True, True,
    )
    log.parent.mkdir(parents=True, exist_ok=True)
    log.write_text(
        f"runtime desktop capture status={status} "
        f"surface_probe={int(surface_probe)}\n",
        encoding="utf-8",
    )
    if status != 0:
        raise RuntimeError(f"runtime desktop capture failed with status {status}")
    ppm_to_png(screenshot)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument(
        "--output-dir", type=Path,
        default=Path("website/assets"),
    )
    parser.add_argument(
        "--work-dir", type=Path,
        default=Path("build/website-capture"),
    )
    parser.add_argument("--timeout", type=float, default=180.0)
    parser.add_argument(
        "--desktop", action="store_true",
        help="capture reist-desktop.png after activation from a VGA build",
    )
    parser.add_argument(
        "--desktop-apps", action="store_true",
        help="capture reist-desktop-apps.png with windowed Surface clients",
    )
    args = parser.parse_args()
    if not args.qemu.is_file() or not args.image.is_file():
        parser.error("--qemu and --image must name existing files")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")

    output_dir = args.output_dir.resolve()
    work_dir = args.work_dir.resolve()
    work_dir.mkdir(parents=True, exist_ok=True)
    try:
        if args.desktop:
            capture_desktop(
                args.qemu.resolve(), args.image.resolve(), output_dir,
                args.timeout, work_dir / "desktop.log",
            )
        if args.desktop_apps:
            capture_desktop(
                args.qemu.resolve(), args.image.resolve(), output_dir,
                args.timeout, work_dir / "desktop-apps.log", True,
            )
        if not args.desktop and not args.desktop_apps:
            capture_text_screens(
                args.qemu.resolve(), args.image.resolve(), output_dir,
                work_dir, args.timeout, work_dir / "text-mode.log",
            )
    except (ConnectionError, OSError, RuntimeError, TimeoutError,
            ValueError) as error:
        print(f"website capture: FAIL: {error}", file=sys.stderr)
        return 1
    print(f"website capture: PASS: {output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
