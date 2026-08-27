#!/usr/bin/env python3
"""Prove bounded Ring-3 driver recovery and degradation in QEMU."""

from __future__ import annotations

import argparse
import queue
import subprocess
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_qemu_smoke as smoke


DRIVER_DOMAIN_MARKERS = (
    smoke.BOOT_MARKER,
    "DRIVER_DOMAIN TEST_STARTED",
    "DRIVER_DOMAIN CRASH_RECOVERED",
    "DRIVER_DOMAIN HANG_RECOVERED",
    "DRIVER_DOMAIN STALE_GENERATION_REJECTED",
    "DRIVER_DOMAIN RESTART_BUDGET_EXHAUSTED",
    "DRIVER_DOMAIN RESET_FAILURE_FENCED",
)


def run(qemu: Path, image: Path, timeout: float, log: Path) -> int:
    process = subprocess.Popen(
        smoke.qemu_command(qemu, image, smp=4),
        stdin=subprocess.PIPE,
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
        target=smoke.reader, args=(process.stdout, chunks, finished),
        daemon=True,
    )
    reader.start()
    deadline = time.monotonic() + timeout
    error: str | None = None
    try:
        for marker in DRIVER_DOMAIN_MARKERS:
            error, _ = smoke.wait_for_line(
                process, chunks, transcript, finished, marker, deadline)
            if error is not None:
                break
        if error is None and smoke.SHELL_PROMPT not in "".join(transcript):
            error, _ = smoke.wait_for_line(
                process, chunks, transcript, finished, smoke.SHELL_PROMPT,
                deadline)
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
        print(f"DRIVER DOMAIN FAIL: {error}; log={log}")
        return 1
    ap_executions = "".join(transcript).count("DRIVER_DOMAIN AP_EXEC cpu=")
    if ap_executions < 2:
        print("DRIVER DOMAIN FAIL: AP domain did not execute both initial and "
              f"restart generations; executions={ap_executions}; log={log}")
        return 1
    print(f"DRIVER DOMAIN PASS log={log}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument(
        "--log", type=Path,
        default=Path("build/test-results/driver-domain.log"),
    )
    parser.add_argument("--timeout", type=float, default=90.0)
    args = parser.parse_args()
    if not args.qemu.is_file() or not args.image.is_file() or args.timeout <= 0:
        parser.error("qemu/image must exist and timeout must be positive")
    return run(args.qemu.resolve(), args.image.resolve(), args.timeout,
               args.log.resolve())


if __name__ == "__main__":
    raise SystemExit(main())
