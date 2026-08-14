"""Host contracts for the bounded REIST standby handover protocol."""

from __future__ import annotations

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class ReistHandoverTests(unittest.TestCase):
    def test_host_protocol(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "handover-test.exe"
            subprocess.run([
                compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-DREIST_HOST_TEST", "-I", str(ROOT),
                str(ROOT / "test/test_handover_host.c"),
                str(ROOT / "kernel/init/handover.c"),
                str(ROOT / "kernel/init/critical_object.c"),
                "-o", str(executable),
            ], check=True, timeout=30)
            subprocess.run([str(executable)], check=True, timeout=10)

    def test_protocol_is_static_bounded_and_fail_closed(self):
        header = (ROOT / "include/kernel/handover.h").read_text()
        source = (ROOT / "kernel/init/handover.c").read_text()
        self.assertIn("HANDOVER_MAX_LEASE_MS 10000U", header)
        self.assertIn("critical_object_t protected_status", source)
        self.assertNotIn("k_malloc", source)
        self.assertNotIn("k_free", source)
        self.assertIn("status.fenced_epoch != status.epoch", source)
        self.assertIn("expected_epoch != status.epoch", source)
        self.assertIn("status.epoch == UINT64_MAX", source)


if __name__ == "__main__":
    unittest.main()
