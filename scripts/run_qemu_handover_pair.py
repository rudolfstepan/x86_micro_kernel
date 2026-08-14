"""Prove fenced takeover between two independent QEMU processes."""

from __future__ import annotations

import argparse
import queue
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path

import run_qemu_smoke as smoke


ACTIVE_STATE_MARKER = "REIST_HANDOVER ACTIVE_STATE_SENT"
STANDBY_STATE_MARKER = "REIST_HANDOVER STANDBY_STATE_APPLIED"


def launch(qemu: Path, image: Path, port: int) -> subprocess.Popen[str]:
    return subprocess.Popen(
        smoke.qemu_command(qemu, image, handover_port=port),
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        bufsize=0,
    )


def accept(listener: socket.socket) -> socket.socket:
    connection, _ = listener.accept()
    connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    connection.settimeout(20.0)
    listener.close()
    return connection


def start_reader(process: subprocess.Popen[str]):
    assert process.stdout is not None
    chunks: queue.Queue[str] = queue.Queue()
    transcript: list[str] = []
    finished = threading.Event()
    thread = threading.Thread(
        target=smoke.reader,
        args=(process.stdout, chunks, finished),
        daemon=True,
    )
    thread.start()
    return chunks, transcript, finished, thread


def wait(process, state, marker: str, deadline: float) -> str | None:
    chunks, transcript, finished, _ = state
    error, _ = smoke.wait_for_line(
        process, chunks, transcript, finished, marker, deadline)
    return error


def stop_and_drain(process, state) -> None:
    chunks, transcript, finished, thread = state
    smoke.stop_process(process)
    finished.wait(timeout=1)
    thread.join(timeout=1)
    smoke.drain(chunks, transcript)


def run(qemu: Path, active_image: Path, standby_image: Path,
        timeout: float) -> tuple[int, str, str | None]:
    active_listener, active_port = smoke.open_injection_listener()
    standby_listener, standby_port = smoke.open_injection_listener()
    active = standby = None
    active_connection = standby_connection = None
    active_state = standby_state = None
    events: list[str] = []
    error: str | None = None
    deadline = time.monotonic() + timeout
    try:
        active = launch(qemu, active_image, active_port)
        active_connection = accept(active_listener)
        active_state = start_reader(active)

        standby = launch(qemu, standby_image, standby_port)
        standby_connection = accept(standby_listener)
        standby_state = start_reader(standby)

        replica = smoke.receive_exact(
            active_connection, smoke.HANDOVER_SERIAL_FRAME.size)
        replicated = (None if replica is None else
                      smoke.validate_handover_frame(
                          replica, smoke.HANDOVER_SERIAL_REPLICA))
        if replicated != (1, 1):
            error = "invalid or missing active-state replica"
        if error is None:
            error = wait(active, active_state, ACTIVE_STATE_MARKER, deadline)
        if error is None:
            ready = smoke.receive_exact(
                standby_connection, smoke.HANDOVER_SERIAL_FRAME.size)
            parsed_ready = (None if ready is None else
                            smoke.validate_handover_frame(
                                ready, smoke.HANDOVER_SERIAL_READY))
            if parsed_ready != (2, 1):
                error = "invalid or missing standby-ready frame"
        if error is None:
            events.append("HOST_STANDBY_READY")
            standby_connection.sendall(replica)
            events.append("HOST_REPLICA_FORWARDED")
            error = wait(standby, standby_state, STANDBY_STATE_MARKER, deadline)

        request = None
        if error is None:
            request = smoke.receive_exact(
                standby_connection, smoke.HANDOVER_SERIAL_FRAME.size)
            parsed = (None if request is None else
                      smoke.validate_handover_frame(
                          request, smoke.HANDOVER_SERIAL_REQUEST))
            if parsed != (1, 1):
                error = "invalid or missing standby fence request"
        if error is None and active.poll() is not None:
            error = "active QEMU exited before external fencing"
        if error is None:
            smoke.stop_process(active)
            if active.poll() is None:
                error = "active QEMU remained alive after fencing"
            else:
                events.append("HOST_FENCE_ACTIVE_STOPPED")
        if error is None:
            standby_connection.sendall(smoke.handover_frame(
                smoke.HANDOVER_SERIAL_ACK, 1, 1))
            events.append("HOST_FENCE_ACK_SENT")

        if error is None:
            for marker in (
                "REIST_HANDOVER REQUEST_SENT",
                "REIST_HANDOVER FENCE_CONFIRMED",
                "REIST_HANDOVER TAKEOVER_OK",
                smoke.SHELL_PROMPT,
                smoke.REIST_PROBE_COMPLETION_MARKER,
            ):
                error = wait(standby, standby_state, marker, deadline)
                if error is not None:
                    break
        if error is None:
            assert standby.stdin is not None
            for character in "GTEST\n":
                standby.stdin.write(character)
                standby.stdin.flush()
                time.sleep(0.075)
            error = wait(standby, standby_state, smoke.TEST_MARKER, deadline)
        if error is None:
            chunks, transcript, finished, _ = standby_state
            test_position = smoke.exact_line_position(
                "".join(transcript), smoke.TEST_MARKER)
            line_error, _ = smoke.wait_for_line(
                standby, chunks, transcript, finished, smoke.SHELL_PROMPT,
                deadline, after=test_position)
            error = line_error
    except (OSError, subprocess.SubprocessError) as exception:
        error = f"pair runner failure: {exception}"
    finally:
        if active_connection is not None:
            active_connection.close()
        if standby_connection is not None:
            standby_connection.close()
        active_listener.close()
        standby_listener.close()
        if active is not None and active_state is not None:
            stop_and_drain(active, active_state)
        if standby is not None and standby_state is not None:
            stop_and_drain(standby, standby_state)

    active_text = "" if active_state is None else "".join(active_state[1])
    standby_text = "" if standby_state is None else "".join(standby_state[1])
    transcript = "\n".join((
        "=== ACTIVE CHANNEL ===", active_text,
        "=== HOST SUPERVISOR ===", *events,
        "=== STANDBY CHANNEL ===", standby_text,
    ))
    if error is None:
        marker_error = smoke.validate(
            standby_text, expect_reist_probe=True, expect_handover=True)
        if marker_error is not None:
            error = marker_error
    return (0 if error is None else 1), transcript, error


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", type=Path, default=Path("qemu-system-i386"))
    parser.add_argument("--active-image", type=Path, required=True)
    parser.add_argument("--standby-image", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=120.0)
    parser.add_argument("--log", type=Path)
    args = parser.parse_args()
    if (not args.active_image.is_file() or
            not args.standby_image.is_file() or args.timeout <= 0):
        print("handover-pair: invalid image or timeout", file=sys.stderr)
        return 2
    status, transcript, error = run(
        args.qemu, args.active_image.resolve(),
        args.standby_image.resolve(), args.timeout)
    if args.log:
        args.log.parent.mkdir(parents=True, exist_ok=True)
        args.log.write_text(transcript, encoding="utf-8")
    stream = sys.stdout if status == 0 else sys.stderr
    print(transcript, file=stream)
    if status == 0:
        print("handover-pair: PASS")
        return 0
    print(f"handover-pair: FAIL: {error}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
