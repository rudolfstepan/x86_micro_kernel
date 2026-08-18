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
REIST_NETWORK_SERVICE_READY_MARKER = "REIST_NETWORK SERVICE_READY"
REIST_MEMORY_FAULT_MARKER = "REIST_MEMORY_FAULT_INJECTION_OK"
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
REIST_ARP_RESOLUTION_QUEUED_MARKER = "REIST_NETWORK ARP_RESOLUTION_QUEUED"
REIST_ARP_RESOLUTION_MARKER = "REIST_NETWORK ARP_RESOLUTION_MEDIATED"
REIST_ICMP_ECHO_QUEUED_MARKER = "REIST_NETWORK ICMP_ECHO_QUEUED"
REIST_ICMP_ECHO_MARKER = "REIST_NETWORK ICMP_ECHO_MEDIATED"
REIST_DHCP_CONFIG_QUEUED_MARKER = "REIST_NETWORK DHCP_CONFIG_QUEUED"
REIST_DHCP_CONFIG_MARKER = "REIST_NETWORK DHCP_CONFIG_MEDIATED"
REIST_DHCP_BOOT_DISCOVER_MARKER = "REIST_NETWORK DHCP_BOOT_DISCOVER_RING3"
REIST_DHCP_BOOT_OFFER_MARKER = "REIST_NETWORK DHCP_BOOT_OFFER_RING3"
REIST_DHCP_BOOT_ACK_MARKER = "REIST_NETWORK DHCP_BOOT_ACK_RING3"
REIST_DHCP_LEASE_EXPIRED_MARKER = "REIST_NETWORK DHCP_LEASE_EXPIRED"
REIST_DHCP_RENEWED_MARKER = "REIST_NETWORK DHCP_RENEWED"
REIST_DHCP_RENEW_REQUESTED_MARKER = "REIST_NETWORK DHCP_RENEW_REQUESTED"
REIST_DHCP_RENEW_INGRESS_RING3_MARKER = \
    "REIST_NETWORK DHCP_RENEW_INGRESS_RING3"
REIST_NETWORK_FRAME_MARKER = "REIST_NETWORK FRAME_HANDOFF"
REIST_NETWORK_IPV4_MARKER = "REIST_NETWORK IPV4_PARSED_RING3"
REIST_NETWORK_ICMP_MARKER = "REIST_NETWORK ICMP_PARSED_RING3"
REIST_NETWORK_UDP_MARKER = "REIST_NETWORK UDP_PARSED_RING3"
REIST_NETWORK_DHCP_MARKER = "REIST_NETWORK DHCP_PARSED_RING3"
REIST_NETWORK_UDP_INGRESS_MARKER = "REIST_NETWORK UDP_INGRESS_RING3"
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
REIST_STORAGE_REINTEGRATION_MARKER = "REIST_STORAGE RESOURCE_REINTEGRATED_RW 0"
REIST_STORAGE_IO_RECOVERY_MARKER = "TEST_STAGE STORAGE_MEDIA_REINTEGRATED_OK"
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
PS2_GUEST_COMMAND = "gtest"
OUTBOUND_PING_COMMAND = "ping 10.0.2.98"
OUTBOUND_PING_REPLY_MARKER = "reply: received"
OUTBOUND_PING_TARGET = bytes((10, 0, 2, 98))
OUTBOUND_PING_MAC = bytes((0x02, 0xCA, 0xFE, 0x00, 0x00, 0x03))
TCP_TEST_COMMAND = "nc 10.0.2.99 8080 ping"
TCP_TEST_TARGET = bytes((10, 0, 2, 99))
TCP_TEST_MAC = bytes((0x02, 0xCA, 0xFE, 0x00, 0x00, 0x04))
TCP_TEST_REPLY_MARKER = "pong"
DNS_TEST_COMMAND = "nslookup test.reist 10.0.2.99"
DNS_TEST_REPLY_MARKER = "address: 10.0.2.77"
GUEST_IP = bytes((10, 0, 2, 15))
GUEST_MAC = bytes((0x52, 0x54, 0x00, 0x12, 0x34, 0x56))
QEMU_MUX_SWITCH = "\x01c"
KEY_INTERVAL_SECONDS = 0.075
FAIL_MARKERS = (
    "TEST_FAIL",
    "PANIC:",
    "KERNEL ASSERTION FAILED",
    "Kernel exception:",
    "Unable to start SHELL.PRG",
    "REIST_NETWORK ARP_REPLY_REJECTED",
    "REIST_NETWORK ICMP_ECHO_REJECTED",
    "REIST_NETWORK DHCP_CONFIG_REJECTED",
    "REIST_NETWORK DHCP_RENEWAL_REJECTED",
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
    sata: bool = False,
    auxiliary_sata_image: Path | None = None,
) -> list[str]:
    command = [
        str(qemu),
        "-accel", "tcg",
        "-machine", "pc",
        "-nodefaults",
        "-m", memory,
        "-boot", "c",
        "-display", "none",
        "-monitor", "none",
        "-serial", "mon:stdio",
        "-no-shutdown",
    ]
    if sata:
        command.extend(["-device", "ich9-ahci,id=reistahci"])
        system_port = 0
        if auxiliary_sata_image is not None:
            system_port = 1
            command.extend([
                "-drive", (f"file={auxiliary_sata_image},format=raw,"
                           "if=none,id=reistauxdisk"),
                "-device", ("ide-hd,drive=reistauxdisk,bus=reistahci.0,"
                            "bootindex=2"),
            ])
        command.extend([
            "-drive", f"file={image},format=raw,if=none,id=reistdisk",
            "-device", (f"ide-hd,drive=reistdisk,bus=reistahci.{system_port},"
                        "bootindex=1"),
        ])
    else:
        command.extend([
            "-drive", f"file={image},format=raw,if=ide,index=0,media=disk",
        ])
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


