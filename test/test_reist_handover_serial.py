"""Contracts for the external COM2 handover fence reference channel."""

from __future__ import annotations

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class ReistHandoverSerialTests(unittest.TestCase):
    def test_frame_protocol_host(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "handover-serial-test.exe"
            subprocess.run([
                compiler, "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                "-I", str(ROOT),
                str(ROOT / "test/test_handover_serial_host.c"),
                str(ROOT / "kernel/init/handover_serial_backend.c"),
                "-o", str(executable),
            ], check=True, timeout=30)
            subprocess.run([str(executable)], check=True, timeout=10)

    def test_backend_is_bounded_and_uses_dedicated_com2(self):
        source = (ROOT / "kernel/init/handover_serial_backend.c").read_text()
        self.assertIn("SERIAL_COM2", source)
        self.assertIn("UART_IO_TIMEOUT_MS", source)
        self.assertIn("UART_MAX_POLLS", source)
        self.assertIn("pit_monotonic_ms() >= deadline", source)
        self.assertNotIn("k_malloc", source)
        self.assertNotIn("serial_write_char", source)

    def test_isolated_build_and_runtime_gate_are_wired(self):
        makefile = (ROOT / "Makefile").read_text()
        build = (ROOT / "scripts/build-windows.ps1").read_text()
        runtime = (ROOT / "scripts/test-reist-runtime.ps1").read_text()
        kernel = (ROOT / "kernel/init/kernel.c").read_text()
        self.assertIn("HANDOVER_FAULT_INJECTION ?= 0", makefile)
        self.assertIn("test-smoke-handover:", makefile)
        self.assertIn("$HandoverFaultInjection", build)
        self.assertIn("HANDOVER_NODE_ID=$HandoverNodeId", build)
        self.assertIn("'handover'", runtime)
        self.assertIn("REIST_HANDOVER REQUEST_SENT", kernel)
        self.assertIn("REIST_HANDOVER TAKEOVER_OK", kernel)
        self.assertIn("REIST_HANDOVER ACTIVE_STATE_SENT", kernel)
        self.assertIn("REIST_HANDOVER STANDBY_STATE_APPLIED", kernel)
        receive = kernel.index("handover_serial_receive_state")
        initialize = kernel.index("handover_init_replica", receive)
        applied = kernel.index("REIST_HANDOVER STANDBY_STATE_APPLIED", initialize)
        self.assertLess(receive, initialize)
        self.assertLess(initialize, applied)
        self.assertLess(kernel.index("supervisor_start_probe("),
                        kernel.index("supervisor_start_worker()"))
        self.assertLess(kernel.index("storage_service_start("),
                        kernel.index("supervisor_start_worker()"))


if __name__ == "__main__":
    unittest.main()
