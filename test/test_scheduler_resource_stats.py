"""Contracts for bounded task-slot high-water diagnostics."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


class SchedulerResourceStatsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (ROOT / "kernel/sched/scheduler.h").read_text(encoding="utf-8")
        cls.scheduler = (ROOT / "kernel/sched/scheduler.c").read_text(encoding="utf-8")
        cls.syscalls = (ROOT / "kernel/syscall/syscall_table.c").read_text(encoding="utf-8")
        cls.sdk = (ROOT / "userspace/sdk/include/x86os.h").read_text(encoding="utf-8")
        cls.guest = (ROOT / "examples/userspace/guest_test.c").read_text(encoding="utf-8")

    def test_versioned_fixed_size_abi_is_append_only(self) -> None:
        self.assertIn("SCHEDULER_RESOURCE_STATS_VERSION 1U", self.header)
        self.assertIn("X86OS_SCHEDULER_STATS_VERSION 1U", self.sdk)
        self.assertRegex(self.sdk, r"X86OS_SYS_SCHEDULER_STATS\s*=\s*84")
        self.assertIn("sizeof(x86os_scheduler_stats_t) == 32U",
                      (ROOT / "userspace/sdk/x86os.c").read_text(encoding="utf-8"))

    def test_snapshot_and_counters_are_bounded_and_saturating(self) -> None:
        self.assertIn("for (int index = 0; index < num_tasks; ++index)", self.scheduler)
        self.assertIn("task_capacity_rejections != UINT32_MAX", self.scheduler)
        self.assertIn("if (active > peak_active_tasks)", self.scheduler)
        self.assertIn(".task_capacity = MAX_TASKS", self.scheduler)
        self.assertIn(".supervised_reserve = SUPERVISED_TASK_RESERVE", self.scheduler)

    def test_syscall_validates_before_copyout(self) -> None:
        start = self.syscalls.index("static int syscall_scheduler_stats(")
        end = self.syscalls.index("\n}", start)
        body = re.sub(r"\s+", " ", self.syscalls[start:end])
        self.assertIn("version != SCHEDULER_RESOURCE_STATS_VERSION", body)
        self.assertIn("user_size < sizeof(scheduler_resource_stats_t)", body)
        self.assertIn("copy_to_user(user_stats, &stats, sizeof(stats))", body)

    def test_guest_proves_exhaustion_peak_and_reclaim(self) -> None:
        self.assertIn("x86os_scheduler_stats(&before)", self.guest)
        self.assertIn("exhausted.capacity_rejections <= before.capacity_rejections",
                      self.guest)
        self.assertIn("reclaimed.active_tasks < exhausted.active_tasks", self.guest)
        self.assertIn("reclaimed.peak_active_tasks >= exhausted.peak_active_tasks",
                      self.guest)


if __name__ == "__main__":
    unittest.main()
