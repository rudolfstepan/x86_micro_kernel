#!/usr/bin/env python3
"""Boot REIST and inject a shell command through QEMU's PS/2 keyboard."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import tempfile
import time


KEYBOARD_READY_MARKER = "PS/2 keyboard ready:"
SHELL_PROMPT = "C:\\>"
SHELL_HELP_MARKER = "Built-ins: cd path pwd help exit"
PS2_COMMAND = "help"
LOCK_KEY_COMMANDS = ("sendkey num_lock\n", "sendkey num_lock\n")
POLL_INTERVAL_SECONDS = 0.02
KEY_INTERVAL_SECONDS = 0.05


def monitor_key_commands(text: str) -> list[str]:
    commands: list[str] = []
    for character in text:
        if not ("a" <= character <= "z"):
            raise ValueError("PS/2 smoke command must contain lowercase ASCII")
        commands.append(f"sendkey {character}\n")
    commands.append("sendkey ret\n")
    return commands


def qemu_command(qemu: Path, image: Path, serial_log: Path) -> list[str]:
    return [
        str(qemu),
        "-accel", "tcg",
        "-machine", "pc",
        "-m", "512M",
        "-drive", f"file={image},format=raw,if=ide,snapshot=on",
        "-boot", "c",
        "-display", "none",
        "-serial", f"file:{serial_log}",
        "-monitor", "stdio",
        "-nic", "none",
        "-no-reboot",
        "-no-shutdown",
    ]


def read_transcript(path: Path) -> str:
    try:
        return path.read_bytes().decode("utf-8", errors="replace")
    except FileNotFoundError:
        return ""


def wait_for_marker(
    process: subprocess.Popen[bytes], serial_log: Path, marker: str,
    deadline: float,
) -> tuple[bool, str]:
    transcript = ""
    while time.monotonic() < deadline:
        transcript = read_transcript(serial_log)
        if marker in transcript:
            return True, transcript
        if process.poll() is not None:
            return False, transcript
        time.sleep(POLL_INTERVAL_SECONDS)
    return False, read_transcript(serial_log)


def stop_process(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=2.0)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=2.0)


def run(qemu: Path, image: Path, timeout: float) -> tuple[int, str, str]:
    with tempfile.TemporaryDirectory(prefix="reist-ps2-") as directory:
        serial_log = Path(directory) / "serial.log"
        try:
            process = subprocess.Popen(
                qemu_command(qemu, image, serial_log),
                stdin=subprocess.PIPE,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.STDOUT,
            )
        except OSError as error:
            return 1, "", f"unable to start QEMU: {error}"

        transcript = ""
        error = ""
        try:
            deadline = time.monotonic() + timeout
            ready, transcript = wait_for_marker(
                process, serial_log, SHELL_PROMPT, deadline)
            if not ready:
                error = "shell prompt not reached before deadline"
            elif KEYBOARD_READY_MARKER not in transcript:
                error = "verified PS/2 initialization marker missing"
            elif process.stdin is None:
                error = "QEMU monitor input unavailable"
            else:
                commands = [
                    *LOCK_KEY_COMMANDS,
                    *monitor_key_commands(PS2_COMMAND),
                ]
                for command in commands:
                    process.stdin.write(command.encode("ascii"))
                    process.stdin.flush()
                    time.sleep(KEY_INTERVAL_SECONDS)
                responded, transcript = wait_for_marker(
                    process, serial_log, SHELL_HELP_MARKER, deadline)
                if not responded:
                    error = "userspace shell did not receive PS/2 help command"
        finally:
            stop_process(process)
            transcript = read_transcript(serial_log)
        return (0 if not error else 1), transcript, error


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", type=Path, default=Path("qemu-system-i386"))
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=90.0)
    parser.add_argument("--log", type=Path)
    args = parser.parse_args()

    if not args.image.is_file() or args.timeout <= 0:
        print("ps2-smoke: invalid image or timeout")
        return 2
    status, transcript, error = run(
        args.qemu, args.image.resolve(), args.timeout)
    if args.log is not None:
        args.log.parent.mkdir(parents=True, exist_ok=True)
        args.log.write_text(transcript, encoding="utf-8")
    if error:
        print(f"ps2-smoke: FAIL: {error}")
        return status
    print("ps2-smoke: PASS (NumLock and text reached userspace shell)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
