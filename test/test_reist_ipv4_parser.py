from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


class ReistIpv4ParserTests(unittest.TestCase):
    def test_host_parser_behavior(self) -> None:
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / (
                "ipv4-parser.exe" if os.name == "nt" else "ipv4-parser")
            subprocess.run([
                compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I", str(ROOT / "userspace/sdk/include"),
                str(ROOT / "userspace/sdk/reist_ipv4_parser.c"),
                str(ROOT / "test/test_reist_ipv4_parser_host.c"),
                "-o", str(executable),
            ], check=True, capture_output=True, timeout=30)
            subprocess.run([str(executable)], check=True, timeout=10)

    def test_parser_is_bounded_heapfree_and_fail_closed(self) -> None:
        source = read("userspace/sdk/reist_ipv4_parser.c")
        self.assertIn("IPV4_MAX_HEADER_SIZE 60U", source)
        self.assertIn("frame_length > REIST_IPV4_MAX_FRAME_SIZE", source)
        self.assertIn("*result = (reist_ipv4_parse_result_t){0};", source)
        self.assertIn("& 0x3FFFU", source)
        self.assertIn("checksum_valid", source)
        for forbidden in ("malloc", "realloc", "for (;;)", "while (true)"):
            self.assertNotIn(forbidden, source)

    def test_ring3_consumes_the_raw_frame_before_reporting(self) -> None:
        service = read("userspace/programs/reist_probe.c")
        supervisor = read("kernel/init/supervisor.c")
        self.assertIn("reist_ipv4_parse_frame", service)
        self.assertIn("X86OS_REIST_REPORT_NETWORK_IPV4", service)
        self.assertLess(service.index("reist_ipv4_parse_frame"),
                        service.index("X86OS_REIST_REPORT_NETWORK_IPV4"))
        self.assertIn("ipv4_delivery_pending", supervisor)
        self.assertIn("REIST_REPORT_NETWORK_IPV4", supervisor)
        self.assertIn("value != 1U && value != 17U", supervisor)
        self.assertIn("REIST_NETWORK IPV4_PARSED_RING3", supervisor)

    def test_runtime_gate_requires_ipv4_ring3_marker(self) -> None:
        self.assertIn("REIST_NETWORK IPV4_PARSED_RING3",
                      read("scripts/run_qemu_smoke.py"))
        runtime = read("scripts/test-reist-runtime.ps1")
        self.assertIn("'network-ipv4-parser'", runtime)
        self.assertIn("--expect-network-ipv4", runtime)

    def test_ring0_ipv4_fallback_is_removed_and_fail_closed(self) -> None:
        netstack = read("drivers/net/netstack.c")
        netdev = read("drivers/net/netdev.c")
        self.assertNotIn("static void handle_ip_packet(", netstack)
        self.assertNotIn("netstack_process_packet", netstack)
        self.assertNotIn("case ETHERTYPE_IPV4:", netstack)
        self.assertNotIn("netdev_queue_rx_packet", netdev)
        self.assertIn("netdev_queue_service_packet(packet, length)", netdev)


if __name__ == "__main__":
    unittest.main()
