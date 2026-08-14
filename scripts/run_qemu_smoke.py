#!/usr/bin/env python3
"""Boot the native image in QEMU and require ordered guest-test markers."""

from __future__ import annotations

import argparse
import queue
import re
import subprocess
import sys
import threading
import time
from pathlib import Path


BOOT_MARKER = "BOOT_OK"
TEST_MARKER = "TEST_OK"
FATAL_ARMED_MARKER = "REIST_TEST DOUBLE_FAULT_ARMED"
FATAL_MARKER = "REIST_FATAL DOUBLE_FAULT RESET"
RECOVERY_MARKER = "REIST_RECOVERY PREVIOUS_FATAL"
RECOVERY_OK_MARKER = "REIST_TEST FATAL_RECOVERY_OK"
REIST_PROBE_MARKERS = (
    "REIST_PROBE CRASH_DETECTED",
    "REIST_PROBE CRASH_RECOVERED",
    "REIST_PROBE HANG_DETECTED",
    "REIST_PROBE HANG_RECOVERED",
    "REIST_PROBE INVALID_REPLY_DETECTED",
    "REIST_PROBE INVALID_RECOVERED",
    "REIST_PROBE REINTEGRATED",
)
REIST_PROBE_COMPLETION_MARKER = "REIST_PROBE RECOVERY_SEQUENCE_OK"
REIST_SERVICE_MARKER = "TEST_STAGE DIAGNOSTIC_SERVICE_OK"
REIST_SERVICE_CORRELATION_MARKER = "TEST_STAGE SERVICE_CORRELATION_OK"
REIST_NETWORK_MARKER = "TEST_STAGE NETWORK_PARSER_OK"
REIST_ARP_VALIDATION_MARKER = "TEST_STAGE ARP_VALIDATION_OK"
REIST_ARP_IDENTITY_MARKER = "TEST_STAGE ARP_IDENTITY_OK"
REIST_NETWORK_HANDOFF_MARKER = "TEST_STAGE NETWORK_HANDOFF_OK"
REIST_NETWORK_PROBE_ID_MARKER = "REIST_NETWORK PROBE_ID_OK"
REIST_NETWORK_CRASH_MARKER = "REIST_NETWORK SERVICE_CRASH_RECOVERED"
REIST_NETWORK_RECOVERY_MARKER = "TEST_STAGE NETWORK_RECOVERY_OK"
REIST_NETWORK_PRESSURE_FALLBACK_MARKER = "REIST_NETWORK QUEUE_PRESSURE_FALLBACK"
REIST_NETWORK_PRESSURE_MARKER = "TEST_STAGE NETWORK_PRESSURE_OK"
SHELL_PROMPT = "C:\\>"
FAIL_MARKERS = (
    "TEST_FAIL",
    "PANIC:",
    "KERNEL ASSERTION FAILED",
    "Kernel exception:",
    "Unable to start SHELL.PRG",
)


def qemu_command(
    qemu: Path,
    image: Path,
    no_apic: bool = False,
    memory: str = "512M",
    watchdog: bool = False,
    allow_reboot: bool = False,
    nic: str = "none",
    persistent: bool = False,
) -> list[str]:
    command = [
        str(qemu),
        "-accel", "tcg",
        "-machine", "pc",
        "-nodefaults",
        "-m", memory,
        "-boot", "c",
        "-drive", f"file={image},format=raw,if=ide,index=0,media=disk",
        "-display", "none",
        "-monitor", "none",
        "-serial", "stdio",
        "-no-shutdown",
    ]
    if not persistent:
        command.append("-snapshot")
    if not allow_reboot:
        command.append("-no-reboot")
    if no_apic:
        command.extend(["-cpu", "qemu32,-apic"])
    if watchdog:
        command.extend(["-device", "ib700", "-watchdog-action", "reset"])
    if nic != "none":
        command.extend(["-device", f"{nic},netdev=reistnet0",
                        "-netdev", "user,id=reistnet0"])
    return command


def reader(
    stream,
    chunks: queue.Queue[str],
    finished: threading.Event,
) -> None:
    try:
        while True:
            chunk = stream.read(1)
            if not chunk:
                break
            chunks.put(chunk)
    finally:
        finished.set()


def drain(chunks: queue.Queue[str], transcript: list[str]) -> None:
    while True:
        try:
            transcript.append(chunks.get_nowait())
        except queue.Empty:
            return


def exact_line_position(text: str, expected: str, after: int = -1) -> int:
    pattern = re.compile(
        rf"(?:^|\n){re.escape(expected)}\r?(?=\n|$)"
    )
    for match in pattern.finditer(text):
        position = match.start() + (1 if text[match.start():].startswith("\n") else 0)
        if position > after:
            return position
    return -1


