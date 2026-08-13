import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class ReistPersistentCrashTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.header = (ROOT / "include/kernel/fatal.h").read_text(encoding="utf-8")
        cls.fatal = (ROOT / "kernel/init/fatal.c").read_text(encoding="utf-8")
        cls.kernel = (ROOT / "kernel/init/kernel.c").read_text(encoding="utf-8")

    def test_record_uses_reserved_fixed_page_and_checksum(self):
        self.assertIn("FATAL_CRASH_RECORD_ADDRESS 0x00030000U", self.header)
        self.assertIn("memory_reserve_region(FATAL_CRASH_RECORD_ADDRESS", self.kernel)
        self.assertIn("persistent_record_valid", self.fatal)
        self.assertIn("destination->magic = record.magic", self.fatal)
        self.assertIn("CMOS_CRASH_RECORD_BASE 0x38U", self.fatal)
        self.assertIn("cmos_write_record(&record)", self.fatal)

    def test_boot_consumes_previous_valid_record(self):
        self.assertIn("fatal_boot_recover_record();", self.kernel)
        self.assertIn('"REIST_RECOVERY PREVIOUS_FATAL\\n"', self.fatal)
        self.assertIn("record->magic = 0", self.fatal)
        self.assertIn("fatal_crash_record_t nvram_record = cmos_read_record()", self.fatal)

    def test_fatal_path_is_bounded_and_resets(self):
        self.assertIn("EMERGENCY_SERIAL_POLL_BUDGET", self.fatal)
        self.assertIn("outb(0x64U, 0xFEU)", self.fatal)
        self.assertIn('"lidt %0; int $3"', self.fatal)
        for forbidden in ("printf(", "k_malloc(", "vfs_", "serial_write_char("):
            self.assertNotIn(forbidden, self.fatal)


if __name__ == "__main__":
    unittest.main()
