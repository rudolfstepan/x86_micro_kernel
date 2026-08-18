#!/usr/bin/env python3
"""Verify the userspace LAN commands against QEMU's RTL8139 NIC."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import tempfile
import time


PROMPT = "C:\\>"
POLL_SECONDS = 0.02
KEY_SECONDS = 0.04


def read_log(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except FileNotFoundError:
        return ""


def wait_for(path: Path, marker: str, deadline: float) -> str | None:
    while time.monotonic() < deadline:
        text = read_log(path)
        if marker in text:
            return text
        time.sleep(POLL_SECONDS)
    return None


def wait_for_after(path: Path, marker: str, offset: int,
                   deadline: float) -> int | None:
    while time.monotonic() < deadline:
        position = read_log(path).find(marker, offset)
        if position >= 0:
            return position
        time.sleep(POLL_SECONDS)
    return None


def monitor_key_commands(text: str) -> list[str]:
    commands: list[str] = []
    for character in text:
        if "a" <= character <= "z" or "0" <= character <= "9":
            key = character
        elif character == ".":
            key = "dot"
        elif character == " ":
            key = "spc"
        else:
            raise ValueError(f"unsupported PS/2 character: {character!r}")
        commands.append(f"sendkey {key}\n")
    commands.append("sendkey ret\n")
    return commands


def send_command(process: subprocess.Popen[bytes], text: str) -> None:
    if process.stdin is None:
        raise RuntimeError("QEMU monitor input is unavailable")
    for command in monitor_key_commands(text):
        process.stdin.write(command.encode("ascii"))
        process.stdin.flush()
        time.sleep(KEY_SECONDS)


def send_key(process: subprocess.Popen[bytes], key: str) -> None:
    if process.stdin is None:
        raise RuntimeError("QEMU monitor input is unavailable")
    process.stdin.write(f"sendkey {key}\n".encode("ascii"))
    process.stdin.flush()
    time.sleep(KEY_SECONDS)


def qemu_command(qemu: Path, image: Path, serial: Path) -> list[str]:
    return [
        str(qemu), "-accel", "tcg", "-machine", "pc", "-m", "512M",
        "-drive", f"file={image},format=raw,if=ide,snapshot=on",
        "-boot", "c", "-display", "none", "-serial", f"file:{serial}",
        "-monitor", "stdio", "-nic", "user,model=rtl8139",
        "-no-reboot", "-no-shutdown",
    ]


def stop(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=3.0)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=3.0)


def run(qemu: Path, image: Path, log: Path, timeout: float) -> int:
    log.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="reist-network-shell-") as folder:
        serial = Path(folder) / "serial.log"
        process = subprocess.Popen(
            qemu_command(qemu, image, serial),
            stdin=subprocess.PIPE,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.STDOUT,
        )
        try:
            deadline = time.monotonic() + timeout
            if wait_for(serial, PROMPT, deadline) is None:
                print("network-shell: FAIL: shell prompt not reached")
                return 1

            commands = (
                ("net status", "backend=RTL8139"),
                ("getip", "IP=10.0.2.15"),
            )
            for command, marker in commands:
                before = read_log(serial)
                send_command(process, command)
                if wait_for(serial, marker, deadline) is None:
                    print(f"network-shell: FAIL: {command!r} -> {marker!r}")
                    return 1
                if read_log(serial) == before:
                    print(f"network-shell: FAIL: no response for {command!r}")
                    return 1

            for target in ("10.0.2.15", "10.0.2.2"):
                start = len(read_log(serial))
                send_command(process, f"ping {target}")
                reply = wait_for_after(serial, "reply: received", start,
                                       deadline)
                if reply is None:
                    print(f"network-shell: FAIL: ping {target} received no echo reply")
                    return 1
                send_key(process, "ctrl-c")
                if wait_for_after(serial, PROMPT, reply, deadline) is None:
                    print(f"network-shell: FAIL: ping {target} did not return to shell")
                    return 1
            print("network-shell: PASS (net status, getip, self-ping and gateway ping)")
            return 0
        finally:
            final = read_log(serial)
            log.write_text(final, encoding="utf-8")
            stop(process)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=90.0)
    args = parser.parse_args()
    if not args.qemu.is_file() or not args.image.is_file() or args.timeout <= 0:
        print("network-shell: invalid qemu, image or timeout")
        return 2
    return run(args.qemu, args.image, args.log, args.timeout)


if __name__ == "__main__":
    raise SystemExit(main())
