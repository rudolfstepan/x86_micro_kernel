#!/usr/bin/env python3
"""Boot the framebuffer image and require the graphical desktop marker."""

from __future__ import annotations

import argparse
import json
import queue
import re
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path


BOOT_MARKER = "BOOT_OK"
BOOT_FRAMEBUFFER_PREFIX = "Framebuffer initialized:"
RUNTIME_FRAMEBUFFER_PREFIX = "DISPLAY_CONTROL: QEMU framebuffer ready"
DESKTOP_MARKER = "DESKTOP_OK"
FAIL_MARKERS = (
    "PANIC:",
    "KERNEL ASSERTION FAILED",
    "Kernel exception:",
    "User process exception:",
    "Unable to start DESKTOP.PRG",
    "desktop: Grafikmodus nicht verfuegbar",
    "Graphical desktop exited",
)


def exact_line_position(text: str, expected: str) -> int:
    match = re.search(rf"(?:^|\n){re.escape(expected)}\r?(?=\n|$)", text)
    if match is None:
        return -1
    return match.start() + (1 if text[match.start():].startswith("\n") else 0)


def prefix_line_position(text: str, prefix: str) -> int:
    match = re.search(rf"(?:^|\n){re.escape(prefix)}", text)
    if match is None:
        return -1
    return match.start() + (1 if text[match.start():].startswith("\n") else 0)


def failure_marker(text: str) -> str | None:
    for line in text.splitlines():
        clean = line.rstrip("\r")
        for marker in FAIL_MARKERS:
            if clean.startswith(marker):
                return marker
    return None


def validate(transcript: str) -> str | None:
    failed = failure_marker(transcript)
    if failed is not None:
        return f"guest emitted failure marker {failed!r}"
    boot_framebuffer = prefix_line_position(transcript, BOOT_FRAMEBUFFER_PREFIX)
    runtime_framebuffer = prefix_line_position(
        transcript, RUNTIME_FRAMEBUFFER_PREFIX)
    boot = exact_line_position(transcript, BOOT_MARKER)
    desktop = exact_line_position(transcript, DESKTOP_MARKER)
    if boot_framebuffer < 0 and runtime_framebuffer < 0:
        return "missing framebuffer-ready line"
    if boot < 0:
        return f"missing {BOOT_MARKER} marker"
    if desktop < 0:
        return f"missing {DESKTOP_MARKER} marker"
    boot_ordered = boot_framebuffer >= 0 and boot_framebuffer < boot < desktop
    runtime_ordered = (runtime_framebuffer >= 0 and
                       boot < runtime_framebuffer < desktop)
    if not boot_ordered and not runtime_ordered:
        return "desktop markers appeared out of order"
    return None


def reserve_qmp_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return int(listener.getsockname()[1])


def qemu_command(qemu: Path, image: Path, memory: str,
                 qmp_port: int | None = None) -> list[str]:
    command = [
        str(qemu),
        "-accel", "tcg",
        "-machine", "pc",
        "-nodefaults",
        "-m", memory,
        "-boot", "c",
        "-drive", f"file={image},format=raw,if=ide,index=0,media=disk",
        "-snapshot",
        "-display", "none",
        "-monitor", "none",
        "-serial", "stdio",
        "-vga", "std",
        "-no-reboot",
        "-no-shutdown",
    ]
    if qmp_port is not None:
        command.extend([
            "-qmp", f"tcp:127.0.0.1:{qmp_port},server=on,wait=off",
        ])
    return command


def reader(stream, chunks: queue.Queue[str], finished: threading.Event) -> None:
    try:
        while True:
            chunk = stream.read(1)
            if not chunk:
                return
            chunks.put(chunk)
    finally:
        finished.set()


def drain(chunks: queue.Queue[str], transcript: list[str]) -> None:
    while True:
        try:
            transcript.append(chunks.get_nowait())
        except queue.Empty:
            return


