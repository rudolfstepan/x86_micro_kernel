#!/usr/bin/env python3
"""Bounded headless Ring-3 C++ lifetime/reap proof through the normal shell."""
from __future__ import annotations
import argparse
from pathlib import Path
import queue
import subprocess
import threading
import time

from run_qemu_smoke import (qemu_command, configure_qemu_host_timers,
                            inject_ps2_command, stop_process, SHELL_PROMPT,
                            failure_marker)

MARKERS = ("REIST_CPP_LIFETIME_OK", "REIST_CPP_BACKING_RETURN_OK",
           "REIST_CPP_TYPES_OK", "REIST_CPP_HANDLE_OWNERSHIP_OK",
           "REIST_CPP_REAP_OK mode=--oom", "REIST_CPP_REAP_OK mode=--fault",
           "REIST_CPP_REAP_OK mode=--hold", "REIST_CPP_REAP_OK mode=--normal",
           "REIST_CPP_RUNTIME_OK")
MAX_TRANSCRIPT = 4 * 1024 * 1024


def run(qemu: Path, image: Path, log: Path, timeout: float = 180) -> int:
    chunks: queue.Queue[str] = queue.Queue(65536)
    stopped = threading.Event()
    overflow = threading.Event()
    transcript = ""
    process = subprocess.Popen(qemu_command(qemu, image, vmware_vga=True),
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, encoding="utf-8", errors="replace", bufsize=0,
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0) |
                      getattr(subprocess, "BELOW_NORMAL_PRIORITY_CLASS", 0))

    def reader():
        while not stopped.is_set():
            char = process.stdout.read(1)
            if not char:
                return
            try:
                chunks.put(char, timeout=1)
            except queue.Full:
                overflow.set()
                return

    thread = threading.Thread(target=reader, daemon=True)
    thread.start()
    deadline = time.monotonic() + timeout

    def drain():
        nonlocal transcript
        batch = []
        for _ in range(65536):
            try:
                batch.append(chunks.get_nowait())
            except queue.Empty:
                break
        if len(transcript) + len(batch) > MAX_TRANSCRIPT:
            raise RuntimeError("C++ proof transcript capacity exceeded")
        transcript += "".join(batch)

    def wait(marker: str, after: int) -> int:
        while time.monotonic() < deadline:
            drain()
            if overflow.is_set() or failure_marker(transcript) or "REIST_CPP_RUNTIME_FAIL" in transcript:
                raise RuntimeError("C++ guest reported failure or output overflow")
            position = transcript.find(marker, after)
            if position >= 0:
                return position + len(marker)
            if process.poll() is not None:
                raise RuntimeError("QEMU exited before " + marker)
            stopped.wait(0.01)
        raise TimeoutError("C++ proof deadline before " + marker)

    error = None
    try:
        configure_qemu_host_timers(process)
        position = wait(SHELL_PROMPT, 0)
        inject_ps2_command(process, "cpptest")
        for marker in MARKERS:
            position = wait(marker + "\n", position)
        position = wait(SHELL_PROMPT, position)
        inject_ps2_command(process, "help")
        position = wait("Built-ins: cd path pwd history help exit", position)
        wait(SHELL_PROMPT, position)
    except (OSError, RuntimeError, TimeoutError, ValueError) as caught:
        error = str(caught)
    finally:
        stopped.set()
        stop_process(process)
        thread.join(timeout=2)
        try:
            drain()
        except RuntimeError as caught:
            error = str(caught)
        for stream in (process.stdin, process.stdout):
            if stream is not None:
                stream.close()
        log.parent.mkdir(parents=True, exist_ok=True)
        log.write_text(transcript, encoding="utf-8")
    if error:
        print(f"CPP CLIENT FAIL: {error}; log={log}")
        return 1
    print(f"CPP CLIENT PASS: C++ types/handle ownership, lifetime, backing return, OOM/fault/kill reap, fresh child, shell; log={log}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=180)
    args = parser.parse_args()
    if not args.qemu.is_file() or not args.image.is_file() or not 0 < args.timeout <= 180:
        parser.error("existing qemu/image and 0 < timeout <= 180 required")
    return run(args.qemu, args.image, args.log, args.timeout)


if __name__ == "__main__":
    raise SystemExit(main())
