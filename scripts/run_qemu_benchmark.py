#!/usr/bin/env python3
"""Run BENCHMARK.PRG once with a hard QEMU deadline and verify HDD cleanup."""

from __future__ import annotations

import argparse
import os
import queue
import re
import shutil
import subprocess
import sys
import threading
import time
import tempfile
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_qemu_smoke as smoke


START_MARKER = "REIST Benchmark: begrenzte Diagnose laeuft ..."
TABLE_MARKER = "REIST OS System Benchmark"
DONE_MARKER = "BENCHMARK_STATUS phase=complete"
EXPECTED_STATUS = (
    "BENCHMARK_STATUS phase=cpu",
    "BENCHMARK_STATUS phase=ram-write",
    "BENCHMARK_STATUS phase=ram-read",
    "BENCHMARK_STATUS phase=hdd-create",
    "BENCHMARK_STATUS phase=hdd-write progress_kib=0 total_kib=256",
    "BENCHMARK_STATUS phase=hdd-write progress_kib=64 total_kib=256",
    "BENCHMARK_STATUS phase=hdd-write progress_kib=128 total_kib=256",
    "BENCHMARK_STATUS phase=hdd-write progress_kib=192 total_kib=256",
    "BENCHMARK_STATUS phase=hdd-write progress_kib=256 total_kib=256",
    "BENCHMARK_STATUS phase=hdd-fsync",
    "BENCHMARK_STATUS phase=hdd-read progress_kib=0 total_kib=256",
    "BENCHMARK_STATUS phase=hdd-read progress_kib=64 total_kib=256",
    "BENCHMARK_STATUS phase=hdd-read progress_kib=128 total_kib=256",
    "BENCHMARK_STATUS phase=hdd-read progress_kib=192 total_kib=256",
    "BENCHMARK_STATUS phase=hdd-read progress_kib=256 total_kib=256",
    "BENCHMARK_STATUS phase=hdd-cleanup state=begin",
    "BENCHMARK_STATUS phase=hdd-cleanup state=complete",
    "BENCHMARK_STATUS phase=vga",
)
STATUS_PATTERN = re.compile(r"^BENCHMARK_STATUS[^\r\n]*$", re.MULTILINE)
ROW_PATTERN = re.compile(
    r"\|\s*HDD\s*\|\s*(Seq\. (?:Schreiben|Lesen))\s*\|"
    r"\s*([^|]+?)\s*\|\s*([^|]+?)\s*\|"
)


def resolve_qemu(requested: Path) -> Path:
    if requested != Path("qemu-system-i386"):
        if requested.is_file():
            return requested.resolve()
        raise FileNotFoundError(f"QEMU executable not found: {requested}")
    found = shutil.which("qemu-system-i386")
    if found is not None:
        return Path(found).resolve()
    for candidate in (
        Path(r"C:\tmp\qemu-portable\qemu-system-i386.exe"),
        Path(r"C:\Program Files\qemu\qemu-system-i386.exe"),
        Path(r"C:\msys64\mingw64\bin\qemu-system-i386.exe"),
    ):
        if candidate.is_file():
            return candidate.resolve()
    raise FileNotFoundError("qemu-system-i386 was not found")


def latest_status(text: str) -> str:
    matches = list(STATUS_PATTERN.finditer(text))
    return matches[-1].group(0).rstrip("\r") if matches else "none"


def run(qemu: Path, image: Path, timeout: float, log: Path, smp: int = 1) -> int:
    log.parent.mkdir(parents=True, exist_ok=True)
    descriptor, clone_name = tempfile.mkstemp(
        prefix="reist-benchmark-", suffix=".img", dir=log.parent
    )
    os.close(descriptor)
    clone = Path(clone_name)
    try:
        shutil.copyfile(image, clone)
        command = smoke.qemu_command(
            qemu, clone, memory="256M", nic="none", persistent=True, smp=smp
        )
        command.extend(["-vga", "std"])
        process = subprocess.Popen(
            command,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            bufsize=0,
        )
    except BaseException:
        clone.unlink(missing_ok=True)
        raise
    assert process.stdout is not None
    chunks: queue.Queue[str] = queue.Queue()
    transcript: list[str] = []
    finished = threading.Event()
    reader = threading.Thread(
        target=smoke.reader,
        args=(process.stdout, chunks, finished),
        daemon=True,
    )
    reader.start()

    error: str | None = None
    benchmark_ms = 0
    deadline = time.monotonic() + timeout
    try:
        error, position = smoke.wait_for_line(
            process, chunks, transcript, finished, smoke.SHELL_PROMPT, deadline
        )
        if error is None:
            started = time.monotonic()
            smoke.inject_ps2_command(process, "benchmark")
            error, position = smoke.wait_for_line(
                process, chunks, transcript, finished, START_MARKER, deadline,
                after=position,
            )
        for marker in EXPECTED_STATUS:
            if error is not None:
                break
            error, position = smoke.wait_for_line(
                process, chunks, transcript, finished, marker, deadline,
                after=position,
            )
        if error is None:
            error, position = smoke.wait_for_line(
                process, chunks, transcript, finished, TABLE_MARKER, deadline,
                after=position,
            )
        if error is None:
            error, position = smoke.wait_for_line(
                process, chunks, transcript, finished, DONE_MARKER, deadline,
                after=position,
            )
        if error is None:
            error, _ = smoke.wait_for_line(
                process, chunks, transcript, finished, smoke.SHELL_PROMPT,
                deadline, after=position,
            )
            benchmark_ms = round((time.monotonic() - started) * 1000)
    except (OSError, RuntimeError, TimeoutError, ValueError) as caught:
        error = str(caught)
    finally:
        smoke.stop_process(process)
        finished.wait(timeout=1.0)
        smoke.drain(chunks, transcript)
        reader.join(timeout=1.0)
        text = "".join(transcript)
        log.write_text(text, encoding="utf-8")
        clone.unlink(missing_ok=True)

    rows = {
        match.group(1): (match.group(2).strip(), match.group(3).strip())
        for match in ROW_PATTERN.finditer(text)
    }
    for name in ("Seq. Schreiben", "Seq. Lesen"):
        if error is None and name not in rows:
            error = f"missing HDD result row: {name}"
        elif error is None and (rows[name][0] == "-" or rows[name][1] != "OK"):
            error = (
                f"HDD {name} failed: value={rows[name][0]} "
                f"status={rows[name][1]}"
            )

    if error is not None:
        print(
            "BENCHMARK_RUNTIME_FAIL "
            f"reason={error} last_status='{latest_status(text)}' log={log}"
        )
        return 1
    print(
        "BENCHMARK_RUNTIME_PASS "
        f"elapsed_ms={benchmark_ms} "
        f"write='{rows['Seq. Schreiben'][0]}' "
        f"read='{rows['Seq. Lesen'][0]}' cleanup=ok log={log}"
    )
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", type=Path, default=Path("qemu-system-i386"))
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=120.0)
    parser.add_argument("--smp", type=int, default=1)
    parser.add_argument(
        "--log", type=Path,
        default=Path("build/codex-agent/benchmark-runtime.log"),
    )
    args = parser.parse_args()
    if not args.image.is_file() or args.timeout <= 0:
        parser.error("image must exist and timeout must be positive")
    if args.smp < 1 or args.smp > 16:
        parser.error("smp must be in 1..16")
    try:
        qemu = resolve_qemu(args.qemu)
    except FileNotFoundError as error:
        parser.error(str(error))
    return run(qemu, args.image.resolve(), args.timeout, args.log.resolve(),
               args.smp)


if __name__ == "__main__":
    raise SystemExit(main())