def monitor_key_commands(text: str) -> list[str]:
    commands: list[str] = []
    for character in text:
        if "a" <= character <= "z" or "0" <= character <= "9":
            key = character
        elif character == " ":
            key = "spc"
        elif character == ".":
            key = "dot"
        else:
            raise ValueError("unsupported PS/2 guest command character")
        commands.append(f"sendkey {key}\n")
    commands.append("sendkey ret\n")
    return commands


def inject_ps2_command(process: subprocess.Popen[str], text: str) -> None:
    """Use QEMU's bounded monitor sendkey path; COM1 remains output-only."""
    if process.stdin is None:
        raise RuntimeError("QEMU multiplexed monitor input unavailable")
    process.stdin.write(QEMU_MUX_SWITCH)
    process.stdin.flush()
    time.sleep(KEY_INTERVAL_SECONDS)
    try:
        for command in monitor_key_commands(text):
            process.stdin.write(command)
            process.stdin.flush()
            time.sleep(KEY_INTERVAL_SECONDS)
    finally:
        process.stdin.write(QEMU_MUX_SWITCH)
        process.stdin.flush()


def inject_ps2_key(process: subprocess.Popen[str], key: str) -> None:
    if process.stdin is None:
        raise RuntimeError("QEMU multiplexed monitor input unavailable")
    process.stdin.write(QEMU_MUX_SWITCH)
    process.stdin.flush()
    time.sleep(KEY_INTERVAL_SECONDS)
    try:
        process.stdin.write(f"sendkey {key}\n")
        process.stdin.flush()
        time.sleep(KEY_INTERVAL_SECONDS)
    finally:
        process.stdin.write(QEMU_MUX_SWITCH)
        process.stdin.flush()


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


def outbound_ping_arp_reply_frame() -> bytes:
    frame = b"".join((
        GUEST_MAC, OUTBOUND_PING_MAC, b"\x08\x06",
        b"\x00\x01", b"\x08\x00", b"\x06\x04", b"\x00\x02",
        OUTBOUND_PING_MAC, OUTBOUND_PING_TARGET, GUEST_MAC, GUEST_IP,
    ))
    return frame.ljust(60, b"\x00")


