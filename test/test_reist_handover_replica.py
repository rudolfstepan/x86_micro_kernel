"""Host contracts for protected, sequenced handover service state."""

from __future__ import annotations

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class ReistHandoverReplicaTests(unittest.TestCase):
    def test_target_replica_uses_a_cpu_owned_smp_lock(self):
        source = (ROOT / "kernel/init/handover_replica.c").read_text()
        self.assertIn("replica_state_lock = SPINLOCK_INIT", source)
        self.assertIn("spinlock_acquire_irq(&replica_state_lock)", source)
        self.assertIn("spinlock_release_irq(&replica_state_lock, flags)",
                      source)

    def test_protected_replica_state_host(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "handover-replica-test.exe"
            subprocess.run([
                compiler, "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                "-DREIST_HOST_TEST", "-I", str(ROOT),
                str(ROOT / "test/test_handover_replica_host.c"),
                str(ROOT / "kernel/init/handover_replica.c"),
                str(ROOT / "kernel/init/critical_object.c"),
                "-o", str(executable),
            ], check=True, timeout=30)
            subprocess.run([str(executable)], check=True, timeout=10)

    def test_replication_is_static_strict_and_fail_closed(self):
        source = (ROOT / "kernel/init/handover_replica.c").read_text()
        self.assertIn("critical_object_t protected_replica", source)
        self.assertIn("next->sequence != current.sequence + 1U", source)
        self.assertIn("new_epoch != state.epoch + 1U", source)
        self.assertNotIn("k_malloc", source)
        self.assertNotIn("k_free", source)


if __name__ == "__main__":
    unittest.main()
