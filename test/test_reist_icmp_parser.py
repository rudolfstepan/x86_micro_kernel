from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


class ReistIcmpParserTests(unittest.TestCase):
    def test_host_parser_behavior(self) -> None:
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / (
                "icmp-parser.exe" if os.name == "nt" else "icmp-parser")
            subprocess.run([
                compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I", str(ROOT / "userspace/sdk/include"),
                str(ROOT / "userspace/sdk/reist_ipv4_parser.c"),
                str(ROOT / "userspace/sdk/reist_icmp_parser.c"),
                str(ROOT / "test/test_reist_icmp_parser_host.c"),
                "-o", str(executable),
            ], check=True, capture_output=True, timeout=30)
            subprocess.run([str(executable)], check=True, timeout=10)

    def test_parser_is_bounded_heapfree_and_fail_closed(self) -> None:
        source = read("userspace/sdk/reist_icmp_parser.c")
        self.assertIn("reist_ipv4_parse_frame", source)
        self.assertIn("ipv4.payload_length < ICMP_HEADER_SIZE", source)
        self.assertIn("*result = (reist_icmp_parse_result_t){0};", source)
        self.assertIn("checksum_valid", source)
        for forbidden in ("malloc", "realloc", "for (;;)", "while (true)"):
            self.assertNotIn(forbidden, source)

    def test_ring3_reports_only_after_icmp_validation(self) -> None:
        service = read("examples/userspace/reist_probe.c")
        supervisor = read("kernel/init/supervisor.c")
        self.assertIn("reist_icmp_parse_frame", service)
        self.assertIn("X86OS_REIST_REPORT_NETWORK_ICMP", service)
        self.assertLess(service.index("reist_icmp_parse_frame"),
                        service.index("X86OS_REIST_REPORT_NETWORK_ICMP"))
        self.assertIn("icmp_delivery_pending", supervisor)
        self.assertIn("icmp_delivery_crc32 == value", supervisor)
        self.assertIn("REIST_REPORT_NETWORK_ICMP", supervisor)
        self.assertIn("REIST_NETWORK ICMP_PARSED_RING3", supervisor)

    def test_runtime_gate_requires_icmp_ring3_marker(self) -> None:
        self.assertIn("REIST_NETWORK ICMP_PARSED_RING3",
                      read("scripts/run_qemu_smoke.py"))
        runtime = read("scripts/test-reist-runtime.ps1")
        self.assertIn("'network-icmp-parser'", runtime)
        self.assertIn("--expect-network-icmp", runtime)


if __name__ == "__main__":
    unittest.main()
