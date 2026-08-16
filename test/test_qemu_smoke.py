"""Host regressions for the deterministic QEMU guest-smoke runner.

The tests use a tiny fake QEMU executable.  They therefore exercise process
control, marker parsing and build/CI wiring without booting a virtual machine.
"""

from __future__ import annotations

import os
import importlib.util
import re
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RUNNER = ROOT / "scripts" / "run_qemu_smoke.py"
RUNNER_SPEC = importlib.util.spec_from_file_location("run_qemu_smoke", RUNNER)
RUNNER_MODULE = importlib.util.module_from_spec(RUNNER_SPEC)
assert RUNNER_SPEC.loader is not None
RUNNER_SPEC.loader.exec_module(RUNNER_MODULE)


class QemuGuestSmokeRunnerTests(unittest.TestCase):
    def test_external_handover_channel_uses_com2_and_crc_frames(self) -> None:
        command = RUNNER_MODULE.qemu_command(
            Path("qemu"), Path("image"), handover_port=32124,
        )
        serial_positions = [index for index, argument in enumerate(command)
                            if argument == "-serial"]
        self.assertEqual(len(serial_positions), 2)
        self.assertEqual(command[serial_positions[0] + 1], "mon:stdio")
        self.assertEqual(command[serial_positions[1] + 1],
                         "tcp:127.0.0.1:32124")

        request = RUNNER_MODULE.handover_frame(
            RUNNER_MODULE.HANDOVER_SERIAL_REQUEST, 1, 9)
        self.assertEqual(len(request), RUNNER_MODULE.HANDOVER_SERIAL_FRAME.size)
        self.assertEqual(RUNNER_MODULE.validate_handover_frame(
            request, RUNNER_MODULE.HANDOVER_SERIAL_REQUEST), (1, 9))
        damaged = bytearray(request)
        damaged[12] ^= 1
        self.assertIsNone(RUNNER_MODULE.validate_handover_frame(
            bytes(damaged), RUNNER_MODULE.HANDOVER_SERIAL_REQUEST))

        transcript = "\n".join((
            *RUNNER_MODULE.REIST_HANDOVER_MARKERS,
            "BOOT_OK", "TEST_OK", "C:\\>", "",
        ))
        self.assertIsNone(RUNNER_MODULE.validate(
            transcript, expect_handover=True))
        reversed_markers = transcript.replace(
            "REIST_HANDOVER REQUEST_SENT\nREIST_HANDOVER FENCE_CONFIRMED",
            "REIST_HANDOVER FENCE_CONFIRMED\nREIST_HANDOVER REQUEST_SENT",
        )
        self.assertIn("out of order", RUNNER_MODULE.validate(
            reversed_markers, expect_handover=True))

    def test_arp_request_injection_uses_qemu_hub_and_framed_socket(self) -> None:
        frame = RUNNER_MODULE.arp_request_frame()
        self.assertEqual(len(frame), 60)
        self.assertEqual(frame[0:6], b"\xff" * 6)
        self.assertEqual(frame[12:14], b"\x08\x06")
        self.assertEqual(frame[20:22], b"\x00\x01")
        self.assertEqual(frame[38:42], bytes((10, 0, 2, 15)))
        self.assertEqual(frame[42:], b"\x00" * 18)
        command = RUNNER_MODULE.qemu_command(
            Path("qemu"), Path("image"), nic="rtl8139",
            injection_port=32123,
        )
        joined = " ".join(str(argument) for argument in command)
        self.assertIn("socket,id=reistsocket,connect=127.0.0.1:32123", joined)
        self.assertIn("hubport,id=reistuserport,hubid=0,netdev=reistuser", joined)
        self.assertIn("hubport,id=reistsocketport,hubid=0,netdev=reistsocket",
                      joined)
        self.assertIn("rtl8139,netdev=reistnicport", joined)
        self.assertIn("-device", command)

        received = bytearray()
        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        port = listener.getsockname()[1]
        client = socket.create_connection(("127.0.0.1", port))
        server, _ = listener.accept()

        def receive() -> None:
            with server:
                expected = len(frame) + 4
                while len(received) < expected:
                    packet = server.recv(expected - len(received))
                    if not packet:
                        break
                    received.extend(packet)
            listener.close()

        thread = threading.Thread(target=receive)
        thread.start()
        self.assertTrue(RUNNER_MODULE.inject_ethernet_frame(client, frame))
        client.close()
        thread.join(timeout=1)
        framed_size = len(frame) + 4
        self.assertEqual(len(received), framed_size)
        self.assertEqual(struct.unpack("!I", received[:4])[0], len(frame))
        self.assertEqual(bytes(received[4:]), frame)

    def test_icmp_echo_frame_has_valid_checksums_and_bounded_payload(self) -> None:
        frame = RUNNER_MODULE.icmp_echo_request_frame()
        self.assertEqual(len(frame), 60)
        self.assertEqual(frame[12:14], b"\x08\x00")
        self.assertEqual(frame[23], 1)
        self.assertEqual(frame[26:30], bytes((10, 0, 2, 99)))
        self.assertEqual(frame[30:34], bytes((10, 0, 2, 15)))
        self.assertEqual(RUNNER_MODULE.internet_checksum(frame[14:34]), 0)
        self.assertEqual(frame[34], 8)
        self.assertEqual(frame[38:42], b"\x12\x34\x00\x01")
        self.assertEqual(frame[42:46], b"REIS")
        self.assertEqual(RUNNER_MODULE.internet_checksum(frame[34:46]), 0)

        transcript = "\n".join((
            "BOOT_OK", RUNNER_MODULE.REIST_ICMP_ECHO_QUEUED_MARKER,
            RUNNER_MODULE.REIST_ICMP_ECHO_MARKER, "TEST_OK", "C:\\>", "",
        ))
        self.assertIsNone(RUNNER_MODULE.validate(
            transcript, expect_icmp_echo=True))

    def test_udp_echo_frame_has_valid_checksums_and_bounded_payload(self) -> None:
        frame = RUNNER_MODULE.udp_echo_request_frame()
        self.assertEqual(len(frame), 60)
        self.assertEqual(frame[12:14], b"\x08\x00")
        self.assertEqual(frame[23], 17)
        self.assertEqual(RUNNER_MODULE.internet_checksum(frame[14:34]), 0)
        udp_length = struct.unpack("!H", frame[38:40])[0]
        udp = frame[34:34 + udp_length]
        self.assertEqual(struct.unpack("!HH", udp[:4]), (40000, 9000))
        self.assertEqual(udp[8:], b"REIST-UDP")
        pseudo = frame[26:34] + struct.pack("!BBH", 0, 17, udp_length)
        self.assertEqual(RUNNER_MODULE.internet_checksum(pseudo + udp), 0)

        transcript = "\n".join((
            "BOOT_OK", RUNNER_MODULE.REIST_UDP_ECHO_QUEUED_MARKER,
            RUNNER_MODULE.REIST_UDP_ECHO_MARKER, "TEST_OK", "C:\\>", "",
        ))
        self.assertIsNone(RUNNER_MODULE.validate(
            transcript, expect_udp_echo=True))

    def test_udp_binding_frame_uses_requested_port_and_generic_markers(self) -> None:
        frame = RUNNER_MODULE.udp_echo_request_frame(9001)
        udp_length = struct.unpack("!H", frame[38:40])[0]
        udp = frame[34:34 + udp_length]
        self.assertEqual(struct.unpack("!HH", udp[:4]), (40000, 9001))
        pseudo = frame[26:34] + struct.pack("!BBH", 0, 17, udp_length)
        self.assertEqual(RUNNER_MODULE.internet_checksum(pseudo + udp), 0)
        transcript = "\n".join((
            "BOOT_OK", RUNNER_MODULE.REIST_UDP_DATAGRAM_QUEUED_MARKER,
            RUNNER_MODULE.REIST_UDP_DATAGRAM_MARKER, "TEST_OK", "C:\\>", "",
        ))
        self.assertIsNone(RUNNER_MODULE.validate(
            transcript, expect_udp_binding=True))

    def test_dhcp_mediation_must_complete_before_boot(self) -> None:
        transcript = "\n".join((
            RUNNER_MODULE.REIST_DHCP_BOOT_DISCOVER_MARKER,
            RUNNER_MODULE.REIST_DHCP_BOOT_OFFER_MARKER,
            RUNNER_MODULE.REIST_DHCP_BOOT_ACK_MARKER,
            RUNNER_MODULE.REIST_DHCP_CONFIG_QUEUED_MARKER,
            RUNNER_MODULE.REIST_DHCP_CONFIG_MARKER,
            "BOOT_OK", "TEST_OK", "C:\\>", "",
        ))
        self.assertIsNone(RUNNER_MODULE.validate(
            transcript, expect_dhcp_config=True))
        late = transcript.replace(
            "REIST_NETWORK DHCP_CONFIG_MEDIATED\nBOOT_OK",
            "BOOT_OK\nREIST_NETWORK DHCP_CONFIG_MEDIATED",
        )
        self.assertIn("pre-boot", RUNNER_MODULE.validate(
            late, expect_dhcp_config=True))
        missing_offer = transcript.replace(
            RUNNER_MODULE.REIST_DHCP_BOOT_OFFER_MARKER + "\n", "")
        self.assertIn("Ring-3", RUNNER_MODULE.validate(
            missing_offer, expect_dhcp_config=True))

    def test_dhcp_lease_expiry_must_withdraw_configuration_after_boot(self) -> None:
        transcript = "\n".join((
            RUNNER_MODULE.REIST_DHCP_BOOT_DISCOVER_MARKER,
            RUNNER_MODULE.REIST_DHCP_BOOT_OFFER_MARKER,
            RUNNER_MODULE.REIST_DHCP_BOOT_ACK_MARKER,
            RUNNER_MODULE.REIST_DHCP_CONFIG_QUEUED_MARKER,
            RUNNER_MODULE.REIST_DHCP_CONFIG_MARKER,
            "BOOT_OK", "C:\\>",
            RUNNER_MODULE.REIST_DHCP_LEASE_EXPIRED_MARKER, "",
        ))
        self.assertIsNone(RUNNER_MODULE.validate(
            transcript, expect_dhcp_config=True, expect_dhcp_expiry=True))
        missing = transcript.replace(
            RUNNER_MODULE.REIST_DHCP_LEASE_EXPIRED_MARKER + "\n", "")
        self.assertIn("lease expiry", RUNNER_MODULE.validate(
            missing, expect_dhcp_expiry=True))
        self.assertIn("BOOT_OK", RUNNER_MODULE.validate(
            transcript.replace("BOOT_OK\n", ""), expect_dhcp_expiry=True))
        before_prompt = transcript.replace(
            "C:\\>\n" + RUNNER_MODULE.REIST_DHCP_LEASE_EXPIRED_MARKER,
            RUNNER_MODULE.REIST_DHCP_LEASE_EXPIRED_MARKER + "\nC:\\>")
        self.assertIn("lease expiry", RUNNER_MODULE.validate(
            before_prompt, expect_dhcp_expiry=True))

    def test_dhcp_renewal_must_follow_t1_request_after_boot(self) -> None:
        transcript = "\n".join((
            RUNNER_MODULE.REIST_DHCP_CONFIG_QUEUED_MARKER,
            RUNNER_MODULE.REIST_DHCP_CONFIG_MARKER,
            "BOOT_OK", "C:\\>",
            RUNNER_MODULE.REIST_DHCP_RENEW_REQUESTED_MARKER,
            RUNNER_MODULE.REIST_DHCP_RENEW_INGRESS_RING3_MARKER,
            RUNNER_MODULE.REIST_DHCP_RENEWED_MARKER, "",
        ))
        self.assertIsNone(RUNNER_MODULE.validate(
            transcript, expect_dhcp_renewal=True))
        missing = transcript.replace(
            RUNNER_MODULE.REIST_DHCP_RENEWED_MARKER + "\n", "")
        self.assertIn("DHCP renewal", RUNNER_MODULE.validate(
            missing, expect_dhcp_renewal=True))

        prompt_without_newline = transcript.replace(
            "C:\\>\n" + RUNNER_MODULE.REIST_DHCP_RENEW_REQUESTED_MARKER,
            "C:\\>" + RUNNER_MODULE.REIST_DHCP_RENEW_REQUESTED_MARKER,
        )
        self.assertIsNone(RUNNER_MODULE.validate(
            prompt_without_newline, expect_dhcp_renewal=True))

    def test_network_frame_handoff_requires_exact_ring3_marker(self) -> None:
        transcript = "\n".join((
            RUNNER_MODULE.REIST_NETWORK_FRAME_MARKER,
            "BOOT_OK", "C:\\>", "TEST_OK", "C:\\>", "",
        ))
        self.assertIsNone(RUNNER_MODULE.validate(
            transcript, expect_network_frame=True))
        self.assertIn("network frame handoff", RUNNER_MODULE.validate(
            transcript.replace(RUNNER_MODULE.REIST_NETWORK_FRAME_MARKER,
                               "NOT_FRAME_HANDOFF"),
            expect_network_frame=True))

    def test_network_ipv4_parser_requires_exact_ring3_marker(self) -> None:
        transcript = "\n".join((
            RUNNER_MODULE.REIST_NETWORK_FRAME_MARKER,
            RUNNER_MODULE.REIST_NETWORK_IPV4_MARKER,
            "BOOT_OK", "C:\\>", "TEST_OK", "C:\\>", "",
        ))
        self.assertIsNone(RUNNER_MODULE.validate(
            transcript, expect_network_ipv4=True))
        self.assertIn("IPv4 parser", RUNNER_MODULE.validate(
            transcript.replace(RUNNER_MODULE.REIST_NETWORK_IPV4_MARKER,
                               "NOT_IPV4_PARSED_RING3"),
            expect_network_ipv4=True))

    def test_network_icmp_parser_requires_exact_ring3_marker(self) -> None:
        transcript = "\n".join((
            RUNNER_MODULE.REIST_NETWORK_ICMP_MARKER,
            "BOOT_OK", "C:\\>", "TEST_OK", "C:\\>", "",
        ))
        self.assertIsNone(RUNNER_MODULE.validate(
            transcript, expect_network_icmp=True))
        self.assertIn("ICMP parser", RUNNER_MODULE.validate(
            transcript.replace(RUNNER_MODULE.REIST_NETWORK_ICMP_MARKER,
                               "NOT_ICMP_PARSED_RING3"),
            expect_network_icmp=True))

    def test_network_udp_parser_requires_exact_ring3_marker(self) -> None:
        transcript = "\n".join((
            RUNNER_MODULE.REIST_NETWORK_UDP_MARKER,
            "BOOT_OK", "C:\\>", "TEST_OK", "C:\\>", "",
        ))
        self.assertIsNone(RUNNER_MODULE.validate(
            transcript, expect_network_udp=True))
        self.assertIn("UDP parser", RUNNER_MODULE.validate(
            transcript.replace(RUNNER_MODULE.REIST_NETWORK_UDP_MARKER,
                               "NOT_UDP_PARSED_RING3"),
            expect_network_udp=True))

    def test_network_dhcp_parser_requires_exact_ring3_marker(self) -> None:
        transcript = "\n".join((
            RUNNER_MODULE.REIST_NETWORK_DHCP_MARKER,
            "BOOT_OK", "C:\\>", "TEST_OK", "C:\\>", "",
        ))
        self.assertIsNone(RUNNER_MODULE.validate(
            transcript, expect_network_dhcp=True))
        self.assertIn("DHCP parser", RUNNER_MODULE.validate(
            transcript.replace(RUNNER_MODULE.REIST_NETWORK_DHCP_MARKER,
                               "NOT_DHCP_PARSED_RING3"),
            expect_network_dhcp=True))

    def test_network_udp_ingress_requires_exact_ring3_marker(self) -> None:
        transcript = "\n".join((
            RUNNER_MODULE.REIST_NETWORK_UDP_INGRESS_MARKER,
            "BOOT_OK", "C:\\>", "TEST_OK", "C:\\>", "",
        ))
        self.assertIsNone(RUNNER_MODULE.validate(
            transcript, expect_network_udp_ingress=True))
        self.assertIn("UDP ingress", RUNNER_MODULE.validate(
            transcript.replace(
                RUNNER_MODULE.REIST_NETWORK_UDP_INGRESS_MARKER,
                "NOT_UDP_INGRESS_RING3"),
            expect_network_udp_ingress=True))

    def test_reist_probe_markers_are_required_in_order(self) -> None:
        transcript = "\n".join((
            *RUNNER_MODULE.REIST_PROBE_MARKERS,
            RUNNER_MODULE.REIST_PROBE_COMPLETION_MARKER,
            RUNNER_MODULE.REIST_NETWORK_SERVICE_READY_MARKER,
            "BOOT_OK",
            RUNNER_MODULE.REIST_SERVICE_CORRELATION_MARKER,
            RUNNER_MODULE.REIST_ARP_IDENTITY_MARKER,
            RUNNER_MODULE.REIST_ARP_VALIDATION_MARKER,
            RUNNER_MODULE.REIST_NETWORK_STATS_MARKER,
            RUNNER_MODULE.REIST_SERVICE_MARKER,
            RUNNER_MODULE.REIST_NETWORK_MARKER,
            "TEST_OK", "C:\\>", "",
        ))
        self.assertIsNone(RUNNER_MODULE.validate(
            transcript, expect_reist_probe=True))
        missing = transcript.replace(
            RUNNER_MODULE.REIST_PROBE_COMPLETION_MARKER + "\n", "")
        self.assertIn("cumulative REIST probe", RUNNER_MODULE.validate(
            missing, expect_reist_probe=True))
        no_ready = transcript.replace(
            RUNNER_MODULE.REIST_NETWORK_SERVICE_READY_MARKER + "\n", "")
        self.assertIn("service-ready", RUNNER_MODULE.validate(
            no_ready, expect_reist_probe=True))
        no_service = transcript.replace(
            RUNNER_MODULE.REIST_SERVICE_MARKER + "\n", "")
        self.assertIn("diagnostic-service", RUNNER_MODULE.validate(
            no_service, expect_reist_probe=True))
        no_network = transcript.replace(
            RUNNER_MODULE.REIST_NETWORK_MARKER + "\n", "")
        self.assertIn("network-parser", RUNNER_MODULE.validate(
            no_network, expect_reist_probe=True))

    def test_real_network_handoff_marker_is_optional_but_enforceable(self):
        transcript = "\n".join((
            *RUNNER_MODULE.REIST_PROBE_MARKERS,
            RUNNER_MODULE.REIST_PROBE_COMPLETION_MARKER,
            RUNNER_MODULE.REIST_NETWORK_SERVICE_READY_MARKER,
            "BOOT_OK",
            RUNNER_MODULE.REIST_SERVICE_CORRELATION_MARKER,
            RUNNER_MODULE.REIST_ARP_IDENTITY_MARKER,
            RUNNER_MODULE.REIST_ARP_VALIDATION_MARKER,
            RUNNER_MODULE.REIST_NETWORK_PROBE_ID_MARKER,
            RUNNER_MODULE.REIST_ARP_BINDING_MARKER,
            RUNNER_MODULE.REIST_NETWORK_HANDOFF_MARKER,
            RUNNER_MODULE.REIST_NETWORK_PRESSURE_FALLBACK_MARKER,
            RUNNER_MODULE.REIST_NETWORK_PRESSURE_MARKER,
            RUNNER_MODULE.REIST_NETWORK_STATS_MARKER,
            RUNNER_MODULE.REIST_ARP_REVOKED_MARKER,
            RUNNER_MODULE.REIST_NETWORK_CRASH_MARKER,
            RUNNER_MODULE.REIST_NETWORK_RECOVERY_MARKER,
            RUNNER_MODULE.REIST_SERVICE_MARKER,
            RUNNER_MODULE.REIST_NETWORK_MARKER,
            "TEST_OK", "C:\\>", "",
        ))
        self.assertIsNone(RUNNER_MODULE.validate(
            transcript, expect_reist_probe=True,
            expect_network_handoff=True))
        missing = transcript.replace(
            RUNNER_MODULE.REIST_NETWORK_HANDOFF_MARKER + "\n", "")
        self.assertIn("real NIC", RUNNER_MODULE.validate(
            missing, expect_reist_probe=True,
            expect_network_handoff=True))
        no_recovery = transcript.replace(
            RUNNER_MODULE.REIST_NETWORK_RECOVERY_MARKER + "\n", "")
        self.assertIn("crash recovery", RUNNER_MODULE.validate(
            no_recovery, expect_reist_probe=True,
            expect_network_handoff=True))
        no_revoke = transcript.replace(
            RUNNER_MODULE.REIST_ARP_REVOKED_MARKER + "\n", "")
        self.assertIn("crash recovery", RUNNER_MODULE.validate(
            no_revoke, expect_reist_probe=True,
            expect_network_handoff=True))

    def test_reist_probe_completion_precedes_guest_command(self) -> None:
        source = RUNNER.read_text(encoding="utf-8")
        completion = source.index("REIST_PROBE_COMPLETION_MARKER, deadline")
        command = source.index(
            "inject_ps2_command(process, PS2_GUEST_COMMAND)")
        self.assertLess(completion, command)

    def test_guest_command_is_injected_through_ps2_not_com1(self) -> None:
        source = RUNNER.read_text(encoding="utf-8")
        injection = RUNNER_MODULE.monitor_key_commands("gtest")
        self.assertEqual(injection[-1], "sendkey ret\n")
        self.assertEqual(len(injection), 6)
        self.assertIn('"-serial", "mon:stdio"', source)
        self.assertNotIn('process.stdin.write(character)', source)

    def setUp(self) -> None:
        self.assertTrue(
            RUNNER.is_file(),
            "R0.4 requires scripts/run_qemu_smoke.py",
        )
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.image = self.directory / "boot image.img"
        self.image.write_bytes(b"not a real disk; fake QEMU never reads it")
        self.arguments_file = self.directory / "qemu-arguments.txt"
        self.log_file = self.directory / "guest-smoke.log"
        self.qemu = self._write_fake_qemu()

    def _write_fake_qemu(self) -> Path:
        if os.name == "nt":
            path = self.directory / "fake-qemu.cmd"
            path.write_text(
                "@echo off\n"
                "> \"%FAKE_QEMU_ARGS%\" echo %*\n"
                "if \"%FAKE_QEMU_MODE%\"==\"success\" goto success\n"
                "if \"%FAKE_QEMU_MODE%\"==\"missing-test\" goto missing\n"
                "if \"%FAKE_QEMU_MODE%\"==\"reverse\" goto reverse\n"
                "if \"%FAKE_QEMU_MODE%\"==\"not-test-ok\" goto not_test_ok\n"
                "if \"%FAKE_QEMU_MODE%\"==\"panic-after-test\" goto panic_after_test\n"
                "if \"%FAKE_QEMU_MODE%\"==\"missing-second-prompt\" goto missing_second_prompt\n"
                "if \"%FAKE_QEMU_MODE%\"==\"early-exit\" goto early\n"
                "if \"%FAKE_QEMU_MODE%\"==\"timeout\" goto timeout\n"
                "exit /b 99\n"
                ":success\n"
                "echo firmware trace\n"
                "echo BOOT_OK\n"
                "echo C:\\^>\n"
                "call :short_delay\n"
                "echo TEST_OK\n"
                "echo C:\\^>\n"
                "exit /b 0\n"
                ":missing\n"
                "echo BOOT_OK\n"
                "echo shell did not finish\n"
                "exit /b 0\n"
                ":reverse\n"
                "echo TEST_OK\n"
                "echo BOOT_OK\n"
                "exit /b 0\n"
                ":not_test_ok\n"
                "echo BOOT_OK\n"
                "echo C:\\^>\n"
                "call :short_delay\n"
                "echo NOT_TEST_OK\n"
                "echo C:\\^>\n"
                "exit /b 0\n"
                ":panic_after_test\n"
                "echo BOOT_OK\n"
                "echo C:\\^>\n"
                "call :short_delay\n"
                "echo TEST_OK\n"
                "echo PANIC: failure after test marker\n"
                "echo C:\\^>\n"
                "exit /b 0\n"
                ":missing_second_prompt\n"
                "echo BOOT_OK\n"
                "echo C:\\^>\n"
                "call :short_delay\n"
                "echo TEST_OK\n"
                "exit /b 0\n"
                ":early\n"
                "echo BOOT_OK\n"
                "echo fatal detail before QEMU exit\n"
                "exit /b 7\n"
                ":timeout\n"
                "echo partial boot transcript\n"
                ":spin\n"
                "goto spin\n"
                ":short_delay\n"
                "ping -n 2 127.0.0.1 >nul\n"
                "exit /b 0\n",
                encoding="ascii",
            )
            return path

        path = self.directory / "fake-qemu"
        path.write_text(
            "#!/bin/sh\n"
            "printf '%s\\n' \"$@\" > \"$FAKE_QEMU_ARGS\"\n"
            "case \"$FAKE_QEMU_MODE\" in\n"
            "  success) printf 'firmware trace\\nBOOT_OK\\nC:\\\\>\\n'; "
            "sleep 0.6; printf 'TEST_OK\\nC:\\\\>\\n'; exit 0 ;;\n"
            "  missing-test) printf 'BOOT_OK\\nshell did not finish\\n'; exit 0 ;;\n"
            "  reverse) printf 'TEST_OK\\nBOOT_OK\\n'; exit 0 ;;\n"
            "  not-test-ok) printf 'BOOT_OK\\nC:\\\\>\\n'; "
            "sleep 0.6; printf 'NOT_TEST_OK\\nC:\\\\>\\n'; exit 0 ;;\n"
            "  panic-after-test) printf 'BOOT_OK\\nC:\\\\>\\n'; "
            "sleep 0.6; "
            "printf 'TEST_OK\\nPANIC: failure after test marker\\nC:\\\\>\\n'; exit 0 ;;\n"
            "  missing-second-prompt) printf 'BOOT_OK\\nC:\\\\>\\n'; "
            "sleep 0.6; printf 'TEST_OK\\n'; exit 0 ;;\n"
            "  early-exit) printf 'BOOT_OK\\nfatal detail before QEMU exit\\n'; exit 7 ;;\n"
            "  timeout) printf 'partial boot transcript\\n'; "
            "while :; do :; done ;;\n"
            "  *) exit 99 ;;\n"
            "esac\n",
            encoding="ascii",
        )
        path.chmod(0o755)
        return path

    def run_smoke(
        self,
        mode: str,
        *,
        timeout: str = "4",
        image: Path | None = None,
        memory: str | None = None,
        watchdog: bool = False,
        persistent: bool = False,
        sata: bool = False,
        auxiliary_sata_image: Path | None = None,
        boot_only: bool = False,
    ) -> subprocess.CompletedProcess[str]:
        environment = os.environ.copy()
        environment["FAKE_QEMU_MODE"] = mode
        environment["FAKE_QEMU_ARGS"] = str(self.arguments_file)
        command = [
                sys.executable,
                str(RUNNER),
                "--qemu",
                str(self.qemu),
                "--image",
                str(image or self.image),
                "--timeout",
                timeout,
                "--log",
                str(self.log_file),
            ]
        if memory is not None:
            command.extend(["--memory", memory])
        if watchdog:
            command.append("--watchdog")
        if persistent:
            command.append("--persistent")
        if sata:
            command.append("--sata")
        if auxiliary_sata_image is not None:
            command.extend(["--aux-sata-image", str(auxiliary_sata_image)])
        if boot_only:
            command.append("--boot-only")
        return subprocess.run(
            command,
            cwd=ROOT,
            env=environment,
            text=True,
            capture_output=True,
            timeout=8,
        )

    def combined_output(self, result: subprocess.CompletedProcess[str]) -> str:
        return result.stdout + result.stderr

    def test_success_requires_ordered_boot_and_test_markers(self) -> None:
        result = self.run_smoke("success")

        self.assertEqual(result.returncode, 0, self.combined_output(result))
        transcript = self.log_file.read_text(encoding="utf-8")
        self.assertLess(transcript.index("BOOT_OK"), transcript.index("TEST_OK"))
        prompts = [line.rstrip("\r") for line in transcript.splitlines()]
        self.assertGreaterEqual(prompts.count(r"C:\>"), 2)
        self.assertGreater(
            prompts.index(r"C:\>", prompts.index("TEST_OK")),
            prompts.index("TEST_OK"),
        )

    def test_negated_test_marker_is_not_accepted_as_success(self) -> None:
        result = self.run_smoke("not-test-ok")

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("NOT_TEST_OK", self.combined_output(result))

    def test_panic_after_test_marker_still_fails_the_smoke(self) -> None:
        result = self.run_smoke("panic-after-test")

        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            "PANIC: failure after test marker",
            self.combined_output(result),
        )

    def test_test_marker_without_second_shell_prompt_is_failure(self) -> None:
        result = self.run_smoke("missing-second-prompt")

        self.assertNotEqual(result.returncode, 0)
        output = self.combined_output(result)
        self.assertIn("TEST_OK", output)
        self.assertIn(r"C:\>", output)

    def test_qemu_is_headless_and_uses_the_generated_ide_image(self) -> None:
        result = self.run_smoke("success")
        self.assertEqual(result.returncode, 0, self.combined_output(result))
        arguments = self.arguments_file.read_text(encoding="utf-8")

        self.assertRegex(arguments, r"(?:^|\s)-accel(?:\s|$)")
        self.assertRegex(arguments, r"(?:^|\s)tcg(?:\s|$)")
        self.assertRegex(arguments, r"(?:^|\s)-display(?:\s|$)")
        self.assertRegex(arguments, r"(?:^|\s)none(?:\s|$)")
        self.assertRegex(arguments, r"(?:^|\s)-monitor(?:\s|$)")
        self.assertRegex(arguments, r"(?:^|\s)-serial(?:\s|$)")
        self.assertRegex(arguments, r"(?:^|\s)mon:stdio(?:\s|$)")
        self.assertIn("-nodefaults", arguments)
        self.assertIn("-snapshot", arguments)
        self.assertIn("-no-reboot", arguments)
        self.assertIn("-no-shutdown", arguments)
        self.assertIn(f"file={self.image}", arguments)
        self.assertIn("format=raw", arguments)
        self.assertIn("if=ide", arguments)
        self.assertIn("index=0", arguments)

    def test_memory_option_forwards_every_smoke_matrix_size_to_qemu(self) -> None:
        for amount in ("32M", "64M", "256M", "1024M"):
            with self.subTest(amount=amount):
                result = self.run_smoke("success", memory=amount)
                self.assertEqual(result.returncode, 0, self.combined_output(result))
                arguments = self.arguments_file.read_text(encoding="utf-8")
                self.assertRegex(
                    arguments,
                    rf"(?:^|\s)-m\s+{re.escape(amount)}(?:\s|$)",
                )

    def test_sata_option_attaches_boot_image_to_ahci(self) -> None:
        result = self.run_smoke("success", sata=True)
        self.assertEqual(result.returncode, 0, self.combined_output(result))
        arguments = self.arguments_file.read_text(encoding="utf-8")
        self.assertIn("ich9-ahci,id=reistahci", arguments)
        self.assertIn("if=none,id=reistdisk", arguments)
        self.assertIn("ide-hd,drive=reistdisk,bus=reistahci.0", arguments)

    def test_auxiliary_sata_disk_precedes_separately_bootable_system_disk(self) -> None:
        auxiliary = self.directory / "auxiliary.img"
        auxiliary.write_bytes(b"foreign test volume")
        result = self.run_smoke(
            "success", sata=True, auxiliary_sata_image=auxiliary,
        )
        self.assertEqual(result.returncode, 0, self.combined_output(result))
        arguments = self.arguments_file.read_text(encoding="utf-8")
        auxiliary_argument = f"file={auxiliary.resolve()}"
        system_argument = f"file={self.image.resolve()}"
        self.assertLess(arguments.index(auxiliary_argument),
                        arguments.index(system_argument))
        self.assertIn("reistauxdisk,bus=reistahci.0,bootindex=2", arguments)
        self.assertIn("reistdisk,bus=reistahci.1,bootindex=1", arguments)

    def test_auxiliary_sata_disk_requires_sata_mode(self) -> None:
        auxiliary = self.directory / "auxiliary.img"
        auxiliary.write_bytes(b"foreign test volume")
        result = self.run_smoke(
            "success", auxiliary_sata_image=auxiliary,
        )
        self.assertEqual(result.returncode, 2)
        self.assertIn("requires --sata", self.combined_output(result))

    def test_boot_only_does_not_require_guest_file_io_test(self) -> None:
        result = self.run_smoke("success", boot_only=True)
        self.assertEqual(result.returncode, 0, self.combined_output(result))

        transcript = "\n".join((
            RUNNER_MODULE.REIST_PROBE_COMPLETION_MARKER,
            RUNNER_MODULE.REIST_NETWORK_SERVICE_READY_MARKER,
            "BOOT_OK", "C:\\>", "",
        ))
        self.assertIsNone(RUNNER_MODULE.validate(
            transcript, expect_reist_probe=True, boot_only=True,
        ))

    def test_watchdog_profile_attaches_real_ib700_reset_device(self) -> None:
        result = self.run_smoke("success", watchdog=True)
        self.assertEqual(result.returncode, 0, self.combined_output(result))
        arguments = self.arguments_file.read_text(encoding="utf-8")
        self.assertRegex(arguments, r"(?:^|\s)-device\s+ib700(?:\s|$)")
        self.assertRegex(arguments, r"(?:^|\s)-watchdog-action\s+reset(?:\s|$)")

    def test_persistent_mode_omits_snapshot_for_disposable_recovery_images(self) -> None:
        result = self.run_smoke("success", persistent=True)
        self.assertEqual(result.returncode, 0, self.combined_output(result))
        arguments = self.arguments_file.read_text(encoding="utf-8")
        self.assertNotIn("-snapshot", arguments)

    def test_fatal_recovery_profile_allows_the_expected_reboot(self) -> None:
        environment = os.environ.copy()
        environment["FAKE_QEMU_MODE"] = "success"
        environment["FAKE_QEMU_ARGS"] = str(self.arguments_file)
        subprocess.run(
            [sys.executable, str(RUNNER), "--qemu", str(self.qemu),
             "--image", str(self.image), "--timeout", "1",
             "--expect-fatal-recovery"], cwd=ROOT, env=environment,
            capture_output=True, text=True, timeout=8,
        )
        arguments = self.arguments_file.read_text(encoding="utf-8")
        self.assertNotIn("-no-reboot", arguments)

    def test_invalid_memory_option_is_rejected_before_qemu_starts(self) -> None:
        result = self.run_smoke("success", memory="0M")

        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.arguments_file.exists())
        self.assertIn("memory", self.combined_output(result).lower())

    def test_normal_qemu_exit_without_test_marker_is_failure(self) -> None:
        result = self.run_smoke("missing-test")

        self.assertNotEqual(result.returncode, 0)
        output = self.combined_output(result)
        self.assertIn("shell did not finish", output)
        self.assertIn("TEST_OK", output)

    def test_test_marker_before_boot_marker_is_failure(self) -> None:
        result = self.run_smoke("reverse")

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("BOOT_OK", self.combined_output(result))

    def test_early_qemu_failure_preserves_the_guest_transcript(self) -> None:
        result = self.run_smoke("early-exit")

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("fatal detail before QEMU exit", self.combined_output(result))
        self.assertIn("fatal detail before QEMU exit",
                      self.log_file.read_text(encoding="utf-8"))

    def test_timeout_terminates_qemu_and_prints_partial_transcript(self) -> None:
        started = time.monotonic()
        result = self.run_smoke("timeout", timeout="0.2")
        elapsed = time.monotonic() - started

        self.assertNotEqual(result.returncode, 0)
        self.assertLess(elapsed, 3.0, "smoke timeout did not terminate QEMU")
        output = self.combined_output(result).lower()
        self.assertIn("timeout", output)
        self.assertIn("partial boot transcript", output)

    def test_missing_image_is_rejected_before_qemu_is_started(self) -> None:
        missing = self.directory / "missing.img"
        result = self.run_smoke("success", image=missing)

        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.arguments_file.exists())
        self.assertIn("image", self.combined_output(result).lower())


class QemuGuestSmokePackagingTests(unittest.TestCase):
    def test_make_exposes_a_native_image_smoke_target(self) -> None:
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")

        self.assertRegex(makefile, r"(?m)^\.PHONY:.*\btest-smoke\b")
        self.assertRegex(makefile, r"(?m)^test-smoke:\s+native-image\s*$")
        self.assertIn("scripts/run_qemu_smoke.py", makefile)
        self.assertIn("$(OUTPUT_DIR)/reist-os.img", makefile)

    def test_ci_installs_qemu_and_runs_the_guest_smoke(self) -> None:
        workflow = (ROOT / ".github/workflows/build.yml").read_text(
            encoding="utf-8"
        )

        self.assertRegex(workflow, r"\bqemu-system-(?:x86|i386)\b")
        self.assertIn("make test-smoke", workflow)
        self.assertLess(
            workflow.index("make all TARGET=qemu VIDEO=vga"),
            workflow.index("make test-smoke"),
        )


if __name__ == "__main__":
    unittest.main()
