"""Prove desktop.prg activates native graphics after a VGA text boot."""

from __future__ import annotations

import argparse
import pathlib
import queue
import subprocess
import sys
import threading
import time

from run_qemu_smoke import (
    QEMU_MUX_SWITCH,
    SHELL_PROMPT,
    monitor_key_commands,
    stop_process,
)


def reader(stream, output: queue.Queue[str], finished: threading.Event) -> None:
    try:
        while True:
            value = stream.read(1)
            if not value:
                return
            output.put(value)
    finally:
        finished.set()


def drain(output: queue.Queue[str], transcript: list[str]) -> None:
    while True:
        try:
            transcript.append(output.get_nowait())
        except queue.Empty:
            return


def send_command(process: subprocess.Popen[str], command: str) -> None:
    if process.stdin is None:
        raise RuntimeError("QEMU monitor input unavailable")
    process.stdin.write(QEMU_MUX_SWITCH)
    process.stdin.flush()
    time.sleep(0.05)
    for key in monitor_key_commands(command):
        process.stdin.write(key)
        process.stdin.flush()
        time.sleep(0.02)
    process.stdin.write(QEMU_MUX_SWITCH)
    process.stdin.flush()


def send_key(process: subprocess.Popen[str], key: str) -> None:
    if process.stdin is None:
        raise RuntimeError("QEMU monitor input unavailable")
    process.stdin.write(QEMU_MUX_SWITCH)
    process.stdin.flush()
    time.sleep(0.05)
    process.stdin.write(f"sendkey {key}\n")
    process.stdin.flush()
    time.sleep(0.05)
    process.stdin.write(QEMU_MUX_SWITCH)
    process.stdin.flush()


def run(qemu: pathlib.Path, image: pathlib.Path, screenshot: pathlib.Path,
        timeout: float, expect_failure: bool) -> int:
    command = [
        str(qemu), "-accel", "tcg", "-machine", "pc", "-nodefaults",
        "-device", "VGA,vgamem_mb=1" if expect_failure else "VGA",
        "-m", "512M", "-display", "none",
        "-monitor", "none", "-serial", "mon:stdio", "-no-reboot",
        "-snapshot", "-drive", f"file={image},format=raw,if=ide,index=0",
    ]
    process = subprocess.Popen(command, stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True,
                               encoding="utf-8", errors="replace", bufsize=0)
    assert process.stdout is not None
    output: queue.Queue[str] = queue.Queue()
    finished = threading.Event()
    thread = threading.Thread(target=reader,
                              args=(process.stdout, output, finished), daemon=True)
    thread.start()
    transcript: list[str] = []
    deadline = time.monotonic() + timeout
    try:
        while time.monotonic() < deadline:
            drain(output, transcript)
            text = "".join(transcript)
            if SHELL_PROMPT in text:
                send_command(process, "desktop.prg")
                break
            time.sleep(0.02)
        else:
            raise RuntimeError("VGA shell prompt not observed")
        while time.monotonic() < deadline:
            drain(output, transcript)
            text = "".join(transcript)
            if "DESKTOP_OK" in text:
                if expect_failure:
                    print("runtime-desktop: FAIL: unsupported mode was accepted",
                          file=sys.stderr)
                    return 1
                if process.stdin is not None:
                    process.stdin.write(QEMU_MUX_SWITCH)
                    process.stdin.flush()
                    time.sleep(0.05)
                    process.stdin.write(f"screendump {screenshot}\n")
                    process.stdin.flush()
                    time.sleep(0.05)
                    process.stdin.write(QEMU_MUX_SWITCH)
                    process.stdin.flush()
                send_key(process, "esc")
                while time.monotonic() < deadline:
                    drain(output, transcript)
                    exited = "DESKTOP_EXIT_OK" in "".join(transcript)
                    if exited:
                        exit_offset = "".join(transcript).index(
                            "DESKTOP_EXIT_OK")
                        if SHELL_PROMPT in "".join(transcript)[exit_offset:]:
                            print("runtime-desktop: PASS")
                            return 0
                    time.sleep(0.02)
                raise RuntimeError("desktop did not restore the VGA shell")
            if "DISPLAY_CONTROL: native graphics unavailable" in text:
                raise RuntimeError("native runtime graphics activation failed")
            time.sleep(0.02)
        drain(output, transcript)
        tail = "".join(transcript)[-1200:].replace("\r", "")
        raise RuntimeError(f"DESKTOP_OK marker not observed; guest tail:\n{tail}")
    finally:
        stop_process(process)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", type=pathlib.Path, required=True)
    parser.add_argument("--image", type=pathlib.Path, required=True)
    parser.add_argument("--screenshot", type=pathlib.Path, required=True)
    parser.add_argument("--timeout", type=float, default=90.0)
    parser.add_argument("--expect-failure", action="store_true")
    args = parser.parse_args()
    try:
        return run(args.qemu, args.image, args.screenshot, args.timeout,
                   args.expect_failure)
    except (OSError, RuntimeError) as error:
        print(f"runtime-desktop: FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
