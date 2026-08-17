#!/usr/bin/env python3
"""Exercise bounded static component lifecycle control in QEMU."""

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


def monitor_key_commands(text: str) -> list[str]:
    commands: list[str] = []
    for character in text:
        if "a" <= character <= "z" or "0" <= character <= "9":
            key = character
        elif character == " ":
            key = "spc"
        else:
            raise ValueError("component command contains an unsupported key")
        commands.append(f"sendkey {key}\n")
    commands.append("sendkey ret\n")
    return commands


def inject_ps2_command(process: subprocess.Popen[str], text: str) -> None:
    if process.stdin is None:
        raise RuntimeError("QEMU multiplexed monitor input unavailable")
    process.stdin.write(smoke.QEMU_MUX_SWITCH)
    process.stdin.flush()
    time.sleep(smoke.KEY_INTERVAL_SECONDS)
    try:
        for command in monitor_key_commands(text):
            process.stdin.write(command)
            process.stdin.flush()
            time.sleep(smoke.KEY_INTERVAL_SECONDS)
    finally:
        process.stdin.write(smoke.QEMU_MUX_SWITCH)
        process.stdin.flush()


def wait_marker(process: subprocess.Popen[str], chunks: queue.Queue[str],
                transcript: list[str], finished: threading.Event,
                marker: str, deadline: float, after: int = -1
                ) -> tuple[str | None, int]:
    return smoke.wait_for_line(process, chunks, transcript, finished,
                               marker, deadline, after=after)


def send_and_wait(process: subprocess.Popen[str], chunks: queue.Queue[str],
                  transcript: list[str], finished: threading.Event,
                  command: str, marker: str, deadline: float,
                  after: int) -> tuple[str | None, int]:
    inject_ps2_command(process, command)
    error, marker_position = wait_marker(
        process, chunks, transcript, finished, marker, deadline, after)
    if error is not None:
        return error, marker_position
    return wait_marker(process, chunks, transcript, finished,
                       smoke.SHELL_PROMPT, deadline, after=marker_position)


def run(qemu: Path, image: Path, timeout: float, log: Path) -> int:
    command = smoke.qemu_command(qemu, image, nic="e1000")
    process = subprocess.Popen(
        command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, text=True, encoding="utf-8",
        errors="replace", bufsize=0,
    )
    assert process.stdout is not None
    chunks: queue.Queue[str] = queue.Queue()
    transcript: list[str] = []
    finished = threading.Event()
    reader = threading.Thread(target=smoke.reader,
                              args=(process.stdout, chunks, finished),
                              daemon=True)
    reader.start()
    deadline = time.monotonic() + timeout
    error: str | None = None
    position = -1
    try:
        error, position = wait_marker(
            process, chunks, transcript, finished, smoke.SHELL_PROMPT,
            deadline)
        commands = [
            ("svcctl list",
             "COMPONENT RESOURCE 6 name=network-service state=READY generation=1"),
            ("svcctl down 0", "COMPONENT PROTECTED"),
            ("svcctl down 4", "COMPONENT DEPENDENCY_BLOCKED"),
            ("svcctl down 6", "COMPONENT DOWN_OK component=6 generation=2"),
            ("svcctl down 4", "COMPONENT DOWN_OK component=4 generation=2"),
            ("svcctl status 4",
             "COMPONENT STATUS 4 name=network-driver state=DOWN generation=2 FENCED"),
            ("svcctl up 4", "COMPONENT UP_OK component=4 generation=3"),
            ("svcctl up 6", "COMPONENT UP_OK component=6 generation=3"),
            ("svcctl status 6",
             "COMPONENT STATUS 6 name=network-service state=READY generation=3"),
            ("svcctl restart 5",
             "COMPONENT RESTART_OK component=5 generation=3"),
            ("svcctl status 5",
             "COMPONENT STATUS 5 name=storage-service state=READY generation=3"),
            ("storage", "STORAGE SERVICE_BIND_FAILED code=-13"),
        ]
        for user_command, marker in commands:
            if error is not None:
                break
            error, position = send_and_wait(
                process, chunks, transcript, finished, user_command, marker,
                deadline, position)
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
        print(f"COMPONENT CONTROL FAIL: {error}; log={log}")
        return 1
    print(f"COMPONENT CONTROL PASS log={log}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--log", type=Path,
                        default=Path("build/test-results/component-control.log"))
    parser.add_argument("--timeout", type=float, default=90.0)
    args = parser.parse_args()
    if not args.qemu.is_file() or not args.image.is_file() or args.timeout <= 0:
        parser.error("qemu/image must exist and timeout must be positive")
    return run(args.qemu.resolve(), args.image.resolve(), args.timeout,
               args.log.resolve())


if __name__ == "__main__":
    raise SystemExit(main())
