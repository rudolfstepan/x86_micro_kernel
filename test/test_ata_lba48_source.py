import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class AtaLba48ContractTests(unittest.TestCase):
    def test_identify_and_ext_commands_are_bounded(self):
        header = (ROOT / "drivers/block/ata.h").read_text(encoding="utf-8")
        source = (ROOT / "drivers/block/ata.c").read_text(encoding="utf-8")
        drives = (ROOT / "drivers/bus/drives.h").read_text(encoding="utf-8")
        self.assertIn("ATA_READ_SECTORS_EXT 0x24", header)
        self.assertIn("ATA_WRITE_SECTORS_EXT 0x34", header)
        self.assertIn("ATA_FLUSH_CACHE_EXT", header)
        self.assertIn("identify_data[83] & (1U << 10U)", source)
        self.assertIn("identify_data[100]", source)
        self.assertIn("identify_data[103]", source)
        self.assertIn("sectors > UINT32_MAX ? UINT32_MAX", source)
        self.assertIn("lba48_supported", drives)
        self.assertIn("drive == NULL || !drive->lba48_supported", source)
        self.assertIn("use_lba48 ? ATA_READ_SECTORS_EXT", source)
        self.assertIn("use_lba48 ? ATA_WRITE_SECTORS_EXT", source)

    def test_detection_zero_initializes_extended_drive_metadata(self):
        source = (ROOT / "drivers/block/ata.c").read_text(encoding="utf-8")
        self.assertIn("memset(&temp_drive, 0, sizeof(temp_drive))", source)


if __name__ == "__main__":
    unittest.main()
