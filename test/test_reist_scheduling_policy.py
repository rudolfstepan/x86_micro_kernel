import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class ReistSchedulingPolicyTests(unittest.TestCase):
    def test_weighted_fixed_priority_policy_is_bounded(self):
        with tempfile.TemporaryDirectory() as directory:
            output = pathlib.Path(directory) / "scheduling-policy-test.exe"
            subprocess.run([
                "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I.", "kernel/sched/scheduling_policy.c",
                "test/test_scheduling_policy_host.c", "-o", str(output),
            ], cwd=ROOT, check=True, capture_output=True)
            result = subprocess.run([str(output)], cwd=ROOT, check=True,
                                    capture_output=True, text=True)
            self.assertIn("SCHEDULING_POLICY_OK", result.stdout)

    def test_scheduler_assigns_classes_without_dynamic_allocation(self):
        scheduler = (ROOT / "kernel/sched/scheduler.c").read_text(
            encoding="utf-8")
        policy = (ROOT / "kernel/sched/scheduling_policy.c").read_text(
            encoding="utf-8")
        self.assertIn("process == NULL ? SCHEDULER_CLASS_SAFETY", scheduler)
        self.assertIn("supervised ? SCHEDULER_CLASS_SERVICE", scheduler)
        self.assertIn("scheduling_class_cursors", scheduler)
        self.assertIn("scheduler_policy_select_cycle(", scheduler)
        self.assertIn("scheduling_class_cycle_cursor", scheduler)
        self.assertNotIn("k_malloc", policy)
        self.assertNotIn("while (", policy)

    def test_absolute_windows_throttle_overloaded_classes(self):
        policy = (ROOT / "kernel/sched/scheduling_policy.c").read_text(
            encoding="utf-8")
        scheduler = (ROOT / "kernel/sched/scheduler.c").read_text(
            encoding="utf-8")
        self.assertIn("SCHEDULER_WINDOW_MS 100U", (
            ROOT / "kernel/sched/scheduling_policy.h").read_text(
                encoding="utf-8"))
        self.assertIn("scheduler_policy_window_charge", scheduler)
        self.assertIn("scheduler_policy_class_allowed", scheduler)
        self.assertNotIn("k_malloc", policy)

    def test_clock_regression_latches_until_explicit_init(self):
        header = (ROOT / "kernel/sched/scheduling_policy.h").read_text(
            encoding="utf-8")
        policy = (ROOT / "kernel/sched/scheduling_policy.c").read_text(
            encoding="utf-8")
        self.assertIn("SCHEDULER_WINDOW_FAULT_CLOCK_REGRESSION", header)
        self.assertIn("clock_anomaly_count", header)
        self.assertIn("window->fault_flags |=", policy)
        self.assertIn("if (window->fault_flags != 0U) return false;", policy)
        rollover = policy[policy.index("bool rolled ="):policy.index(
            "bool scheduler_policy_class_allowed")]
        self.assertIn("uint32_t fault_flags = window->fault_flags", rollover)
        self.assertIn("window->fault_flags = fault_flags", rollover)

    def test_ipc_waits_use_generation_scoped_priority_inheritance(self):
        ipc = (ROOT / "kernel/ipc/ipc.c").read_text(encoding="utf-8")
        scheduler = (ROOT / "kernel/sched/scheduler.c").read_text(
            encoding="utf-8")
        self.assertIn("counterpart_identity_locked", ipc)
        self.assertEqual(ipc.count("scheduler_set_wait_owner_locked("), 2)
        self.assertEqual(ipc.count("scheduler_clear_wait_owner_locked();"), 2)
        self.assertIn("blocked_owner_generation", scheduler)
        self.assertIn("scheduler_policy_inherit", scheduler)


if __name__ == "__main__":
    unittest.main()
