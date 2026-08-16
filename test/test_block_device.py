import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class BlockDeviceContractTests(unittest.TestCase):
    def read(self, path):
        return (ROOT / path).read_text(encoding="utf-8")

    def test_transport_neutral_contract_validates_ranges_and_dispatches(self):
        header = self.read("drivers/block/block_device.h")
        source = self.read("drivers/block/block_device.c")
        self.assertIn("block_device_read_sector", header)
        self.assertIn("block_device_write_sector", header)
        self.assertIn("block_device_flush", header)
        self.assertIn("block_device_read_sectors", header)
        self.assertIn("block_device_write_sectors", header)
        self.assertIn("sector < drive->sectors", source)
        self.assertIn("DRIVE_TYPE_ATA", source)
        self.assertIn("DRIVE_TYPE_FDD", source)
        self.assertIn("DRIVE_TYPE_AHCI", source)
        self.assertIn("DRIVE_TYPE_PARTITION", source)
        self.assertIn("partition_parent", source)
        self.assertIn("ata_read_sector_fresh", source)
        self.assertIn("fdc_read_sector", source)
        self.assertIn("ahci_read_sector", source)
        self.assertIn("ahci_write_sector", source)
        self.assertIn("ahci_flush", source)


if __name__ == "__main__":
    unittest.main()
