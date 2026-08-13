import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class ReistSupervisorTests(unittest.TestCase):
    def test_supervisor_state_machine_and_budgets(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "supervisor-test.exe"
            subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-DREIST_HOST_TEST", "-I", str(ROOT),
                 str(ROOT / "kernel/init/critical_object.c"),
                 str(ROOT / "kernel/init/supervisor.c"),
                 str(ROOT / "test/test_supervisor_host.c"),
                 "-o", str(executable)], check=True, capture_output=True,
                text=True, timeout=30,
            )
            result = subprocess.run([str(executable)], timeout=10)
            self.assertEqual(result.returncode, 0)

    def test_contract_is_fixed_bounded_fenced_and_ecc_protected(self):
        header = (ROOT / "include/kernel/supervisor.h").read_text(encoding="utf-8")
        source = (ROOT / "kernel/init/supervisor.c").read_text(encoding="utf-8")
        self.assertIn("SUPERVISOR_MAX_DOMAINS 8U", header)
        self.assertIn("critical_object_t protected_state", source)
        self.assertIn("SUPERVISOR_EVENT_FENCE_REQUIRED", source)
        self.assertIn("fence_ops.apply(fence_ops.context)", source)
        self.assertIn("fence_ops.verify(fence_ops.context)", source)
        self.assertIn("SUPERVISOR_CHECK_INTERVAL_MS 10U", source)
        self.assertIn("state.state == SUPERVISOR_ISOLATED", source)
        self.assertNotIn("supervisor_ack_fenced", header)
        self.assertIn("restart_count >= state.restart_budget", source)
        self.assertIn("state->epoch != handle.epoch", source)
        self.assertNotIn("k_malloc", source)
        kernel = (ROOT / "kernel/init/kernel.c").read_text(encoding="utf-8")
        self.assertIn("supervisor_init();", kernel)

    def test_pit_drives_bounded_supervisor_deadline_checks(self):
        pit = (ROOT / "kernel/time/pit.c").read_text(encoding="utf-8")
        self.assertIn("supervisor_clock_tick(timer_tick_count);", pit)


if __name__ == "__main__":
    unittest.main()