def wait_for_desktop(
    process: subprocess.Popen[str],
    chunks: queue.Queue[str],
    transcript: list[str],
    finished: threading.Event,
    deadline: float,
) -> str | None:
    while time.monotonic() < deadline:
        drain(chunks, transcript)
        text = "".join(transcript)
        failed = failure_marker(text)
        if failed is not None:
            return f"guest emitted failure marker {failed!r}"
        if exact_line_position(text, DESKTOP_MARKER) >= 0:
            return validate(text)
        if process.poll() is not None:
            finished.wait(timeout=0.25)
            drain(chunks, transcript)
            text = "".join(transcript)
            marker_error = validate(text)
            if marker_error is None:
                return None
            return (
                f"QEMU exited with status {process.returncode}: {marker_error}"
            )
        try:
            transcript.append(chunks.get(timeout=0.05))
        except queue.Empty:
            pass
    return f"timeout before {DESKTOP_MARKER}"


def settle_desktop(
    process: subprocess.Popen[str],
    chunks: queue.Queue[str],
    transcript: list[str],
    finished: threading.Event,
    deadline: float,
    duration: float = 0.5,
) -> str | None:
    settle_deadline = time.monotonic() + duration
    if settle_deadline > deadline:
        return "timeout during desktop stability window"
    while time.monotonic() < settle_deadline:
        drain(chunks, transcript)
        text = "".join(transcript)
        failed = failure_marker(text)
        if failed is not None:
            return f"guest emitted failure marker {failed!r}"
        if process.poll() is not None:
            finished.wait(timeout=0.25)
            drain(chunks, transcript)
            failed = failure_marker("".join(transcript))
            if failed is not None:
                return f"guest emitted failure marker {failed!r}"
            return (
                f"QEMU exited with status {process.returncode} during "
                "desktop stability window"
            )
        remaining = settle_deadline - time.monotonic()
        if remaining <= 0:
            break
        try:
            transcript.append(chunks.get(timeout=min(remaining, 0.05)))
        except queue.Empty:
            pass
    drain(chunks, transcript)
    failed = failure_marker("".join(transcript))
    if failed is not None:
        return f"guest emitted failure marker {failed!r}"
    if process.poll() is not None:
        return (
            f"QEMU exited with status {process.returncode} during "
            "desktop stability window"
        )
    return None


def stop_process(process: subprocess.Popen[str]) -> str | None:
    if process.poll() is not None:
        return None
    try:
        process.terminate()
    except OSError:
        pass
    try:
        process.wait(timeout=3)
        return None
    except subprocess.TimeoutExpired:
        pass
    try:
        process.kill()
    except OSError as error:
        if process.poll() is not None:
            return None
        return f"unable to kill QEMU PID {process.pid}: {error}"
    try:
        process.wait(timeout=3)
        return None
    except subprocess.TimeoutExpired:
        return f"QEMU PID {process.pid} survived kill and was not reaped"


def _receive_qmp_line(connection: socket.socket, buffered: bytearray,
                      deadline: float) -> dict[str, object]:
    while b"\n" not in buffered:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("timeout waiting for QMP response")
        connection.settimeout(min(remaining, 0.25))
        try:
            chunk = connection.recv(4096)
        except socket.timeout:
            continue
        if not chunk:
            raise ConnectionError("QMP connection closed")
        buffered.extend(chunk)
    line, _, rest = buffered.partition(b"\n")
    buffered[:] = rest
    return json.loads(line.decode("utf-8"))


def _wait_qmp_reply(connection: socket.socket, buffered: bytearray,
                    identifier: str, deadline: float) -> str | None:
    while True:
        response = _receive_qmp_line(connection, buffered, deadline)
        if response.get("id") != identifier:
            continue
        if "error" in response:
            return f"QMP command failed: {response['error']}"
        return None


def qmp_screendump(port: int, screenshot: Path, deadline: float) -> str | None:
    connection: socket.socket | None = None
    while time.monotonic() < deadline:
        try:
            connection = socket.create_connection(
                ("127.0.0.1", port), timeout=0.25
            )
            break
        except OSError:
            time.sleep(0.05)
    if connection is None:
        return "unable to connect to QMP for screenshot"

    buffered = bytearray()
    try:
        greeting = _receive_qmp_line(connection, buffered, deadline)
        if "QMP" not in greeting:
            return "invalid QMP greeting"
        capabilities = "desktop-capabilities"
        connection.sendall((json.dumps({
            "execute": "qmp_capabilities", "id": capabilities,
        }) + "\n").encode("utf-8"))
        error = _wait_qmp_reply(connection, buffered, capabilities, deadline)
        if error is not None:
            return error

        command_id = "desktop-screendump"
        connection.sendall((json.dumps({
            "execute": "screendump",
            "arguments": {"filename": str(screenshot)},
            "id": command_id,
        }) + "\n").encode("utf-8"))
        error = _wait_qmp_reply(connection, buffered, command_id, deadline)
        if error is not None:
            return error
    except (ConnectionError, OSError, TimeoutError, ValueError) as error:
        return f"QMP screenshot failed: {error}"
    finally:
        connection.close()

    if not screenshot.is_file() or screenshot.stat().st_size == 0:
        return "QMP reported success without creating the screenshot"
    return None


