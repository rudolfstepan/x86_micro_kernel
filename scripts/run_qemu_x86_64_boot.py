#!/usr/bin/env python3
"""Run the bounded, single-vCPU REIST x86_64 transition proof."""

from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import time


SUCCESS = "REIST_X86_64_EXCEPTION_RECOVERY_OK"
REQUIRED_MARKERS = (
    "REIST_X86_64_LONG_MODE_BOOT_OK",
    "REIST_X86_64_HIGHER_HALF_PAGING_OK",
    "REIST_X86_64_EXCEPTION_IDT_READY",
    "REIST_X86_64_EXCEPTION_UD_OK",
    "REIST_X86_64_PAGING_NX_OK",
    "REIST_X86_64_PHYSICAL_MEMORY_OK",
    "REIST_X86_64_ELF64_LOAD_OK",
    "REIST_X86_64_USER_EXECUTION_OK",
    "REIST_X86_64_PROCESS_SCHEDULER_OK",
    "REIST_X86_64_TIMER_IRQ_OK",
    "REIST_X86_64_TIMER_PREEMPTION_OK",
    SUCCESS,
)
FAILURES = (
    "REIST_X86_64_UNSUPPORTED",
    "REIST_X86_64_LONG_MODE_STATE_ERROR",
    "REIST_X86_64_HIGHER_HALF_STATE_ERROR",
    "REIST_X86_64_MEMORY_MAP_ERROR",
    "REIST_X86_64_PHYSICAL_MEMORY_ERROR",
    "REIST_X86_64_ELF64_LOAD_ERROR",
    "REIST_X86_64_USER_EXECUTION_ERROR",
    "REIST_X86_64_PROCESS_SCHEDULER_ERROR",
    "REIST_X86_64_TIMER_IRQ_ERROR",
    "REIST_X86_64_TIMER_PREEMPTION_ERROR",
    "REIST_X86_64_EXCEPTION_FATAL",
)
QEMU_FALLBACKS = (
    Path(r"C:\tmp\qemu-portable\qemu-system-x86_64.exe"),
    Path(r"C:\Program Files\qemu\qemu-system-x86_64.exe"),
    Path(r"C:\msys64\mingw64\bin\qemu-system-x86_64.exe"),
)


def resolve_qemu(explicit: Path | None) -> Path:
    if explicit is not None:
        if explicit.is_file():
            return explicit.resolve()
        found = shutil.which(str(explicit))
        if found:
            return Path(found).resolve()
        raise FileNotFoundError(f"qemu-system-x86_64 not found: {explicit}")
    found = shutil.which("qemu-system-x86_64")
    if found:
        return Path(found).resolve()
    for candidate in QEMU_FALLBACKS:
        if candidate.is_file():
            return candidate.resolve()
    raise FileNotFoundError("qemu-system-x86_64 was not found")


def terminate_bounded(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=2.0)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=2.0)


def run_boot(qemu: Path, image: Path, log: Path, timeout: float) -> str:
    log.parent.mkdir(parents=True, exist_ok=True)
    if log.exists():
        log.unlink()
    command = [
        str(qemu),
        "-machine", "pc,accel=tcg",
        "-cpu", "qemu64",
        "-m", "32M",
        "-smp", "1",
        "-display", "none",
        "-monitor", "none",
        "-serial", f"file:{log.resolve()}",
        "-no-reboot",
        "-no-shutdown",
        "-kernel", str(image.resolve()),
    ]
    process = subprocess.Popen(
        command,
        cwd=image.resolve().parent,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
    )
    deadline = time.monotonic() + timeout
    captured = ""
    try:
        while time.monotonic() < deadline:
            if log.exists():
                captured = log.read_text(encoding="ascii", errors="replace")
                if SUCCESS in captured or any(marker in captured for marker in FAILURES):
                    break
            if process.poll() is not None:
                break
            time.sleep(0.02)
    finally:
        terminate_bounded(process)
    if log.exists():
        captured = log.read_text(encoding="ascii", errors="replace")
    stderr = b""
    if process.stderr is not None:
        stderr = process.stderr.read(4096)
        process.stderr.close()
    if any(marker in captured for marker in FAILURES):
        raise RuntimeError(f"bootstrap reported failure: {captured.strip()}")
    positions = [captured.find(marker) for marker in REQUIRED_MARKERS]
    if any(position < 0 for position in positions):
        detail = stderr.decode("utf-8", errors="replace").strip()
        raise RuntimeError(f"required marker missing; qemu={detail or 'no diagnostic'}")
    if positions != sorted(positions) or len(set(positions)) != len(positions):
        raise RuntimeError("x86_64 paging and exception markers are out of order")
    return captured


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--qemu", type=Path)
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--log", type=Path, required=True)
    args = parser.parse_args()
    if not args.image.is_file():
        parser.error(f"bootstrap image does not exist: {args.image}")
    if not (1.0 <= args.timeout <= 10.0):
        parser.error("timeout must be within 1..10 seconds")
    try:
        qemu = resolve_qemu(args.qemu)
        run_boot(qemu, args.image, args.log, args.timeout)
    except (FileNotFoundError, OSError, RuntimeError) as exc:
        print(f"X86_64_BOOTSTRAP_RUNTIME_FAIL {exc}", file=sys.stderr)
        return 1
    print(f"X86_64_BOOTSTRAP_RUNTIME_OK marker={SUCCESS}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
