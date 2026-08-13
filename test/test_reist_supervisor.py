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
        self.assertIn("critical_object_t protected_fence_ops", source)
        self.assertIn("critical_object_t protected_descriptor", source)
        self.assertIn("SUPERVISOR_DESCRIPTOR_VERSION", source)
        self.assertNotIn("bool occupied", source)
        self.assertIn("SUPERVISOR_FENCE_OPS_VERSION", source)
        self.assertIn("fence_ops_read(handle.slot, &fence_ops)", source)
        self.assertIn("SUPERVISOR_EVENT_FENCE_REQUIRED", source)
        self.assertIn("fence_ops.apply(fence_ops.context)", source)
        self.assertIn("fence_ops.verify(fence_ops.context)", source)
        self.assertIn("SUPERVISOR_CHECK_INTERVAL_MS 10U", source)
        self.assertIn("state.state == SUPERVISOR_ISOLATED", source)
        self.assertIn("static uint32_t next_poll_slot", source)
        self.assertIn("(next_poll_slot + offset) % SUPERVISOR_MAX_DOMAINS", source)
        self.assertIn("supervisor_service_one(uint64_t now_ms)", source)
        self.assertIn("KASSERT_NOT_IRQ();", source)
        self.assertIn("KASSERT(irq_enabled());", source)
        service = source[source.index("supervisor_service_one(uint64_t now_ms)"):]
        service = service[:service.index("supervisor_apply_fence(", 1)]
        self.assertNotIn("while (", service)
        self.assertNotIn("supervisor_ack_fenced", header)
        self.assertIn("restart_count >= state.restart_budget", source)
        self.assertIn("state->epoch != handle.epoch", source)
        self.assertNotIn("k_malloc", source)
        kernel = (ROOT / "kernel/init/kernel.c").read_text(encoding="utf-8")
        self.assertIn("supervisor_init();", kernel)

    def test_pit_drives_bounded_supervisor_deadline_checks(self):
        pit = (ROOT / "kernel/time/pit.c").read_text(encoding="utf-8")
        self.assertIn("supervisor_clock_tick(timer_tick_count);", pit)

    def test_reserved_foreground_worker_is_fail_closed(self):
        source = (ROOT / "kernel/init/supervisor.c").read_text(encoding="utf-8")
        kernel = (ROOT / "kernel/init/kernel.c").read_text(encoding="utf-8")
        guest = (ROOT / "examples/userspace/guest_test.c").read_text(encoding="utf-8")
        self.assertIn("static void supervisor_worker(void)", source)
        self.assertIn("supervisor_service_one(pit_monotonic_ms())", source)
        self.assertIn("output_fence_all();", source)
        self.assertIn("scheduler_sleep_ms(SUPERVISOR_CHECK_INTERVAL_MS)", source)
        self.assertIn("if (!supervisor_start_worker())", kernel)
        self.assertIn("int children[5];", guest)


if __name__ == "__main__":
    unittest.main()