def run(qemu: Path, image: Path, timeout: float, memory: str,
        screenshot: Path | None = None) -> tuple[int, str, str | None]:
    qmp_port = reserve_qmp_port() if screenshot is not None else None
    process = subprocess.Popen(
        qemu_command(qemu, image, memory, qmp_port),
        stdin=subprocess.DEVNULL,
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
    thread: threading.Thread | None = None
    deadline = time.monotonic() + timeout
    error: str | None = None
    try:
        thread = threading.Thread(
            target=reader, args=(process.stdout, chunks, finished), daemon=True
        )
        thread.start()
        error = wait_for_desktop(
            process, chunks, transcript, finished, deadline
        )
        if error is None:
            # DESKTOP_OK precedes the final repaint.  Always keep QEMU alive
            # long enough to catch an immediate desktop exit/fault, even when
            # the caller did not request a screenshot.
            error = settle_desktop(
                process, chunks, transcript, finished, deadline
            )
        if error is None and screenshot is not None:
            assert qmp_port is not None
            error = qmp_screendump(qmp_port, screenshot, deadline)
    finally:
        # Always reap QEMU, including timeout, QMP, parsing and screenshot
        # failures.  This is intentionally independent of marker success.
        cleanup_error = stop_process(process)
        if thread is not None:
            finished.wait(timeout=1)
            thread.join(timeout=1)
            if thread.is_alive():
                process.stdout.close()
                thread.join(timeout=1)
        else:
            process.stdout.close()
        drain(chunks, transcript)
        if cleanup_error is not None:
            error = (cleanup_error if error is None
                     else f"{error}; {cleanup_error}")
    text = "".join(transcript)
    return (0 if error is None else 1), text, error


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", type=Path, default=Path("qemu-system-i386"))
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=90.0)
    parser.add_argument("--memory", default="512M")
    parser.add_argument("--log", type=Path)
    parser.add_argument(
        "--screenshot", type=Path,
        help="write a QMP screendump after the serial DESKTOP_OK marker",
    )
    args = parser.parse_args()

    if not args.image.is_file():
        print(f"desktop-smoke: image not found: {args.image}", file=sys.stderr)
        return 2
    if args.timeout <= 0:
        print("desktop-smoke: timeout must be positive", file=sys.stderr)
        return 2
    if re.fullmatch(r"[1-9][0-9]*[KMG]", args.memory,
                    flags=re.IGNORECASE) is None:
        print("desktop-smoke: memory must look like 64M or 1G", file=sys.stderr)
        return 2

    screenshot = args.screenshot.resolve() if args.screenshot else None
    if screenshot is not None:
        screenshot.parent.mkdir(parents=True, exist_ok=True)
        screenshot.unlink(missing_ok=True)
    try:
        status, transcript, process_error = run(
            args.qemu, args.image.resolve(), args.timeout, args.memory,
            screenshot,
        )
    except OSError as error:
        print(f"desktop-smoke: unable to start QEMU: {error}", file=sys.stderr)
        return 2

    if args.log:
        args.log.parent.mkdir(parents=True, exist_ok=True)
        args.log.write_text(transcript, encoding="utf-8")

    marker_error = validate(transcript)
    if marker_error is None and process_error is None:
        print(transcript, end="" if transcript.endswith("\n") else "\n")
        if screenshot is not None:
            print(f"desktop-smoke: screenshot: {screenshot}")
        print("desktop-smoke: PASS")
        return 0

    print(transcript, end="" if transcript.endswith("\n") else "\n",
          file=sys.stderr)
    print(f"desktop-smoke: FAIL: {process_error or marker_error}",
          file=sys.stderr)
    return status or 1


if __name__ == "__main__":
    raise SystemExit(main())
