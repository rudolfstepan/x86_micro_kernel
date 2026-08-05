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

    def test_runtime_driver_and_fat12_batch_track_reads(self):
        driver = (ROOT / "drivers" / "block" / "fdd.c").read_text(
            encoding="utf-8"
        )
        fat12 = (ROOT / "fs" / "fat12" / "fat12.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("bool fdc_read_sectors", driver)
        self.assertIn("transfer_size", driver)
        self.assertIn("fdc_read_logical_range", fat12)
        self.assertIn("complete_clusters", fat12)

    def test_root_directory_can_stop_at_fat_end_marker(self):
        source = (ROOT / "fs" / "fat12" / "fat12.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("if (end_of_directory) break;", source)

    def test_bios_loader_batches_floppy_reads_without_crossing_tracks(self):
        source = (
            ROOT / "arch" / "x86" / "boot" / "bios" / "stage2_bios.asm"
        ).read_text(encoding="utf-8")
        chs_reader = source.split("read_bounce_chs:", 1)[1].split(
            "verify_kernel_crc:", 1
        )[0]
        self.assertIn("chs_transfer_count", chs_reader)
        self.assertIn("sub eax, edx", chs_reader)
        self.assertIn("mov ah, 0x02", chs_reader)
        self.assertNotIn("mov ax, 0x0201", chs_reader)

    def test_floppy_kernel_is_verified_once_and_then_loaded_from_ram(self):
        source = (
            ROOT / "arch" / "x86" / "boot" / "bios" / "stage2_bios.asm"
        ).read_text(encoding="utf-8")
        self.assertIn("KERNEL_CACHE_ADDRESS", source)
        self.assertIn("cache_write_address", source)
        self.assertIn("cmp byte [kernel_cached], 1", source)
        self.assertIn("mov dword [pm_source], KERNEL_CACHE_ADDRESS", source)


if __name__ == "__main__":
    unittest.main()