def failure_marker(text: str) -> str | None:
    for line in text.splitlines():
        clean = line.rstrip("\r")
        for marker in FAIL_MARKERS:
            if clean.startswith(marker):
                return marker
    return None


def wait_for_line(
    process: subprocess.Popen[str],
    chunks: queue.Queue[str],
    transcript: list[str],
    finished: threading.Event,
    expected: str,
    deadline: float,
    *,
    after: int = -1,
) -> tuple[str | None, int]:
    while time.monotonic() < deadline:
        drain(chunks, transcript)
        text = "".join(transcript)
        failed = failure_marker(text)
        if failed is not None:
            return f"guest emitted failure marker {failed!r}", -1
        position = exact_line_position(text, expected, after)
        if position >= 0:
            return None, position
        if process.poll() is not None:
            # stdout can reach EOF slightly after process.poll() changes.
            # Wait for the reader and inspect every final byte before failing.
            finished.wait(timeout=0.25)
            drain(chunks, transcript)
            text = "".join(transcript)
            failed = failure_marker(text)
            if failed is not None:
                return f"guest emitted failure marker {failed!r}", -1
            position = exact_line_position(text, expected, after)
            if position >= 0:
                return None, position
            return (
                f"QEMU exited with status {process.returncode} before {expected}",
                -1,
            )
        try:
            transcript.append(chunks.get(timeout=0.05))
        except queue.Empty:
            pass
    return f"timeout before {expected}", -1


def stop_process(process: subprocess.Popen[str]) -> None:
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=3)


def run(
    qemu: Path,
    image: Path,
    timeout: float,
    no_apic: bool = False,
    memory: str = "512M",
    watchdog: bool = False,
    allow_reboot: bool = False,
    nic: str = "none",
    persistent: bool = False,
    expect_reist_probe: bool = False,
) -> tuple[int, str, str | None]:
    process = subprocess.Popen(
        qemu_command(qemu, image, no_apic, memory, watchdog, allow_reboot, nic,
                     persistent),
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        bufsize=0,
    )
    assert process.stdin is not None and process.stdout is not None
    chunks: queue.Queue[str] = queue.Queue()
    transcript: list[str] = []
    finished = threading.Event()
    thread = threading.Thread(
        target=reader,
        args=(process.stdout, chunks, finished),
        daemon=True,
    )
    thread.start()
    deadline = time.monotonic() + timeout
    error: str | None = None
    try:
        error, _ = wait_for_line(
            process, chunks, transcript, finished, SHELL_PROMPT, deadline
        )
        if error is None and expect_reist_probe:
            error, _ = wait_for_line(
                process, chunks, transcript, finished,
                REIST_PROBE_COMPLETION_MARKER, deadline,
            )
        if error is None:
            # The UART RX path currently drops command bursts.  Pace every
            # byte so the same runner works on Windows and POSIX hosts.
            for character in "GTEST\n":
                process.stdin.write(character)
                process.stdin.flush()
                time.sleep(0.075)
            error, test_position = wait_for_line(
                process, chunks, transcript, finished, TEST_MARKER, deadline
            )
            if error is None:
                error, _ = wait_for_line(
                    process,
                    chunks,
                    transcript,
                    finished,
                    SHELL_PROMPT,
                    deadline,
                    after=test_position,
                )
    finally:
        stop_process(process)
        finished.wait(timeout=1)
        thread.join(timeout=1)
        drain(chunks, transcript)
    text = "".join(transcript)
    return (0 if error is None else 1), text, error


