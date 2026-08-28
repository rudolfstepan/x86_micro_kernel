#!/usr/bin/env python3
"""Run the bounded, single-vCPU REIST x86_64 transition proof."""

from __future__ import annotations

import argparse
from pathlib import Path
import queue
import shutil
import subprocess
import sys
import threading
import time


SUCCESS = "REIST_X86_64_RING3_SHELL_OK"
REQUIRED_MARKERS = (
    "REIST_X86_64_LONG_MODE_BOOT_OK",
    "REIST_X86_64_HIGHER_HALF_PAGING_OK",
    "REIST_X86_64_EXCEPTION_IDT_READY",
    "REIST_X86_64_EXCEPTION_UD_OK",
    "REIST_X86_64_PAGING_NX_OK",
    "REIST_X86_64_PHYSICAL_MEMORY_OK",
    "REIST_X86_64_PHYSICAL_MEMORY_128M_OK",
    "REIST_X86_64_ELF64_LOAD_OK",
    "REIST_X86_64_USER_EXECUTION_OK",
    "REIST_X86_64_PROCESS_SCHEDULER_OK",
    "REIST_X86_64_TIMER_IRQ_OK",
    "REIST_X86_64_TIMER_PREEMPTION_OK",
    "REIST_X86_64_QUANTUM_SWITCH_OK",
    "REIST_X86_64_RUNQUEUE_LIFECYCLE_OK",
    "REIST_X86_64_DEADLINE_SLEEP_OK",
    "REIST_X86_64_SPAWN_WAIT_OK",
    "REIST_X86_64_EXCEPTION_RECOVERY_OK",
    "REIST_X86_64_C_CALLBACK_OK",
    "REIST_X86_64_C_CORE_HANDOFF_OK",
    "REIST_X86_64_RING3_SHELL_READY",
    "REIST_X86_64_RING3_SHELL_INFO_OK",
    "REIST_X86_64_RING3_SHELL_EXIT_OK",
    "REIST_X86_64_SCHEDULED_SHELL_OK",
    "REIST_X86_64_C_KERNEL_CONTROL_OK",
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
    "REIST_X86_64_QUANTUM_SWITCH_ERROR",
    "REIST_X86_64_RUNQUEUE_LIFECYCLE_ERROR",
    "REIST_X86_64_DEADLINE_SLEEP_ERROR",
    "REIST_X86_64_SPAWN_WAIT_ERROR",
    "REIST_X86_64_C_CORE_HANDOFF_ERROR",
    "REIST_X86_64_C_KERNEL_CONTROL_ERROR",
    "REIST_X86_64_RING3_SHELL_ERROR",
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
        "-m", "128M",
        "-smp", "1",
        "-display", "none",
        "-monitor", "none",
        "-serial", "stdio",
        "-no-reboot",
        "-no-shutdown",
        "-kernel", str(image.resolve()),
    ]
    process = subprocess.Popen(
        command,
        cwd=image.resolve().parent,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        bufsize=0,
    )
    output_queue: queue.Queue[bytes] = queue.Queue()

    def read_serial() -> None:
        if process.stdout is None:
            return
        while True:
            chunk = process.stdout.read(256)
            if not chunk:
                return
            output_queue.put(chunk)

    reader = threading.Thread(target=read_serial, daemon=True)
    reader.start()
    deadline = time.monotonic() + timeout
    captured_bytes = bytearray()
    info_sent = False
    exit_sent = False
    try:
        while time.monotonic() < deadline:
            try:
                captured_bytes.extend(output_queue.get(timeout=0.02))
            except queue.Empty:
                pass
            while True:
                try:
                    captured_bytes.extend(output_queue.get_nowait())
                except queue.Empty:
                    break
            captured = captured_bytes.decode("ascii", errors="replace")
            if not info_sent and "REIST_X86_64_RING3_SHELL_READY" in captured:
                if process.stdin is None:
                    raise RuntimeError("qemu serial input is unavailable")
                process.stdin.write(b"INFO\n")
                process.stdin.flush()
                info_sent = True
            if info_sent and not exit_sent and \
                    "REIST_X86_64_RING3_SHELL_INFO_OK" in captured:
                if process.stdin is None:
                    raise RuntimeError("qemu serial input closed before EXIT")
                process.stdin.write(b"EXIT\n")
                process.stdin.flush()
                exit_sent = True
            if SUCCESS in captured or any(marker in captured for marker in FAILURES):
                break
            if process.poll() is not None:
                break
    finally:
        if process.stdin is not None:
            try:
                process.stdin.close()
            except OSError:
                pass
        terminate_bounded(process)
        reader.join(timeout=1.0)
        while True:
            try:
                captured_bytes.extend(output_queue.get_nowait())
            except queue.Empty:
                break
        if process.stdout is not None:
            process.stdout.close()
        log.write_bytes(captured_bytes)
    captured = captured_bytes.decode("ascii", errors="replace")
    stderr = b""
    if process.stderr is not None:
        stderr = process.stderr.read(4096)
        process.stderr.close()
    if any(marker in captured for marker in FAILURES):
        raise RuntimeError(f"bootstrap reported failure: {captured.strip()}")
    if not info_sent or not exit_sent:
        raise RuntimeError("bounded Ring-3 shell dialogue did not complete")
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
