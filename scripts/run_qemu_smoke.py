#!/usr/bin/env python3
"""Boot the native image in QEMU and require ordered guest-test markers."""

from __future__ import annotations

import argparse
import queue
import re
import socket
import struct
import subprocess
import sys
import threading
import time
import zlib
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
REIST_NETWORK_STATS_MARKER = "TEST_STAGE NETWORK_STATS_OK"
REIST_ARP_VALIDATION_MARKER = "TEST_STAGE ARP_VALIDATION_OK"
REIST_ARP_IDENTITY_MARKER = "TEST_STAGE ARP_IDENTITY_OK"
REIST_NETWORK_HANDOFF_MARKER = "TEST_STAGE NETWORK_HANDOFF_OK"
REIST_NETWORK_INJECTION_READY_MARKER = "TEST_STAGE NETWORK_INJECTION_READY"
REIST_NETWORK_PROBE_ID_MARKER = "REIST_NETWORK PROBE_ID_OK"
REIST_ARP_BINDING_MARKER = "REIST_NETWORK ARP_BINDING_OK"
REIST_ARP_REVOKED_MARKER = "REIST_NETWORK ARP_BINDINGS_REVOKED"
REIST_ARP_REQUEST_QUEUED_MARKER = "REIST_NETWORK ARP_REQUEST_QUEUED"
REIST_ARP_RESOLUTION_QUEUED_MARKER = "REIST_NETWORK ARP_RESOLUTION_QUEUED"
REIST_ARP_RESOLUTION_MARKER = "REIST_NETWORK ARP_RESOLUTION_MEDIATED"
REIST_ARP_REPLY_MARKER = "REIST_NETWORK ARP_REPLY_MEDIATED"
REIST_ICMP_ECHO_QUEUED_MARKER = "REIST_NETWORK ICMP_ECHO_QUEUED"
REIST_ICMP_ECHO_MARKER = "REIST_NETWORK ICMP_ECHO_MEDIATED"
REIST_DHCP_CONFIG_QUEUED_MARKER = "REIST_NETWORK DHCP_CONFIG_QUEUED"
REIST_DHCP_CONFIG_MARKER = "REIST_NETWORK DHCP_CONFIG_MEDIATED"
REIST_DHCP_LEASE_EXPIRED_MARKER = "REIST_NETWORK DHCP_LEASE_EXPIRED"
REIST_UDP_ECHO_QUEUED_MARKER = "REIST_NETWORK UDP_ECHO_QUEUED"
REIST_UDP_ECHO_MARKER = "REIST_NETWORK UDP_ECHO_MEDIATED"
REIST_UDP_DATAGRAM_QUEUED_MARKER = "REIST_NETWORK UDP_DATAGRAM_QUEUED"
REIST_UDP_DATAGRAM_MARKER = "REIST_NETWORK UDP_DATAGRAM_MEDIATED"
REIST_NETWORK_CRASH_MARKER = "REIST_NETWORK SERVICE_CRASH_RECOVERED"
REIST_NETWORK_RECOVERY_MARKER = "TEST_STAGE NETWORK_RECOVERY_OK"
REIST_NETWORK_PRESSURE_FALLBACK_MARKER = "REIST_NETWORK QUEUE_PRESSURE_FALLBACK"
REIST_NETWORK_PRESSURE_MARKER = "TEST_STAGE NETWORK_PRESSURE_OK"
REIST_STORAGE_CRASH_MARKER = "REIST_STORAGE TEST_CRASH_INJECTED"
REIST_STORAGE_FAILURE_MARKER = "REIST_STORAGE SERVICE_FAILURE_DETECTED"
REIST_STORAGE_RESTARTED_MARKER = "REIST_STORAGE SERVICE_RESTARTED"
REIST_STORAGE_READY_MARKER = "REIST_STORAGE SERVICE_READY"
REIST_STORAGE_RECOVERY_MARKER = "TEST_STAGE STORAGE_RESTART_OK"
REIST_STORAGE_IO_INJECTION_MARKER = "REIST_STORAGE TEST_IO_ERROR_INJECTED"
REIST_STORAGE_QUARANTINE_MARKER = "REIST_STORAGE RESOURCE_QUARANTINED 0"
REIST_STORAGE_IO_RECOVERY_MARKER = "TEST_STAGE STORAGE_IO_QUARANTINE_OK"
REIST_STORAGE_SELF_TEST_MARKER = "TEST_STAGE STORAGE_SERVICE_OK"
REIST_HANDOVER_MARKERS = (
    "REIST_HANDOVER REQUEST_SENT",
    "REIST_HANDOVER FENCE_CONFIRMED",
    "REIST_HANDOVER TAKEOVER_OK",
)
HANDOVER_SERIAL_MAGIC = 0x54464952
HANDOVER_SERIAL_VERSION = 1
HANDOVER_SERIAL_REQUEST = 1
HANDOVER_SERIAL_ACK = 2
HANDOVER_SERIAL_REPLICA = 3
HANDOVER_SERIAL_READY = 4
HANDOVER_SERIAL_STATE = 5
HANDOVER_SERIAL_FRAME = struct.Struct("<IBBHIQI")
HANDOVER_SERIAL_STATE_FRAME = struct.Struct("<IBBHIIIIQQIII")
SHELL_PROMPT = "C:\\>"
FAIL_MARKERS = (
    "TEST_FAIL",
    "PANIC:",
    "KERNEL ASSERTION FAILED",
    "Kernel exception:",
    "Unable to start SHELL.PRG",
    "REIST_NETWORK ARP_REPLY_REJECTED",
    "REIST_NETWORK ICMP_ECHO_REJECTED",
    "REIST_NETWORK DHCP_CONFIG_REJECTED",
    "REIST_NETWORK UDP_ECHO_REJECTED",
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
    injection_port: int | None = None,
    handover_port: int | None = None,
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
        if injection_port is None:
            command.extend(["-device", f"{nic},netdev=reistnet0",
                            "-netdev", "user,id=reistnet0"])
        else:
            command.extend([
                "-netdev", "user,id=reistuser",
                "-netdev", ("socket,id=reistsocket,connect=127.0.0.1:"
                            f"{injection_port}"),
                "-netdev", "hubport,id=reistuserport,hubid=0,netdev=reistuser",
                "-netdev", ("hubport,id=reistsocketport,hubid=0,"
                            "netdev=reistsocket"),
                "-netdev", "hubport,id=reistnicport,hubid=0",
                "-device", f"{nic},netdev=reistnicport",
            ])
    if handover_port is not None:
        command.extend(["-serial", f"tcp:127.0.0.1:{handover_port}"])
    return command


def open_injection_listener() -> tuple[socket.socket, int]:
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.bind(("127.0.0.1", 0))
    listener.listen(1)
    listener.settimeout(5.0)
    return listener, int(listener.getsockname()[1])


def handover_frame(frame_type: int, active_node: int, epoch: int) -> bytes:
    prefix = HANDOVER_SERIAL_FRAME.pack(
        HANDOVER_SERIAL_MAGIC, HANDOVER_SERIAL_VERSION, frame_type,
        HANDOVER_SERIAL_FRAME.size, active_node, epoch, 0,
    )
    crc = zlib.crc32(prefix[:-4]) & 0xFFFFFFFF
    return prefix[:-4] + struct.pack("<I", crc)


def validate_handover_frame(data: bytes, expected_type: int) -> tuple[int, int] | None:
    if len(data) != HANDOVER_SERIAL_FRAME.size:
        return None
    magic, version, frame_type, size, active_node, epoch, crc = (
        HANDOVER_SERIAL_FRAME.unpack(data)
    )
    if (magic != HANDOVER_SERIAL_MAGIC or version != HANDOVER_SERIAL_VERSION or
            frame_type != expected_type or size != len(data) or
            active_node == 0 or epoch == 0 or
            zlib.crc32(data[:-4]) & 0xFFFFFFFF != crc):
        return None
    return active_node, epoch


def validate_handover_state_frame(
    data: bytes,
) -> tuple[int, int, int, int, int] | None:
    if len(data) != HANDOVER_SERIAL_STATE_FRAME.size:
        return None
    (magic, version, frame_type, size, state_version, state_size,
     source_node, service_id, epoch, sequence, value, reserved, crc) = (
        HANDOVER_SERIAL_STATE_FRAME.unpack(data)
    )
    if (magic != HANDOVER_SERIAL_MAGIC or version != HANDOVER_SERIAL_VERSION or
            frame_type != HANDOVER_SERIAL_STATE or size != len(data) or
            state_version != 1 or state_size != 40 or source_node == 0 or
            service_id == 0 or epoch == 0 or sequence == 0 or reserved != 0 or
            zlib.crc32(data[:-4]) & 0xFFFFFFFF != crc):
        return None
    return source_node, service_id, epoch, sequence, value


def serve_handover_fence(connection: socket.socket, timeout: float,
                         result: list[str | None]) -> None:
    try:
        connection.settimeout(timeout)
        request = receive_exact(connection, HANDOVER_SERIAL_FRAME.size)
        parsed = (None if request is None else
                  validate_handover_frame(request, HANDOVER_SERIAL_REQUEST))
        if parsed is None:
            result[0] = "invalid or missing handover fence request"
            return
        active_node, epoch = parsed
        connection.sendall(handover_frame(HANDOVER_SERIAL_ACK,
                                          active_node, epoch))
        result[0] = None
    except OSError as error:
        result[0] = f"handover fence channel failed: {error}"


def arp_request_frame() -> bytes:
    source_mac = bytes((0x02, 0xCA, 0xFE, 0x00, 0x00, 0x01))
    frame = b"".join((
        b"\xff" * 6, source_mac, b"\x08\x06",
        b"\x00\x01", b"\x08\x00", b"\x06\x04", b"\x00\x01",
        source_mac, bytes((10, 0, 2, 99)), b"\x00" * 6,
        bytes((10, 0, 2, 15)),
    ))
    return frame.ljust(60, b"\x00")


def internet_checksum(data: bytes) -> int:
    if len(data) & 1:
        data += b"\x00"
    total = sum(struct.unpack(f"!{len(data) // 2}H", data))
    while total >> 16:
        total = (total & 0xFFFF) + (total >> 16)
    return (~total) & 0xFFFF


def icmp_echo_request_frame() -> bytes:
    source_mac = bytes((0x02, 0xCA, 0xFE, 0x00, 0x00, 0x02))
    destination_mac = b"\xff" * 6
    source_ip = bytes((10, 0, 2, 99))
    destination_ip = bytes((10, 0, 2, 15))
    payload = b"REIS"
    icmp = struct.pack("!BBHHH", 8, 0, 0, 0x1234, 1) + payload
    icmp = icmp[:2] + struct.pack("!H", internet_checksum(icmp)) + icmp[4:]
    total_length = 20 + len(icmp)
    ip = struct.pack(
        "!BBHHHBBH4s4s", 0x45, 0, total_length, 0x5245, 0,
        64, 1, 0, source_ip, destination_ip,
    )
    ip = ip[:10] + struct.pack("!H", internet_checksum(ip)) + ip[12:]
    return (destination_mac + source_mac + b"\x08\x00" + ip + icmp).ljust(
        60, b"\x00")


def udp_echo_request_frame(destination_port: int = 9000) -> bytes:
    source_mac = bytes((0x02, 0xCA, 0xFE, 0x00, 0x00, 0x02))
    destination_mac = b"\xff" * 6
    source_ip = bytes((10, 0, 2, 99))
    destination_ip = bytes((10, 0, 2, 15))
    payload = b"REIST-UDP"
    udp_length = 8 + len(payload)
    udp = struct.pack("!HHHH", 40000, destination_port, udp_length, 0) + payload
    pseudo = source_ip + destination_ip + struct.pack("!BBH", 0, 17,
                                                       udp_length)
    checksum = internet_checksum(pseudo + udp)
    udp = udp[:6] + struct.pack("!H", checksum or 0xFFFF) + udp[8:]
    total_length = 20 + len(udp)
    ip = struct.pack(
        "!BBHHHBBH4s4s", 0x45, 0, total_length, 0x5544, 0,
        64, 17, 0, source_ip, destination_ip,
    )
    ip = ip[:10] + struct.pack("!H", internet_checksum(ip)) + ip[12:]
    return (destination_mac + source_mac + b"\x08\x00" + ip + udp).ljust(
        60, b"\x00")


def inject_ethernet_frame(
    connection: socket.socket, frame: bytes
) -> bool:
    if len(frame) < 14 or len(frame) > 1514:
        return False
    try:
        framed = struct.pack("!I", len(frame)) + frame
        connection.sendall(framed)
        return True
    except OSError:
        return False


def receive_exact(connection: socket.socket, size: int) -> bytes | None:
    data = bytearray()
    try:
        while len(data) < size:
            chunk = connection.recv(size - len(data))
            if not chunk:
                return None
            data.extend(chunk)
    except OSError:
        return None
    return bytes(data)


def receive_arp_request(connection: socket.socket, target: bytes,
                        deadline: float) -> bool:
    while time.monotonic() < deadline:
        connection.settimeout(max(0.01, deadline - time.monotonic()))
        header = receive_exact(connection, 4)
        if header is None:
            return False
        length = struct.unpack("!I", header)[0]
        if length < 14 or length > 1514:
            return False
        frame = receive_exact(connection, length)
        if frame is None:
            return False
        if (len(frame) >= 42 and frame[12:14] == b"\x08\x06" and
                frame[20:22] == b"\x00\x01" and frame[38:42] == target):
            return True
    return False


def receive_icmp_echo_reply(connection: socket.socket,
                            deadline: float) -> bool:
    expected_destination = bytes((0x02, 0xCA, 0xFE, 0x00, 0x00, 0x02))
    expected_source_ip = bytes((10, 0, 2, 15))
    expected_destination_ip = bytes((10, 0, 2, 99))
    while time.monotonic() < deadline:
        connection.settimeout(max(0.01, deadline - time.monotonic()))
        header = receive_exact(connection, 4)
        if header is None:
            return False
        length = struct.unpack("!I", header)[0]
        if length < 14 or length > 1514:
            return False
        frame = receive_exact(connection, length)
        if frame is None:
            return False
        if (len(frame) < 46 or frame[0:6] != expected_destination or
                frame[12:14] != b"\x08\x00"):
            continue
        ihl = (frame[14] & 0x0F) * 4
        if (ihl < 20 or len(frame) < 14 + ihl + 12 or frame[23] != 1 or
                frame[26:30] != expected_source_ip or
                frame[30:34] != expected_destination_ip):
            continue
        icmp = frame[14 + ihl:]
        ip_total = struct.unpack("!H", frame[16:18])[0]
        icmp_length = ip_total - ihl
        if icmp_length < 8 or len(icmp) < icmp_length:
            continue
        icmp = icmp[:icmp_length]
        if (icmp[0:2] == b"\x00\x00" and
                icmp[4:8] == b"\x12\x34\x00\x01" and
                icmp[8:] == b"REIS" and internet_checksum(icmp) == 0):
            return True
    return False


def receive_udp_echo_reply(connection: socket.socket, deadline: float,
                           source_port_expected: int = 9000) -> bool:
    expected_destination = bytes((0x02, 0xCA, 0xFE, 0x00, 0x00, 0x02))
    expected_source_ip = bytes((10, 0, 2, 15))
    expected_destination_ip = bytes((10, 0, 2, 99))
    while time.monotonic() < deadline:
        connection.settimeout(max(0.01, deadline - time.monotonic()))
        header = receive_exact(connection, 4)
        if header is None:
            return False
        length = struct.unpack("!I", header)[0]
        if length < 14 or length > 1514:
            return False
        frame = receive_exact(connection, length)
        if frame is None:
            return False
        if (len(frame) < 42 or frame[0:6] != expected_destination or
                frame[12:14] != b"\x08\x00"):
            continue
        ihl = (frame[14] & 0x0F) * 4
        if (ihl < 20 or len(frame) < 14 + ihl + 8 or frame[23] != 17 or
                frame[26:30] != expected_source_ip or
                frame[30:34] != expected_destination_ip):
            continue
        ip_total = struct.unpack("!H", frame[16:18])[0]
        udp = frame[14 + ihl:14 + ip_total]
        if len(udp) < 8:
            continue
        source_port, destination_port, udp_length, checksum = struct.unpack(
            "!HHHH", udp[:8])
        if (source_port != source_port_expected or destination_port != 40000 or
                udp_length != len(udp) or checksum == 0 or
                udp[8:] != b"REIST-UDP"):
            continue
        pseudo = (expected_source_ip + expected_destination_ip +
                  struct.pack("!BBH", 0, 17, udp_length))
        if internet_checksum(pseudo + udp) == 0:
            return True
    return False


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
    inject_arp_request: bool = False,
    expect_arp_resolution: bool = False,
    expect_handover: bool = False,
    inject_icmp_echo: bool = False,
    inject_udp_echo: bool = False,
    expect_dhcp_expiry: bool = False,
    udp_port: int = 9000,
) -> tuple[int, str, str | None]:
    injection_listener: socket.socket | None = None
    injection_connection: socket.socket | None = None
    injection_port: int | None = None
    handover_listener: socket.socket | None = None
    handover_connection: socket.socket | None = None
    handover_port: int | None = None
    handover_thread: threading.Thread | None = None
    handover_result: list[str | None] = [None]
    if (inject_arp_request or expect_arp_resolution or inject_icmp_echo or
            inject_udp_echo):
        injection_listener, injection_port = open_injection_listener()
    if expect_handover:
        handover_listener, handover_port = open_injection_listener()
    try:
        process = subprocess.Popen(
            qemu_command(qemu, image, no_apic, memory, watchdog, allow_reboot,
                         nic, persistent, injection_port, handover_port),
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            bufsize=0,
        )
    except BaseException:
        if injection_listener is not None:
            injection_listener.close()
        if handover_listener is not None:
            handover_listener.close()
        raise
    if injection_listener is not None:
        try:
            injection_connection, _ = injection_listener.accept()
            injection_connection.settimeout(None)
            injection_connection.setsockopt(socket.IPPROTO_TCP,
                                            socket.TCP_NODELAY, 1)
        except BaseException:
            stop_process(process)
            raise
        finally:
            injection_listener.close()
    if handover_listener is not None:
        try:
            handover_connection, _ = handover_listener.accept()
            handover_connection.setsockopt(socket.IPPROTO_TCP,
                                           socket.TCP_NODELAY, 1)
            handover_thread = threading.Thread(
                target=serve_handover_fence,
                args=(handover_connection, timeout, handover_result),
                daemon=True,
            )
            handover_thread.start()
        except BaseException:
            stop_process(process)
            raise
        finally:
            handover_listener.close()
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
        if error is None and expect_dhcp_expiry:
            error, expiry_position = wait_for_line(
                process, chunks, transcript, finished,
                REIST_DHCP_LEASE_EXPIRED_MARKER, deadline,
            )
            if error is None:
                settle_deadline = min(deadline, time.monotonic() + 0.5)
                while time.monotonic() < settle_deadline:
                    drain(chunks, transcript)
                    failed = failure_marker("".join(transcript))
                    if failed is not None:
                        error = f"guest emitted failure marker {failed!r}"
                        break
                    if process.poll() is not None:
                        error = "QEMU exited immediately after DHCP lease expiry"
                        break
                    time.sleep(0.02)
        elif error is None:
            # The UART RX path currently drops command bursts.  Pace every
            # byte so the same runner works on Windows and POSIX hosts.
            for character in "GTEST\n":
                process.stdin.write(character)
                process.stdin.flush()
                time.sleep(0.075)
            if inject_arp_request or inject_icmp_echo or inject_udp_echo:
                error, _ = wait_for_line(
                    process, chunks, transcript, finished,
                    REIST_NETWORK_INJECTION_READY_MARKER, deadline,
                )
            if error is None and inject_arp_request:
                assert injection_port is not None
                assert injection_connection is not None
                queued = False
                for _ in range(3):
                    if not inject_ethernet_frame(
                            injection_connection, arp_request_frame()):
                        error = "unable to inject bounded ARP request"
                        break
                    confirmation_deadline = min(deadline,
                                                time.monotonic() + 1.0)
                    confirmation_error, _ = wait_for_line(
                        process, chunks, transcript, finished,
                        REIST_ARP_REQUEST_QUEUED_MARKER,
                        confirmation_deadline,
                    )
                    if confirmation_error is None:
                        queued = True
                        break
                if error is None and not queued:
                    error = "ARP request was not queued after 3 bounded attempts"
                if error is None:
                    error, _ = wait_for_line(
                        process, chunks, transcript, finished,
                        REIST_ARP_REPLY_MARKER, deadline,
                    )
            if error is None and inject_icmp_echo:
                assert injection_connection is not None
                queued = False
                for _ in range(3):
                    if not inject_ethernet_frame(
                            injection_connection, icmp_echo_request_frame()):
                        error = "unable to inject bounded ICMP echo request"
                        break
                    confirmation_deadline = min(deadline,
                                                time.monotonic() + 1.0)
                    confirmation_error, _ = wait_for_line(
                        process, chunks, transcript, finished,
                        REIST_ICMP_ECHO_QUEUED_MARKER,
                        confirmation_deadline,
                    )
                    if confirmation_error is None:
                        queued = True
                        break
                if error is None and not queued:
                    error = "ICMP echo request was not queued after 3 bounded attempts"
                if error is None:
                    error, _ = wait_for_line(
                        process, chunks, transcript, finished,
                        REIST_ICMP_ECHO_MARKER, deadline,
                    )
                if error is None and not receive_icmp_echo_reply(
                        injection_connection, deadline):
                    error = "mediated ICMP echo reply was not observed on QEMU socket"
            if error is None and inject_udp_echo:
                assert injection_connection is not None
                queued_marker = (REIST_UDP_ECHO_QUEUED_MARKER if udp_port == 9000
                                 else REIST_UDP_DATAGRAM_QUEUED_MARKER)
                reply_marker = (REIST_UDP_ECHO_MARKER if udp_port == 9000
                                else REIST_UDP_DATAGRAM_MARKER)
                queued = False
                for _ in range(3):
                    if not inject_ethernet_frame(
                            injection_connection,
                            udp_echo_request_frame(udp_port)):
                        error = "unable to inject bounded UDP echo request"
                        break
                    confirmation_deadline = min(deadline,
                                                time.monotonic() + 1.0)
                    confirmation_error, _ = wait_for_line(
                        process, chunks, transcript, finished,
                        queued_marker,
                        confirmation_deadline,
                    )
                    if confirmation_error is None:
                        queued = True
                        break
                if error is None and not queued:
                    error = "UDP echo request was not queued after 3 bounded attempts"
                if error is None:
                    error, _ = wait_for_line(
                        process, chunks, transcript, finished,
                        reply_marker, deadline,
                    )
                if error is None and not receive_udp_echo_reply(
                        injection_connection, deadline, udp_port):
                    error = "mediated UDP echo reply was not observed on QEMU socket"
            test_position = -1
            if error is None:
                error, test_position = wait_for_line(
                    process, chunks, transcript, finished, TEST_MARKER,
                    deadline,
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
            if error is None and expect_arp_resolution:
                assert injection_connection is not None
                error, _ = wait_for_line(
                    process, chunks, transcript, finished,
                    REIST_ARP_RESOLUTION_QUEUED_MARKER, deadline)
                if error is None:
                    error, _ = wait_for_line(
                        process, chunks, transcript, finished,
                        REIST_ARP_RESOLUTION_MARKER, deadline)
                if error is None and not receive_arp_request(
                        injection_connection, bytes((10, 0, 2, 99)), deadline):
                    error = "mediated ARP request was not observed on QEMU socket"
    finally:
        if injection_connection is not None:
            injection_connection.close()
        if handover_thread is not None:
            handover_thread.join(timeout=1)
            if error is None and handover_thread.is_alive():
                error = "handover fence supervisor did not finish"
            elif error is None and handover_result[0] is not None:
                error = handover_result[0]
        if handover_connection is not None:
            handover_connection.close()
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
    expect_arp_reply: bool = False,
    expect_storage_recovery: bool = False,
    expect_storage_io_failure: bool = False,
    expect_storage_self_test: bool = False,
    expect_handover: bool = False,
    expect_icmp_echo: bool = False,
    expect_dhcp_config: bool = False,
    expect_dhcp_expiry: bool = False,
    expect_udp_echo: bool = False,
    expect_udp_binding: bool = False,
) -> str | None:
    failed = failure_marker(transcript)
    if failed is not None:
        return f"guest emitted failure marker {failed!r}"
    boot = exact_line_position(transcript, BOOT_MARKER)
    test = exact_line_position(transcript, TEST_MARKER)
    if boot < 0:
        return f"missing {BOOT_MARKER} marker"
    if test < 0 and not expect_dhcp_expiry:
        return f"missing {TEST_MARKER} marker"
    if test >= 0 and test < boot:
        return f"{TEST_MARKER} appeared before {BOOT_MARKER}"
    if not expect_dhcp_expiry and exact_line_position(
            transcript, SHELL_PROMPT, after=test) < 0:
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
        network_stats = exact_line_position(transcript,
                                            REIST_NETWORK_STATS_MARKER)
        arp_validation = exact_line_position(transcript,
                                             REIST_ARP_VALIDATION_MARKER)
        arp_identity = exact_line_position(transcript,
                                           REIST_ARP_IDENTITY_MARKER)
        if (arp_identity < completion or arp_validation < arp_identity or
                network_stats < arp_validation or service < network_stats or
                network < service or network > test):
            return "missing ordered REIST network-parser marker"
    if expect_network_handoff:
        handoff = exact_line_position(transcript,
                                      REIST_NETWORK_HANDOFF_MARKER)
        probe_id = exact_line_position(transcript,
                                       REIST_NETWORK_PROBE_ID_MARKER)
        arp_binding = exact_line_position(transcript,
                                          REIST_ARP_BINDING_MARKER)
        if (probe_id < 0 or arp_binding < probe_id or handoff < arp_binding or
                handoff > test):
            return "missing ordered real NIC network-handoff marker"
        crash = exact_line_position(transcript, REIST_NETWORK_CRASH_MARKER)
        revoked = exact_line_position(transcript, REIST_ARP_REVOKED_MARKER)
        pressure_fallback = exact_line_position(
            transcript, REIST_NETWORK_PRESSURE_FALLBACK_MARKER)
        pressure = exact_line_position(transcript,
                                       REIST_NETWORK_PRESSURE_MARKER)
        recovery = exact_line_position(transcript,
                                       REIST_NETWORK_RECOVERY_MARKER)
        if (pressure_fallback < handoff or pressure < pressure_fallback or
                revoked < pressure or crash < revoked or recovery < crash or
                recovery > test):
            return "missing ordered network-service crash recovery marker"
    if expect_arp_reply:
        arp_reply = exact_line_position(transcript, REIST_ARP_REPLY_MARKER)
        if arp_reply < boot or arp_reply > test:
            return "missing mediated ARP reply marker"
    if expect_icmp_echo:
        queued = exact_line_position(transcript,
                                     REIST_ICMP_ECHO_QUEUED_MARKER)
        replied = exact_line_position(transcript, REIST_ICMP_ECHO_MARKER)
        if queued < boot or replied < queued or replied > test:
            return "missing ordered mediated ICMP echo markers"
    if expect_dhcp_config:
        queued = exact_line_position(transcript,
                                     REIST_DHCP_CONFIG_QUEUED_MARKER)
        committed = exact_line_position(transcript, REIST_DHCP_CONFIG_MARKER)
        if queued < 0 or committed < queued or committed > boot:
            return "missing ordered pre-boot mediated DHCP configuration"
    if expect_dhcp_expiry:
        committed = exact_line_position(transcript, REIST_DHCP_CONFIG_MARKER)
        expired = exact_line_position(transcript,
                                      REIST_DHCP_LEASE_EXPIRED_MARKER)
        prompt = exact_line_position(transcript, SHELL_PROMPT)
        if committed < 0 or boot < committed or prompt < boot or expired < prompt:
            return "missing ordered fail-closed DHCP lease expiry"
    if expect_udp_echo:
        queued = exact_line_position(transcript,
                                     REIST_UDP_ECHO_QUEUED_MARKER)
        replied = exact_line_position(transcript, REIST_UDP_ECHO_MARKER)
        if queued < boot or replied < queued or replied > test:
            return "missing ordered mediated UDP echo markers"
    if expect_udp_binding:
        queued = exact_line_position(transcript,
                                     REIST_UDP_DATAGRAM_QUEUED_MARKER)
        replied = exact_line_position(transcript, REIST_UDP_DATAGRAM_MARKER)
        if queued < boot or replied < queued or replied > test:
            return "missing ordered mediated UDP binding markers"
    if expect_storage_recovery:
        crash = exact_line_position(transcript, REIST_STORAGE_CRASH_MARKER)
        failure = exact_line_position(transcript, REIST_STORAGE_FAILURE_MARKER)
        restarted = exact_line_position(transcript,
                                        REIST_STORAGE_RESTARTED_MARKER)
        ready = exact_line_position(transcript, REIST_STORAGE_READY_MARKER,
                                    after=restarted)
        recovered = exact_line_position(transcript,
                                        REIST_STORAGE_RECOVERY_MARKER)
        positions = [crash, failure, restarted, ready, recovered]
        if any(position < 0 for position in positions):
            return "missing storage-service crash/recovery marker"
        if positions != sorted(positions):
            return "storage-service crash/recovery markers are out of order"
    if expect_storage_io_failure:
        positions = [exact_line_position(transcript, marker) for marker in (
            REIST_STORAGE_IO_INJECTION_MARKER,
            REIST_STORAGE_QUARANTINE_MARKER,
            REIST_STORAGE_IO_RECOVERY_MARKER,
        )]
        if any(position < 0 for position in positions):
            return "missing storage I/O-failure/quarantine marker"
        if positions != sorted(positions):
            return "storage I/O-failure/quarantine markers are out of order"
    if expect_storage_self_test:
        ready = exact_line_position(transcript, REIST_STORAGE_READY_MARKER,
                                    after=boot)
        self_test = exact_line_position(transcript,
                                        REIST_STORAGE_SELF_TEST_MARKER,
                                        after=ready)
        if ready < boot or self_test < ready or self_test > test:
            return "missing ordered storage-service post-recovery self-test"
    if expect_handover:
        positions = [exact_line_position(transcript, marker)
                     for marker in REIST_HANDOVER_MARKERS]
        if any(position < 0 for position in positions):
            return "missing external handover marker"
        if positions != sorted(positions) or positions[-1] > boot:
            return "external handover markers are out of order"
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
        "--inject-arp-request",
        action="store_true",
        help="inject one bounded Ethernet ARP request through a QEMU socket hub",
    )
    parser.add_argument(
        "--inject-icmp-echo",
        action="store_true",
        help="inject and verify one bounded Ring-3-mediated ICMP echo",
    )
    parser.add_argument(
        "--inject-udp-echo",
        action="store_true",
        help="inject and verify one bounded Ring-3-mediated UDP echo",
    )
    parser.add_argument(
        "--udp-port", type=int, default=9000,
        help="destination port for the bounded UDP injection (default 9000)",
    )
    parser.add_argument(
        "--expect-dhcp-config",
        action="store_true",
        help="require a Ring-3-mediated DHCP configuration before boot",
    )
    parser.add_argument(
        "--expect-dhcp-expiry",
        action="store_true",
        help="require bounded fail-closed DHCP lease withdrawal after boot",
    )
    parser.add_argument(
        "--expect-arp-resolution", action="store_true",
        help="trigger PING and require a mediated outgoing ARP request",
    )
    parser.add_argument(
        "--expect-storage-recovery", action="store_true",
        help="require an injected storage-service crash and bounded recovery",
    )
    parser.add_argument(
        "--expect-storage-io-failure", action="store_true",
        help="require an injected storage I/O failure and quarantine",
    )
    parser.add_argument(
        "--expect-storage-self-test", action="store_true",
        help="require storage-service bind and media self-test before PASS",
    )
    parser.add_argument(
        "--expect-handover", action="store_true",
        help="serve COM2 fence readback and require bounded takeover markers",
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
    if args.udp_port < 1024 or args.udp_port > 65535:
        print("guest-smoke: UDP port must be in 1024..65535", file=sys.stderr)
        return 2
    if re.fullmatch(r"[1-9][0-9]*[KMG]", args.memory,
                    flags=re.IGNORECASE) is None:
        print("guest-smoke: memory must look like 64M or 1G", file=sys.stderr)
        return 2
    if (args.inject_arp_request or args.expect_arp_resolution or
            args.inject_icmp_echo or args.inject_udp_echo) and args.nic == "none":
        print("guest-smoke: network injection verification requires a NIC",
              file=sys.stderr)
        return 2
    if (args.expect_dhcp_config or args.expect_dhcp_expiry) and args.nic == "none":
        print("guest-smoke: DHCP mediation requires a NIC", file=sys.stderr)
        return 2

    try:
        status, transcript, process_error = run(
            args.qemu, args.image.resolve(), args.timeout, args.no_apic,
            args.memory, args.watchdog, args.expect_fatal_recovery, args.nic,
            args.persistent, args.expect_reist_probe,
            args.inject_arp_request,
            args.expect_arp_resolution,
            args.expect_handover,
            args.inject_icmp_echo,
            args.inject_udp_echo,
            args.expect_dhcp_expiry,
            args.udp_port,
        )
    except OSError as error:
        print(f"guest-smoke: unable to start QEMU: {error}", file=sys.stderr)
        return 2

    if args.log:
        args.log.parent.mkdir(parents=True, exist_ok=True)
        args.log.write_text(transcript, encoding="utf-8")

    marker_error = validate(transcript, args.expect_fatal_recovery,
                            args.expect_reist_probe,
                            args.expect_network_handoff,
                            args.inject_arp_request,
                            args.expect_storage_recovery,
                            args.expect_storage_io_failure,
                            args.expect_storage_self_test,
                            args.expect_handover,
                            args.inject_icmp_echo,
                            args.expect_dhcp_config,
                            args.expect_dhcp_expiry,
                            args.inject_udp_echo and args.udp_port == 9000,
                            args.inject_udp_echo and args.udp_port != 9000)
    if marker_error is None and process_error is None:
        print(transcript, end="" if transcript.endswith("\n") else "\n")
        print("guest-smoke: PASS")
        return 0

    print(transcript, end="" if transcript.endswith("\n") else "\n",
          file=sys.stderr)
    detail = process_error or marker_error
    if not args.expect_dhcp_expiry and TEST_MARKER not in str(detail):
        detail = f"{detail}; missing {TEST_MARKER} marker"
    print(f"guest-smoke: FAIL: {detail}", file=sys.stderr)
    return status or 1


if __name__ == "__main__":
    raise SystemExit(main())