def validate(
    transcript: str,
    expect_fatal_recovery: bool = False,
    expect_reist_probe: bool = False,
    expect_network_handoff: bool = False,
) -> str | None:
    failed = failure_marker(transcript)
    if failed is not None:
        return f"guest emitted failure marker {failed!r}"
    boot = exact_line_position(transcript, BOOT_MARKER)
    test = exact_line_position(transcript, TEST_MARKER)
    if boot < 0:
        return f"missing {BOOT_MARKER} marker"
    if test < 0:
        return f"missing {TEST_MARKER} marker"
    if test < boot:
        return f"{TEST_MARKER} appeared before {BOOT_MARKER}"
    if exact_line_position(transcript, SHELL_PROMPT, after=test) < 0:
        return f"missing {SHELL_PROMPT} prompt after {TEST_MARKER}"
    if expect_fatal_recovery:
        positions = [exact_line_position(transcript, marker) for marker in (
            FATAL_ARMED_MARKER, FATAL_MARKER, RECOVERY_MARKER,
            RECOVERY_OK_MARKER, BOOT_MARKER,
        )]
        if any(position < 0 for position in positions):
            return "missing fatal-injection/recovery marker"
        if positions != sorted(positions):
            return "fatal-injection/recovery markers are out of order"
    if expect_reist_probe:
        completion = exact_line_position(transcript,
                                         REIST_PROBE_COMPLETION_MARKER)
        if completion < 0 or completion > test:
            return "missing cumulative REIST probe recovery marker"
        service = exact_line_position(transcript, REIST_SERVICE_MARKER)
        correlation = exact_line_position(transcript,
                                          REIST_SERVICE_CORRELATION_MARKER)
        if correlation < completion or service < correlation or service > test:
            return "missing ordered REIST diagnostic-service marker"
        network = exact_line_position(transcript, REIST_NETWORK_MARKER)
        arp_validation = exact_line_position(transcript,
                                             REIST_ARP_VALIDATION_MARKER)
        arp_identity = exact_line_position(transcript,
                                           REIST_ARP_IDENTITY_MARKER)
        if (arp_identity < completion or arp_validation < arp_identity or
                service < arp_validation or network < service or network > test):
            return "missing ordered REIST network-parser marker"
    if expect_network_handoff:
        handoff = exact_line_position(transcript,
                                      REIST_NETWORK_HANDOFF_MARKER)
        probe_id = exact_line_position(transcript,
                                       REIST_NETWORK_PROBE_ID_MARKER)
        if probe_id < 0 or handoff < probe_id or handoff > test:
            return "missing ordered real NIC network-handoff marker"
        crash = exact_line_position(transcript, REIST_NETWORK_CRASH_MARKER)
        pressure_fallback = exact_line_position(
            transcript, REIST_NETWORK_PRESSURE_FALLBACK_MARKER)
        pressure = exact_line_position(transcript,
                                       REIST_NETWORK_PRESSURE_MARKER)
        recovery = exact_line_position(transcript,
                                       REIST_NETWORK_RECOVERY_MARKER)
        if (pressure_fallback < handoff or pressure < pressure_fallback or
                crash < pressure or recovery < crash or recovery > test):
            return "missing ordered network-service crash recovery marker"
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", type=Path, default=Path("qemu-system-i386"))
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=90.0)
    parser.add_argument("--log", type=Path)
    parser.add_argument(
        "--memory",
        default="512M",
        help="QEMU guest RAM size (for example 64M, 512M, or 1024M)",
    )
    parser.add_argument(
        "--no-apic",
        action="store_true",
        help="disable the local APIC and exercise the PIT scheduler fallback",
    )
    parser.add_argument(
        "--watchdog",
        action="store_true",
        help="attach the qualified QEMU IB700 hardware-watchdog profile",
    )
    parser.add_argument(
        "--nic", choices=("none", "rtl8139", "e1000"), default="none",
        help="attach a supported NIC and exercise its REIST supervision",
    )
    parser.add_argument(
        "--expect-fatal-recovery",
        action="store_true",
        help="require ordered Double-Fault, reset and recovered-record markers",
    )
    parser.add_argument(
        "--expect-reist-probe",
        action="store_true",
        help="require ordered crash, hang and invalid-reply recovery markers",
    )
    parser.add_argument(
        "--expect-network-handoff",
        action="store_true",
        help="require a real NIC RX header to reach the Ring-3 service",
    )
    parser.add_argument(
        "--persistent", action="store_true",
        help="allow guest writes to the image (use only with a disposable copy)",
    )
    args = parser.parse_args()

    if not args.image.is_file():
        print(f"guest-smoke: image not found: {args.image}", file=sys.stderr)
        return 2
    if args.timeout <= 0:
        print("guest-smoke: timeout must be positive", file=sys.stderr)
        return 2
    if re.fullmatch(r"[1-9][0-9]*[KMG]", args.memory,
                    flags=re.IGNORECASE) is None:
        print("guest-smoke: memory must look like 64M or 1G", file=sys.stderr)
        return 2

    try:
        status, transcript, process_error = run(
            args.qemu, args.image.resolve(), args.timeout, args.no_apic,
            args.memory, args.watchdog, args.expect_fatal_recovery, args.nic,
            args.persistent, args.expect_reist_probe,
        )
    except OSError as error:
        print(f"guest-smoke: unable to start QEMU: {error}", file=sys.stderr)
        return 2

    if args.log:
        args.log.parent.mkdir(parents=True, exist_ok=True)
        args.log.write_text(transcript, encoding="utf-8")

    marker_error = validate(transcript, args.expect_fatal_recovery,
                            args.expect_reist_probe,
                            args.expect_network_handoff)
    if marker_error is None and process_error is None:
        print(transcript, end="" if transcript.endswith("\n") else "\n")
        print("guest-smoke: PASS")
        return 0

    print(transcript, end="" if transcript.endswith("\n") else "\n",
          file=sys.stderr)
    detail = process_error or marker_error
    if TEST_MARKER not in str(detail):
        detail = f"{detail}; missing {TEST_MARKER} marker"
    print(f"guest-smoke: FAIL: {detail}", file=sys.stderr)
    return status or 1


if __name__ == "__main__":
    raise SystemExit(main())
