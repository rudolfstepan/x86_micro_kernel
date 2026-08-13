import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class ReistWatchdogTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.watchdog = (ROOT / "kernel/init/watchdog.c").read_text(encoding="utf-8")
        cls.scheduler = (ROOT / "kernel/sched/scheduler.c").read_text(encoding="utf-8")
        cls.pit = (ROOT / "kernel/time/pit.c").read_text(encoding="utf-8")
        cls.fatal = (ROOT / "kernel/init/fatal.c").read_text(encoding="utf-8")

    def test_feed_requires_scheduler_progress_not_only_timer_interrupts(self):
        self.assertIn("watchdog_health_progress();", self.scheduler)
        self.assertIn("watchdog_clock_tick(timer_tick_count);", self.pit)
        self.assertIn("current == observed_epoch", self.watchdog)
        self.assertIn("WATCHDOG_FEED_INTERVAL_MS", self.watchdog)

    def test_fatal_path_arms_short_timeout_and_stops_feeding(self):
        self.assertIn("IB700_TIMEOUT_2_SECONDS", self.watchdog)
        self.assertIn("backend_available = false", self.watchdog)
        self.assertIn("watchdog_fatal_handoff()", self.fatal)

    def test_backend_is_explicitly_qemu_profile_only(self):
        self.assertIn("#ifdef QEMU_BUILD", self.watchdog)
        self.assertIn("external backend required", (ROOT / "kernel/init/kernel.c").read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
