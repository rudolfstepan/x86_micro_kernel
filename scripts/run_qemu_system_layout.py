#!/usr/bin/env python3
"""Verify the lowercase hierarchical system-program layout in QEMU."""

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


def monitor_key_commands(value: str) -> list[str]:
    special = {" ": "spc", "/": "slash", ".": "dot", "-": "minus"}
    commands = []
    for character in value:
        if "a" <= character <= "z" or "0" <= character <= "9":
            key = character
        elif character in special:
            key = special[character]
        else:
            raise ValueError("layout command contains an unsupported key")
        commands.append(f"sendkey {key}\n")
    commands.append("sendkey ret\n")
    return commands


def inject(process: subprocess.Popen[str], command: str) -> None:
    if process.stdin is None:
        raise RuntimeError("QEMU monitor input unavailable")
    process.stdin.write(smoke.QEMU_MUX_SWITCH)
    process.stdin.flush()
    time.sleep(smoke.KEY_INTERVAL_SECONDS)
    try:
        for key_command in monitor_key_commands(command):
            process.stdin.write(key_command)
            process.stdin.flush()
            time.sleep(smoke.KEY_INTERVAL_SECONDS)
    finally:
        process.stdin.write(smoke.QEMU_MUX_SWITCH)
        process.stdin.flush()


def send_and_wait(process, chunks, transcript, finished, command, marker,
                  deadline, after):
    inject(process, command)
    error, position = smoke.wait_for_line(
        process, chunks, transcript, finished, marker, deadline, after=after
    )
    if error is not None:
        return error, position
    return smoke.wait_for_line(
        process, chunks, transcript, finished, smoke.SHELL_PROMPT, deadline,
        after=position,
    )


def run(qemu: Path, image: Path, timeout: float, log: Path) -> int:
    process = subprocess.Popen(
        smoke.qemu_command(qemu, image, nic="e1000"),
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
        target=smoke.reader, args=(process.stdout, chunks, finished), daemon=True
    )
    reader.start()
    deadline = time.monotonic() + timeout
    error = None
    position = -1
    try:
        error, position = smoke.wait_for_line(
            process, chunks, transcript, finished, smoke.SHELL_PROMPT, deadline
        )
        commands = [
            ("path", "PATH=C:\\bin;C:\\sbin;C:\\usr\\bin;C:\\usr\\gui\\bin"),
            ("ls -1 /", "bin"),
            ("ls -1 /bin", "shell.prg"),
            ("ls -1 /sbin", "svcctl.prg"),
            ("ls -1 /usr/bin", "hello.prg"),
            ("ls -1 /usr/gui/bin", "desktop.prg"),
            ("guidemo --help", "Usage: guidemo"),
            ("ls -1 /libexec/reist", "storage.prg"),
            ("svcctl status 5",
             "COMPONENT STATUS 5 name=storage-service state=READY generation=1"),
            ("/svcctl.prg status 5",
             "COMPONENT STATUS 5 name=storage-service state=READY generation=1"),
            ("storage", "STORAGE SERVICE_BIND_FAILED code=-13"),
        ]
        for command, marker in commands:
            if error is not None:
                break
            error, position = send_and_wait(
                process, chunks, transcript, finished, command, marker,
                deadline, position,
            )
    except (OSError, RuntimeError, TimeoutError, ValueError) as caught:
        error = str(caught)
    finally:
        smoke.stop_process(process)
        finished.wait(timeout=1.0)
        smoke.drain(chunks, transcript)
        reader.join(timeout=1.0)
        log.parent.mkdir(parents=True, exist_ok=True)
        log.write_text("".join(transcript), encoding="utf-8")
    if error is not None:
        print(f"SYSTEM LAYOUT FAIL: {error}; log={log}")
        return 1
    print(f"SYSTEM LAYOUT PASS log={log}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=90.0)
    parser.add_argument(
        "--log", type=Path, default=Path("build/test-results/system-layout.log")
    )
    args = parser.parse_args()
    if not args.qemu.is_file() or not args.image.is_file() or args.timeout <= 0:
        parser.error("qemu/image must exist and timeout must be positive")
    return run(args.qemu.resolve(), args.image.resolve(), args.timeout,
               args.log.resolve())


if __name__ == "__main__":
    raise SystemExit(main())