def tcp_test_arp_reply_frame() -> bytes:
    frame = b"".join((
        GUEST_MAC, TCP_TEST_MAC, b"\x08\x06",
        b"\x00\x01", b"\x08\x00", b"\x06\x04", b"\x00\x02",
        TCP_TEST_MAC, TCP_TEST_TARGET, GUEST_MAC, GUEST_IP,
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


def tcp_test_frame(source_port: int, destination_port: int, sequence: int,
                   acknowledgement: int, flags: int,
                   payload: bytes = b"") -> bytes:
    tcp = struct.pack(
        "!HHIIBBHHH", source_port, destination_port, sequence,
        acknowledgement, 5 << 4, flags, 4096, 0, 0,
    ) + payload
    pseudo = TCP_TEST_TARGET + GUEST_IP + struct.pack("!BBH", 0, 6, len(tcp))
    checksum = internet_checksum(pseudo + tcp)
    tcp = tcp[:16] + struct.pack("!H", checksum) + tcp[18:]
    total_length = 20 + len(tcp)
    ip = struct.pack(
        "!BBHHHBBH4s4s", 0x45, 0, total_length, 0x5443, 0,
        64, 6, 0, TCP_TEST_TARGET, GUEST_IP,
    )
    ip = ip[:10] + struct.pack("!H", internet_checksum(ip)) + ip[12:]
    return (GUEST_MAC + TCP_TEST_MAC + b"\x08\x00" + ip + tcp).ljust(
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


def receive_arp_reply(connection: socket.socket, deadline: float) -> bool:
    peer_mac = bytes((0x02, 0xCA, 0xFE, 0x00, 0x00, 0x01))
    peer_ip = bytes((10, 0, 2, 99))
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
        if (len(frame) >= 42 and frame[0:6] == peer_mac and
                frame[6:12] == GUEST_MAC and frame[12:14] == b"\x08\x06" and
                frame[14:22] == b"\x00\x01\x08\x00\x06\x04\x00\x02" and
                frame[22:28] == GUEST_MAC and frame[28:32] == GUEST_IP and
                frame[32:38] == peer_mac and frame[38:42] == peer_ip):
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


def receive_icmp_echo_request(
    connection: socket.socket, deadline: float,
) -> tuple[int, int, bytes] | None:
    while time.monotonic() < deadline:
        connection.settimeout(max(0.01, deadline - time.monotonic()))
        header = receive_exact(connection, 4)
        if header is None:
            return None
        length = struct.unpack("!I", header)[0]
        if length < 14 or length > 1514:
            return None
        frame = receive_exact(connection, length)
        if frame is None or len(frame) < 42:
            return None
        if (frame[0:6] != OUTBOUND_PING_MAC or
                frame[12:14] != b"\x08\x00" or frame[23] != 1 or
                frame[26:30] != GUEST_IP or
                frame[30:34] != OUTBOUND_PING_TARGET):
            continue
        ihl = (frame[14] & 0x0F) * 4
        ip_total = struct.unpack("!H", frame[16:18])[0]
        if ihl < 20 or ip_total < ihl + 8 or len(frame) < 14 + ip_total:
            continue
        icmp = frame[14 + ihl:14 + ip_total]
        if icmp[0:2] != b"\x08\x00" or internet_checksum(icmp) != 0:
            continue
        identifier, sequence = struct.unpack("!HH", icmp[4:8])
        return identifier, sequence, icmp[8:]
    return None


def outbound_ping_icmp_reply_frame(
    identifier: int, sequence: int, payload: bytes,
) -> bytes:
    icmp = struct.pack("!BBHHH", 0, 0, 0, identifier, sequence) + payload
    icmp = icmp[:2] + struct.pack("!H", internet_checksum(icmp)) + icmp[4:]
    total_length = 20 + len(icmp)
    ip = struct.pack(
        "!BBHHHBBH4s4s", 0x45, 0, total_length, 0x5049, 0,
        64, 1, 0, OUTBOUND_PING_TARGET, GUEST_IP,
    )
    ip = ip[:10] + struct.pack("!H", internet_checksum(ip)) + ip[12:]
    return (GUEST_MAC + OUTBOUND_PING_MAC + b"\x08\x00" + ip + icmp).ljust(
        60, b"\x00")


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


def receive_tcp_segment(
    connection: socket.socket, deadline: float,
) -> tuple[int, int, int, int, int, bytes] | None:
    while time.monotonic() < deadline:
        connection.settimeout(max(0.01, deadline - time.monotonic()))
        header = receive_exact(connection, 4)
        if header is None:
            return None
        length = struct.unpack("!I", header)[0]
        if length > 1514:
            return None
        if length < 54:
            _ = receive_exact(connection, length)
            continue
        frame = receive_exact(connection, length)
        if frame is None:
            return None
        if (len(frame) >= 42 and frame[12:14] == b"\x08\x06" and
                frame[20:22] == b"\x00\x01" and
                frame[38:42] == TCP_TEST_TARGET):
            if not inject_ethernet_frame(connection, tcp_test_arp_reply_frame()):
                return None
            continue
        if frame[12:14] != b"\x08\x00" or frame[23] != 6:
            continue
        ihl = (frame[14] & 0x0F) * 4
        total = struct.unpack("!H", frame[16:18])[0]
        if (ihl < 20 or total < ihl + 20 or len(frame) < 14 + total or
                frame[26:30] != GUEST_IP or frame[30:34] != TCP_TEST_TARGET):
            continue
        tcp = frame[14 + ihl:14 + total]
        header_length = (tcp[12] >> 4) * 4
        if header_length < 20 or header_length > len(tcp):
            continue
        pseudo = GUEST_IP + TCP_TEST_TARGET + struct.pack("!BBH", 0, 6,
                                                           len(tcp))
        if internet_checksum(pseudo + tcp) != 0:
            continue
        source, destination, sequence, acknowledgement = struct.unpack(
            "!HHII", tcp[:12])
        return (source, destination, sequence, acknowledgement, tcp[13],
                tcp[header_length:])
    return None


def serve_tcp_test_client(connection: socket.socket, deadline: float) -> str | None:
    syn = receive_tcp_segment(connection, deadline)
    if syn is None or syn[4] != 0x02 or syn[5] != b"":
        return "valid TCP SYN was not observed"
    client_port, server_port, client_sequence = syn[0], syn[1], syn[2]
    server_sequence = 7000
    if server_port != 8080 or not inject_ethernet_frame(
            connection, tcp_test_frame(server_port, client_port,
                                       server_sequence, client_sequence + 1,
                                       0x12)):
        return "unable to inject TCP SYN/ACK"
    payload_segment = None
    for _ in range(3):
        segment = receive_tcp_segment(connection, deadline)
        if segment is None:
            break
        if segment[5]:
            payload_segment = segment
            break
    if payload_segment is None or payload_segment[5] != b"ping":
        return "TCP client payload was not observed"
    client_next = payload_segment[2] + len(payload_segment[5])
    server_next = server_sequence + 1
    if not inject_ethernet_frame(
            connection, tcp_test_frame(server_port, client_port, server_next,
                                       client_next, 0x18, b"pong")):
        return "unable to inject TCP response"
    server_next += 4
    fin = None
    for _ in range(4):
        segment = receive_tcp_segment(connection, deadline)
        if segment is None:
            break
        if segment[4] & 0x01:
            fin = segment
            break
    if fin is None:
        return "TCP client FIN was not observed"
    if not inject_ethernet_frame(
            connection, tcp_test_frame(server_port, client_port, server_next,
                                       fin[2] + len(fin[5]) + 1, 0x11)):
        return "unable to inject TCP FIN/ACK"
    final_ack = None
    for _ in range(4):
        segment = receive_tcp_segment(connection, deadline)
        if (segment is not None and (segment[4] & 0x10) != 0 and
                segment[3] == server_next + 1):
            final_ack = segment
            break
    if final_ack is None:
        return "final TCP ACK was not observed"
    return None


def serve_dns_test_client(connection: socket.socket, deadline: float) -> str | None:
    """Answer one bounded DNS A query from the guest through the socket hub."""
    while time.monotonic() < deadline:
        connection.settimeout(max(0.01, deadline - time.monotonic()))
        header = receive_exact(connection, 4)
        if header is None:
            return "DNS peer frame header was not observed"
        length = struct.unpack("!I", header)[0]
        if length > 1514:
            return "DNS peer emitted an oversized frame"
        frame = receive_exact(connection, length)
        if frame is None:
            return "DNS peer frame was truncated"
        if (len(frame) >= 42 and frame[12:14] == b"\x08\x06" and
                frame[20:22] == b"\x00\x01" and
                frame[38:42] == TCP_TEST_TARGET):
            if not inject_ethernet_frame(connection, tcp_test_arp_reply_frame()):
                return "unable to inject DNS peer ARP reply"
            continue
        if len(frame) < 42 or frame[12:14] != b"\x08\x00":
            continue
        if frame[23] != 17:
            continue
        if frame[26:30] != GUEST_IP or frame[30:34] != TCP_TEST_TARGET:
            continue
        ihl = (frame[14] & 0x0f) * 4
        total = struct.unpack("!H", frame[16:18])[0]
        if ihl < 20 or total < ihl + 20 or len(frame) < 14 + total:
            continue
        udp = frame[14 + ihl:14 + total]
        source_port, destination_port, udp_length, _ = struct.unpack(
            "!HHHH", udp[:8])
        query = udp[8:udp_length]
        if destination_port != 53 or len(query) < 18:
            continue
        question_end = 12
        while question_end < len(query) and query[question_end] != 0:
            label = query[question_end]
            if label > 63 or question_end + label + 1 >= len(query):
                return "guest DNS question is malformed"
            question_end += label + 1
        question_end += 5
        if question_end > len(query):
            return "guest DNS question is truncated"
        answer = (query[:2] + b"\x81\x80\x00\x01\x00\x01\x00\x00\x00\x00" +
                  query[12:question_end] +
                  b"\xc0\x0c\x00\x01\x00\x01\x00\x00\x00\x3c\x00\x04" +
                  bytes((10, 0, 2, 77)))
        response_udp = struct.pack("!HHHH", 53, source_port,
                                   8 + len(answer), 0) + answer
        pseudo = TCP_TEST_TARGET + GUEST_IP + struct.pack(
            "!BBH", 0, 17, len(response_udp))
        checksum = internet_checksum(pseudo + response_udp)
        response_udp = response_udp[:6] + struct.pack(
            "!H", checksum or 0xffff) + response_udp[8:]
        ip = struct.pack("!BBHHHBBH4s4s", 0x45, 0, 20 + len(response_udp),
                         0x444e, 0, 64, 17, 0, TCP_TEST_TARGET, GUEST_IP)
        ip = ip[:10] + struct.pack("!H", internet_checksum(ip)) + ip[12:]
        response = (GUEST_MAC + TCP_TEST_MAC + b"\x08\x00" + ip +
                    response_udp).ljust(60, b"\x00")
        return None if inject_ethernet_frame(connection, response) else \
            "unable to inject DNS response"
    return "guest DNS query was not observed"


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


def exact_line_after_prompt_position(
    text: str, expected: str, after: int = -1
) -> int:
    position = exact_line_position(text, expected, after)
    if position >= 0:
        return position
    pattern = re.compile(
        rf"(?:^|\n){re.escape(SHELL_PROMPT + expected)}\r?(?=\n|$)"
    )
    for match in pattern.finditer(text):
        position = match.start() + (
            1 if text[match.start():].startswith("\n") else 0
        ) + len(SHELL_PROMPT)
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
    expect_dhcp_renewal: bool = False,
    udp_port: int = 9000,
    sata: bool = False,
    auxiliary_sata_image: Path | None = None,
    expect_outbound_ping: bool = False,
    expect_tcp_client: bool = False,
    expect_dns_client: bool = False,
    boot_only: bool = False,
) -> tuple[int, str, str | None]:
    injection_listener: socket.socket | None = None
    injection_connection: socket.socket | None = None
    injection_port: int | None = None
    handover_listener: socket.socket | None = None
    handover_connection: socket.socket | None = None
    handover_port: int | None = None
    handover_thread: threading.Thread | None = None
    handover_result: list[str | None] = [None]
    if (inject_arp_request or expect_arp_resolution or expect_outbound_ping or
            inject_icmp_echo or inject_udp_echo or expect_tcp_client or
            expect_dns_client):
        injection_listener, injection_port = open_injection_listener()
    if expect_handover:
        handover_listener, handover_port = open_injection_listener()
    try:
        process = subprocess.Popen(
            qemu_command(qemu, image, no_apic, memory, watchdog, allow_reboot,
                         nic, persistent, injection_port, handover_port, sata,
                         auxiliary_sata_image),
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
        elif error is None and expect_dhcp_renewal:
            error, _ = wait_for_line(
                process, chunks, transcript, finished,
                REIST_DHCP_RENEWED_MARKER, deadline,
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
                        error = "QEMU exited immediately after DHCP renewal"
                        break
                    time.sleep(0.02)
        elif error is None and boot_only:
            pass
        elif error is None:
            inject_ps2_command(process, PS2_GUEST_COMMAND)
            if inject_arp_request or inject_icmp_echo or inject_udp_echo:
                error, _ = wait_for_line(
                    process, chunks, transcript, finished,
                    REIST_NETWORK_INJECTION_READY_MARKER, deadline,
                )
            if error is None and (inject_icmp_echo or inject_udp_echo):
                # These injected IPv4 frames target the DHCP-assigned guest
                # address.  Do not race the mediated DHCP commit: a faster
                # Ring-0 fallback must never be required to make this test
                # pass.
                error, _ = wait_for_line(
                    process, chunks, transcript, finished,
                    REIST_DHCP_CONFIG_MARKER, deadline,
                )
            if error is None and inject_arp_request:
                assert injection_port is not None
                assert injection_connection is not None
                replied = False
                for _ in range(3):
                    if not inject_ethernet_frame(
                            injection_connection, arp_request_frame()):
                        error = "unable to inject bounded ARP request"
                        break
                    confirmation_deadline = min(deadline,
                                                time.monotonic() + 1.0)
                    if receive_arp_reply(injection_connection,
                                         confirmation_deadline):
                        replied = True
                        break
                if error is None and not replied:
                    error = "ARP reply not received after 3 bounded attempts"
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
            shell_position = -1
            if error is None:
                error, shell_position = wait_for_line(
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
            if error is None and expect_outbound_ping:
                assert injection_connection is not None
                inject_ps2_command(process, OUTBOUND_PING_COMMAND)
                if not receive_arp_request(
                        injection_connection, OUTBOUND_PING_TARGET, deadline):
                    error = "outbound PING ARP request was not observed"
                if error is None and not inject_ethernet_frame(
                        injection_connection, outbound_ping_arp_reply_frame()):
                    error = "unable to inject outbound PING ARP reply"
                if error is None:
                    error, _ = wait_for_line(
                        process, chunks, transcript, finished,
                        REIST_ARP_RESOLUTION_MARKER, deadline,
                        after=shell_position,
                    )
                if error is None:
                    error, _ = wait_for_line(
                        process, chunks, transcript, finished,
                        REIST_ARP_BINDING_MARKER, deadline,
                        after=shell_position,
                    )
                echo = (None if error is not None else
                        receive_icmp_echo_request(injection_connection,
                                                  deadline))
                if error is None and echo is None:
                    error = "outbound ICMP echo request was not observed"
                if error is None and echo is not None and \
                        not inject_ethernet_frame(
                            injection_connection,
                            outbound_ping_icmp_reply_frame(*echo)):
                    error = "unable to inject outbound PING echo reply"
                ping_position = -1
                if error is None:
                    error, ping_position = wait_for_line(
                        process, chunks, transcript, finished,
                        OUTBOUND_PING_REPLY_MARKER, deadline,
                        after=shell_position,
                    )
                if error is None:
                    inject_ps2_key(process, "ctrl-c")
                    error, prompt_after_ping = wait_for_line(
                        process, chunks, transcript, finished, SHELL_PROMPT,
                        deadline, after=ping_position,
                    )
                if error is None:
                    inject_ps2_key(process, "ctrl-c")
                    error, prompt_after_cancel = wait_for_line(
                        process, chunks, transcript, finished, SHELL_PROMPT,
                        deadline, after=prompt_after_ping,
                    )
                    if error is None:
                        drain(chunks, transcript)
                        cancelled = "".join(transcript)[
                            prompt_after_ping:prompt_after_cancel]
                        if "^C" not in cancelled:
                            error = "shell prompt returned without Ctrl+C acknowledgement"
            if error is None and expect_tcp_client:
                assert injection_connection is not None
                inject_ps2_command(process, TCP_TEST_COMMAND)
                if not receive_arp_request(
                        injection_connection, TCP_TEST_TARGET, deadline):
                    error = "TCP client ARP request was not observed"
                if error is None and not inject_ethernet_frame(
                        injection_connection, tcp_test_arp_reply_frame()):
                    error = "unable to inject TCP peer ARP reply"
                if error is None:
                    error = serve_tcp_test_client(injection_connection,
                                                  deadline)
                tcp_position = -1
                if error is None:
                    error, tcp_position = wait_for_line(
                        process, chunks, transcript, finished,
                        TCP_TEST_REPLY_MARKER, deadline, after=shell_position)
                if error is None:
                    error, _ = wait_for_line(
                        process, chunks, transcript, finished, SHELL_PROMPT,
                        deadline, after=tcp_position)
            if error is None and expect_dns_client:
                assert injection_connection is not None
                inject_ps2_command(process, DNS_TEST_COMMAND)
                error = serve_dns_test_client(injection_connection, deadline)
                dns_position = -1
                if error is None:
                    error, dns_position = wait_for_line(
                        process, chunks, transcript, finished,
                        DNS_TEST_REPLY_MARKER, deadline, after=shell_position)
                if error is None:
                    error, _ = wait_for_line(
                        process, chunks, transcript, finished, SHELL_PROMPT,
                        deadline, after=dns_position)
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
    expect_dhcp_renewal: bool = False,
    expect_udp_echo: bool = False,
    expect_udp_binding: bool = False,
    expect_network_frame: bool = False,
    expect_network_ipv4: bool = False,
    expect_network_icmp: bool = False,
    expect_network_udp: bool = False,
    expect_network_dhcp: bool = False,
    expect_network_udp_ingress: bool = False,
    expect_memory_fault: bool = False,
    boot_only: bool = False,
) -> str | None:
    failed = failure_marker(transcript)
    if failed is not None:
        return f"guest emitted failure marker {failed!r}"
    boot = exact_line_position(transcript, BOOT_MARKER)
    test = exact_line_position(transcript, TEST_MARKER)
    if boot < 0:
        return f"missing {BOOT_MARKER} marker"
    if test < 0 and not (expect_dhcp_expiry or expect_dhcp_renewal or
                         boot_only):
        return f"missing {TEST_MARKER} marker"
    if test >= 0 and test < boot:
        return f"{TEST_MARKER} appeared before {BOOT_MARKER}"
    if boot_only:
        if transcript.find(SHELL_PROMPT, boot) < 0:
            return f"missing {SHELL_PROMPT} prompt after {BOOT_MARKER}"
        if expect_reist_probe:
            completion = exact_line_position(
                transcript, REIST_PROBE_COMPLETION_MARKER)
            service_ready = exact_line_position(
                transcript, REIST_NETWORK_SERVICE_READY_MARKER)
            if completion < 0:
                return "missing cumulative REIST probe recovery marker"
            if service_ready < completion or service_ready > boot:
                return "missing pre-boot REIST network service-ready marker"
        return None
    if expect_memory_fault:
        memory_fault = exact_line_position(transcript,
                                           REIST_MEMORY_FAULT_MARKER)
        if memory_fault < 0 or memory_fault > boot:
            return "missing pre-boot memory fault-injection marker"
    if not (expect_dhcp_expiry or expect_dhcp_renewal) and exact_line_position(
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
        service_ready = exact_line_position(
            transcript, REIST_NETWORK_SERVICE_READY_MARKER)
        if completion < 0 or completion > boot:
            return "missing cumulative REIST probe recovery marker"
        if service_ready < completion or service_ready > boot:
            return "missing pre-boot REIST network service-ready marker"
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
    if expect_icmp_echo:
        queued = exact_line_position(transcript,
                                     REIST_ICMP_ECHO_QUEUED_MARKER)
        replied = exact_line_position(transcript, REIST_ICMP_ECHO_MARKER)
        if queued < boot or replied < queued or replied > test:
            return "missing ordered mediated ICMP echo markers"
    if expect_dhcp_config:
        discover = exact_line_position(
            transcript, REIST_DHCP_BOOT_DISCOVER_MARKER)
        offer = exact_line_position(transcript, REIST_DHCP_BOOT_OFFER_MARKER)
        ack = exact_line_position(transcript, REIST_DHCP_BOOT_ACK_MARKER)
        queued = exact_line_position(transcript,
                                     REIST_DHCP_CONFIG_QUEUED_MARKER)
        committed = exact_line_position(transcript, REIST_DHCP_CONFIG_MARKER)
        if (discover < 0 or offer < discover or ack < offer or
                queued < ack or committed < queued or committed > boot):
            return "missing ordered Ring-3 pre-boot DHCP configuration"
    if expect_dhcp_expiry:
        committed = exact_line_position(transcript, REIST_DHCP_CONFIG_MARKER)
        expired = exact_line_position(transcript,
                                      REIST_DHCP_LEASE_EXPIRED_MARKER)
        prompt = exact_line_position(transcript, SHELL_PROMPT)
        if committed < 0 or boot < committed or prompt < boot or expired < prompt:
            return "missing ordered fail-closed DHCP lease expiry"
    if expect_dhcp_renewal:
        committed = exact_line_position(transcript, REIST_DHCP_CONFIG_MARKER)
        # The prompt has no trailing newline.  A background supervisor marker
        # may therefore be appended to the same serial line after the runner
        # has already observed the complete prompt.
        prompt = transcript.find(SHELL_PROMPT, boot)
        requested = exact_line_after_prompt_position(
            transcript, REIST_DHCP_RENEW_REQUESTED_MARKER)
        ingress = exact_line_position(
            transcript, REIST_DHCP_RENEW_INGRESS_RING3_MARKER)
        renewed = exact_line_position(transcript, REIST_DHCP_RENEWED_MARKER)
        if (committed < 0 or boot < committed or prompt < boot or
                requested < prompt or ingress < requested or
                renewed < ingress):
            return "missing ordered bounded DHCP renewal"
    if expect_network_frame:
        frame = exact_line_position(transcript, REIST_NETWORK_FRAME_MARKER)
        if frame < 0 or (test >= 0 and frame > test):
            return "missing bounded Ring-3 network frame handoff"
    if expect_network_ipv4:
        ipv4 = exact_line_position(transcript, REIST_NETWORK_IPV4_MARKER)
        if ipv4 < 0 or (test >= 0 and ipv4 > test):
            return "missing bounded Ring-3 IPv4 parser marker"
    if expect_network_icmp:
        icmp = exact_line_position(transcript, REIST_NETWORK_ICMP_MARKER)
        if icmp < 0 or (test >= 0 and icmp > test):
            return "missing bounded Ring-3 ICMP parser marker"
    if expect_network_udp:
        udp = exact_line_position(transcript, REIST_NETWORK_UDP_MARKER)
        if udp < 0 or (test >= 0 and udp > test):
            return "missing bounded Ring-3 UDP parser marker"
    if expect_network_dhcp:
        dhcp = exact_line_position(transcript, REIST_NETWORK_DHCP_MARKER)
        if dhcp < 0 or (test >= 0 and dhcp > test):
            return "missing bounded Ring-3 DHCP parser marker"
    if expect_network_udp_ingress:
        ingress = exact_line_position(
            transcript, REIST_NETWORK_UDP_INGRESS_MARKER)
        if ingress < 0 or (test >= 0 and ingress > test):
            return "missing validated Ring-3 UDP ingress marker"
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
            REIST_STORAGE_REINTEGRATION_MARKER,
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
        "--expect-memory-fault",
        action="store_true",
        help="require deterministic pre-boot memory rollback evidence",
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
        "--expect-dhcp-renewal",
        action="store_true",
        help="require a bounded Ring-3 DHCP renewal after boot",
    )
    parser.add_argument(
        "--expect-network-frame",
        action="store_true",
        help="require one bounded raw network frame consumed by Ring 3",
    )
    parser.add_argument(
        "--expect-network-ipv4",
        action="store_true",
        help="require one checksum-valid IPv4 frame parsed by Ring 3",
    )
    parser.add_argument(
        "--expect-network-icmp",
        action="store_true",
        help="require one checksum-valid ICMP echo parsed by Ring 3",
    )
    parser.add_argument(
        "--expect-network-udp",
        action="store_true",
        help="require one checksum-valid UDP datagram parsed by Ring 3",
    )
    parser.add_argument(
        "--expect-network-dhcp",
        action="store_true",
        help="require one bounded DHCP reply parsed by Ring 3",
    )
    parser.add_argument(
        "--expect-network-udp-ingress",
        action="store_true",
        help="require one service-owned UDP datagram accepted from Ring 3",
    )
    parser.add_argument(
        "--expect-arp-resolution", action="store_true",
        help="trigger PING and require a mediated outgoing ARP request",
    )
    parser.add_argument(
        "--expect-outbound-ping", action="store_true",
        help="run shell PING against the QEMU gateway and keep the shell alive",
    )
    parser.add_argument(
        "--expect-tcp-client", action="store_true",
        help="run nc against a deterministic socket-hub TCP peer",
    )
    parser.add_argument(
        "--expect-dns-client", action="store_true",
        help="run nslookup against a deterministic socket-hub DNS peer",
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
    parser.add_argument(
        "--sata", action="store_true",
        help="attach the boot image through an emulated ICH9 AHCI controller",
    )
    parser.add_argument(
        "--aux-sata-image", type=Path,
        help=("attach an auxiliary SATA disk before the boot disk to verify "
              "deterministic system-volume selection"),
    )
    parser.add_argument(
        "--boot-only", action="store_true",
        help="stop successfully after BOOT_OK, the shell prompt, and requested boot markers",
    )
    args = parser.parse_args()

    if not args.image.is_file():
        print(f"guest-smoke: image not found: {args.image}", file=sys.stderr)
        return 2
    if args.aux_sata_image is not None and not args.sata:
        print("guest-smoke: --aux-sata-image requires --sata", file=sys.stderr)
        return 2
    if (args.aux_sata_image is not None and
            not args.aux_sata_image.is_file()):
        print(f"guest-smoke: auxiliary image not found: {args.aux_sata_image}",
              file=sys.stderr)
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
            args.expect_outbound_ping or
            args.inject_icmp_echo or args.inject_udp_echo or
            args.expect_tcp_client or args.expect_dns_client) and \
            args.nic == "none":
        print("guest-smoke: network injection verification requires a NIC",
              file=sys.stderr)
        return 2
    if (args.expect_dhcp_config or args.expect_dhcp_expiry or
            args.expect_dhcp_renewal or args.expect_network_frame or
            args.expect_network_ipv4 or args.expect_network_icmp or
            args.expect_network_udp or
            args.expect_network_dhcp or
            args.expect_network_udp_ingress) and \
            args.nic == "none":
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
            args.expect_dhcp_renewal,
            args.udp_port,
            args.sata,
            (args.aux_sata_image.resolve()
             if args.aux_sata_image is not None else None),
            args.expect_outbound_ping,
            args.expect_tcp_client,
            args.expect_dns_client,
            args.boot_only,
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
                            args.expect_dhcp_renewal,
                            args.inject_udp_echo and args.udp_port == 9000,
                            args.inject_udp_echo and args.udp_port != 9000,
                            args.expect_network_frame,
                            args.expect_network_ipv4,
                            args.expect_network_icmp,
                            args.expect_network_udp,
                            args.expect_network_dhcp,
                            args.expect_network_udp_ingress,
                            args.expect_memory_fault,
                            args.boot_only)
    if marker_error is None and process_error is None:
        print(transcript, end="" if transcript.endswith("\n") else "\n")
        print("guest-smoke: PASS")
        return 0

    print(transcript, end="" if transcript.endswith("\n") else "\n",
          file=sys.stderr)
    detail = process_error or marker_error
    if not (args.expect_dhcp_expiry or args.expect_dhcp_renewal or
            args.boot_only) and \
            TEST_MARKER not in str(detail):
        detail = f"{detail}; missing {TEST_MARKER} marker"
    print(f"guest-smoke: FAIL: {detail}", file=sys.stderr)
    return status or 1


if __name__ == "__main__":
    raise SystemExit(main())
