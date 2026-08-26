#!/usr/bin/env python3
"""Boot the native image with an isolated OHCI controller and USB boot
keyboard, then confirm bounded OHCI enumeration.

This validates the OHCI programming path.  QEMU's isolated ``pci-ohci`` device
does not model the EHCI-companion routing found on AMD southbridges; that
separate bounded mechanism is covered by source/host tests and remains subject
to final hardware acceptance.
"""
from __future__ import annotations

import argparse
import queue
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path


READY_MARKER = "OHCI keyboard ready"
MOUSE_READY_MARKER = "OHCI mouse ready"
OPERATIONAL_MARKER = "OHCI operational"
FAILURE_MARKERS = (
    "OHCI probe failed",
    "OHCI enumeration failed",
    "OHCI reset/start failed",
    "OHCI SMM ownership handoff failed",
    "OHCI MMIO mapping failed",
    "OHCI reset failed",
    "OHCI port",  # "OHCI port N reset failed"
)


def resolve_qemu(explicit: Path | None) -> Path:
    if explicit is not None and explicit.exists():
        return explicit
    found = shutil.which("qemu-system-i386")
    if found:
        return Path(found)
    for candidate in (
        Path(r"C:\tmp\qemu-portable\qemu-system-i386.exe"),
        Path(r"C:\Program Files\qemu\qemu-system-i386.exe"),
        Path(r"C:\msys64\mingw64\bin\qemu-system-i386.exe"),
    ):
        if candidate.exists():
            return candidate
    raise FileNotFoundError("qemu-system-i386 was not found")


def qemu_command(qemu: Path, image: Path, memory: str) -> list[str]:
    return [
        str(qemu),
        "-accel", "tcg",
        "-machine", "pc",
        "-nodefaults",
        "-m", memory,
        "-boot", "c",
        "-drive", f"file={image},format=raw,if=ide,index=0,media=disk",
        "-snapshot",
        "-display", "none",
        "-monitor", "none",
        "-serial", "stdio",
        "-vga", "std",
        "-no-reboot",
        "-no-shutdown",
        "-device", "pci-ohci,id=ohci",
        "-device", "usb-kbd,bus=ohci.0",
        "-device", "usb-mouse,bus=ohci.0",
    ]


def reader(stream, chunks: "queue.Queue[str]", finished: threading.Event) -> None:
    try:
        while True:
            chunk = stream.read(1)
            if not chunk:
                return
            chunks.put(chunk)
    finally:
        finished.set()


def run(qemu: Path, image: Path, memory: str, timeout: float,
        log: Path | None) -> str | None:
    command = qemu_command(qemu, image, memory)
    process = subprocess.Popen(
        command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, bufsize=1)
    chunks: "queue.Queue[str]" = queue.Queue()
    finished = threading.Event()
    thread = threading.Thread(
        target=reader, args=(process.stdout, chunks, finished), daemon=True)
    thread.start()

    transcript: list[str] = []
    result: str | None = "timeout before OHCI keyboard enumeration"
    deadline = time.monotonic() + timeout
    try:
        while time.monotonic() < deadline:
            try:
                transcript.append(chunks.get(timeout=0.1))
            except queue.Empty:
                if process.poll() is not None:
                    result = f"QEMU exited early with status {process.returncode}"
                    break
                continue
            text = "".join(transcript)
            if READY_MARKER in text and MOUSE_READY_MARKER in text:
                result = None
                break
            failed = next((m for m in FAILURE_MARKERS if m in text), None)
            if failed is not None:
                result = f"guest emitted failure marker {failed!r}"
                break
    finally:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
        finished.wait(timeout=1)
        while True:
            try:
                transcript.append(chunks.get_nowait())
            except queue.Empty:
                break

    if log is not None:
        log.parent.mkdir(parents=True, exist_ok=True)
        log.write_text("".join(transcript), encoding="utf-8", errors="replace")
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", type=Path, default=None)
    parser.add_argument("--image", type=Path, default=Path("build/reist-os.img"))
    parser.add_argument("--memory", default="256")
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--log", type=Path,
                        default=Path("build/codex-agent/ohci-smoke.log"))
    args = parser.parse_args()

    if not args.image.exists():
        print(f"ohci-smoke: FAIL image {args.image} not found")
        return 1
    try:
        qemu = resolve_qemu(args.qemu)
    except FileNotFoundError as error:
        print(f"ohci-smoke: FAIL {error}")
        return 1

    error = run(qemu, args.image, args.memory, args.timeout, args.log)
    if error is None:
        print("ohci-smoke: PASS (keyboard+mouse ready) "
              f"log={args.log}")
        return 0
    print(f"ohci-smoke: FAIL {error} log={args.log}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
