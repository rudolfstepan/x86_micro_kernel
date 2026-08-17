from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


class ReistUdpParserTests(unittest.TestCase):
    def test_host_parser_behavior(self) -> None:
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / (
                "udp-parser.exe" if os.name == "nt" else "udp-parser")
            subprocess.run([
                compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I", str(ROOT / "userspace/sdk/include"),
                str(ROOT / "userspace/sdk/reist_ipv4_parser.c"),
                str(ROOT / "userspace/sdk/reist_udp_parser.c"),
                str(ROOT / "test/test_reist_udp_parser_host.c"),
                "-o", str(executable),
            ], check=True, capture_output=True, timeout=30)
            subprocess.run([str(executable)], check=True, timeout=10)

    def test_parser_is_bounded_heapfree_and_requires_checksum(self) -> None:
        source = read("userspace/sdk/reist_udp_parser.c")
        self.assertIn("udp_length != ipv4.payload_length", source)
        self.assertIn("checksum == 0U", source)
        self.assertIn("reist_ipv4_parse_frame", source)
        self.assertIn("*result = (reist_udp_parse_result_t){0};", source)
        for forbidden in ("malloc", "realloc", "for (;;)", "while (true)"):
            self.assertNotIn(forbidden, source)

    def test_ring3_report_is_delivery_and_generation_scoped(self) -> None:
        service = read("userspace/programs/reist_probe.c")
        supervisor = read("kernel/init/supervisor.c")
        self.assertIn("reist_udp_parse_frame", service)
        self.assertIn("X86OS_REIST_REPORT_NETWORK_UDP", service)
        self.assertIn("udp_delivery_pending", supervisor)
        self.assertIn("REIST_REPORT_NETWORK_UDP", supervisor)
        self.assertIn("REIST_NETWORK UDP_PARSED_RING3", supervisor)

    def test_runtime_gate_requires_udp_ring3_marker(self) -> None:
        self.assertIn("REIST_NETWORK UDP_PARSED_RING3",
                      read("scripts/run_qemu_smoke.py"))
        runtime = read("scripts/test-reist-runtime.ps1")
        self.assertIn("'network-udp-parser'", runtime)
        self.assertIn("--expect-network-udp", runtime)


if __name__ == "__main__":
    unittest.main()
