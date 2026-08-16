import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class PhysicalStorageProbeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.kernel = (ROOT / "kernel/init/kernel.c").read_text(encoding="utf-8")
        cls.ahci = (ROOT / "drivers/block/ahci.c").read_text(encoding="utf-8")

    def test_zero_drive_boot_fails_before_partition_and_vfs_startup(self):
        fdc = self.kernel.index("fdd_detect_drives()")
        zero = self.kernel.index("if (drive_count <= 0)", fdc)
        partition = self.kernel.index("partition_discover()", zero)
        mount = self.kernel.index("auto_mount_all_drives", partition)
        self.assertLess(fdc, zero)
        self.assertLess(zero, partition)
        self.assertLess(partition, mount)
        block = self.kernel[zero:partition]
        self.assertIn("ata_probe_diagnostics()", block)
        self.assertIn("ahci_probe_diagnostics()", block)
        self.assertIn("No physical block storage device detected", block)

    def test_ahci_link_wait_has_one_controller_wide_deadline(self):
        match = re.search(
            r"static uint32_t ahci_wait_sata_ports\([^;]+?\)\s*\{",
            self.ahci,
            re.DOTALL,
        )
        self.assertIsNotNone(match)
        start = match.end()
        depth = 1
        cursor = start
        while cursor < len(self.ahci) and depth:
            depth += self.ahci[cursor] == "{"
            depth -= self.ahci[cursor] == "}"
            cursor += 1
        body = self.ahci[start:cursor - 1]
        self.assertEqual(body.count("uint64_t start = pit_monotonic_ms()"), 1)
        self.assertIn("now - start >= AHCI_LINK_TIMEOUT_MS", body)
        self.assertIn("pit_delay(AHCI_LINK_POLL_MS)", body)
        self.assertNotRegex(body, r"for \([^)]*port[^)]*\)[^{]*\{[^}]*uint64_t start")


if __name__ == "__main__":
    unittest.main()
