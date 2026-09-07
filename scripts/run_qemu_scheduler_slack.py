#!/usr/bin/env python3
"""Bounded real Ring-3 workload, wake/yield, exception/reap and shell proof."""
import argparse
import queue
import re
import subprocess
import threading
import time
from pathlib import Path

import run_qemu_smoke as smoke
from run_qemu_system_layout import inject, send_and_wait


def validate_metrics(transcript):
    samples = re.findall(r"(?m)^SCHED_SLACK_METRIC adjacent_ms=(\d{4})\r?$",
                         transcript)
    if len(samples) != 2 or any(not 400 <= int(x) <= 1000 for x in samples):
        raise ValueError(f"expected two 1000-ms workloads with >=400 adjacent ticks: {samples}")
    return [int(x) for x in samples]


def run(args):
    started = time.monotonic()
    deadline = started + 180.0
    process = subprocess.Popen(
        smoke.qemu_command(args.qemu, args.image, no_apic=args.no_apic,
                           memory="1024M", nic="e1000", smp=args.smp),
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, encoding="utf-8", errors="replace", bufsize=0,
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
    )
    chunks = queue.Queue()
    transcript = []
    finished = threading.Event()
    reader = threading.Thread(target=smoke.reader,
                              args=(process.stdout, chunks, finished), daemon=True)
    reader.start()
    error = None
    try:
        error, position = smoke.wait_for_line(
            process, chunks, transcript, finished, smoke.SHELL_PROMPT, deadline)
        for _ in range(2):
            if error:
                break
            inject(process, "/libexec/reist/gtest.prg sched-slack")
            for marker in ("SCHED_SLACK_BEGIN", "*** USER PROCESS EXCEPTION ***",
                           "SCHED_SLACK_OK", smoke.SHELL_PROMPT):
                error, position = smoke.wait_for_line(
                    process, chunks, transcript, finished, marker, deadline,
                    after=position)
                if error:
                    break
            if error:
                break
            error, position = send_and_wait(
                process, chunks, transcript, finished, "path",
                "PATH=C:\\bin;C:\\sbin;C:\\usr\\bin;C:\\usr\\gui\\bin",
                deadline, position)
        if not error:
            metrics = validate_metrics("".join(transcript))
            print(f"SCHEDULER_SLACK metrics={metrics}")
    except (OSError, ValueError, RuntimeError, TimeoutError) as caught:
        error = str(caught)
    finally:
        smoke.stop_process(process)
        finished.wait(timeout=1)
        smoke.drain(chunks, transcript)
        reader.join(timeout=1)
        args.log.parent.mkdir(parents=True, exist_ok=True)
        args.log.write_text("".join(transcript), encoding="utf-8")
    print(f"SCHEDULER_SLACK {'FAIL: ' + error if error else 'PASS'} "
          f"elapsed={time.monotonic() - started:.3f}s log={args.log}")
    return 1 if error else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--no-apic", action="store_true")
    parser.add_argument("--smp", type=int, choices=(1, 2), default=1)
    return run(parser.parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
