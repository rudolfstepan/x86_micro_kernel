import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class ReistStorageDomainTests(unittest.TestCase):
    def test_storage_domain_is_idle_aware_and_never_restarts(self):
        source = (ROOT / "kernel/init/storage_safety.c").read_text(encoding="utf-8")
        self.assertIn('supervisor_register("storage-write"', source)
        self.assertIn(".restart_budget = 0", source)
        self.assertIn("supervisor_report_idle(storage_supervisor_handle)", source)
        self.assertIn("ata_fence_writes();", source)
        self.assertIn("fdd_fence_writes();", source)
        self.assertIn("ata_writes_quiescent()", source)
        self.assertIn("fdd_writes_quiescent()", source)
        self.assertIn("critical_object_t storage_control", source)
        self.assertIn("critical_object_read(&storage_control", source)
        self.assertIn("critical_object_update(&storage_control", source)
        self.assertIn("storage_integrity_failed = true", source)

    def test_ata_write_is_supervised_and_flush_failure_is_fatal(self):
        source = (ROOT / "drivers/block/ata.c").read_text(encoding="utf-8")
        write = source[source.index("bool ata_write_sector("):source.index("drive_t* ata_get_drive")]
        self.assertIn("storage_write_begin(pit_monotonic_ms())", write)
        self.assertIn("storage_write_end()", write)
        flush = source[source.index("// Flush cache"):source.index("bool ata_write_sector(")]
        self.assertLess(flush.index("Cache flush timeout"),
                        flush.index("return false;"))
        self.assertIn("ATA_ALT_STATUS", source[source.index("bool ata_writes_quiescent"):])

    def test_fdd_write_is_supervised_and_fence_is_read_back(self):
        source = (ROOT / "drivers/block/fdd.c").read_text(encoding="utf-8")
        write = source[source.index("bool fdc_write_sectors("):source.index("bool fdd_write_sector(")]
        self.assertIn("storage_write_begin(pit_monotonic_ms())", write)
        self.assertIn("storage_write_end()", write)
        fence = source[source.index("void fdd_fence_writes"):source.index("static bool fdc_calibrate")]
        self.assertIn("FDD_DOR", fence)
        self.assertIn("FDD_MSR", fence)
        self.assertIn("MSR_CB", fence)

    def test_supervision_precedes_filesystem_mounts(self):
        source = (ROOT / "kernel/init/kernel.c").read_text(encoding="utf-8")
        self.assertLess(source.index("ata_detect_drives()"), source.index("storage_safety_init("))
        self.assertLess(source.index("fdd_detect_drives()"), source.index("storage_safety_init("))
        self.assertLess(source.index("storage_safety_init("), source.index("auto_mount_all_drives()"))


if __name__ == "__main__":
    unittest.main()
