import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class FloppyDriverSourceTests(unittest.TestCase):
    def test_sector_io_does_not_spin_up_for_every_successful_sector(self):
        source = (ROOT / "drivers" / "block" / "fdd.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("fdd_prepare_drive(drive);", source)
        self.assertIn("FDD_MOTOR_IDLE_MS", source)
        self.assertIn("fdd_cached_track", source)
        read_success = source.split("bool fdc_read_sector", 1)[1].split(
            "bool fdd_write_sector", 1
        )[0]
        self.assertIn("fdd_last_activity[drive] = pit_ticks();", read_success)
        self.assertNotIn("delay_ms(500)", read_success)

    def test_root_directory_can_stop_at_fat_end_marker(self):
        source = (ROOT / "fs" / "fat12" / "fat12.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("if (end_of_directory) break;", source)


if __name__ == "__main__":
    unittest.main()
