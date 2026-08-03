import unittest

from scripts.test_disk_images import FAT32BootSector


def boot_sector(**overrides):
    values = {
        "bytes_per_sector": 512,
        "sectors_per_cluster": 1,
        "reserved_sectors": 1,
        "num_fats": 2,
        "root_entries": 224,
        "total_sectors_16": 2880,
        "total_sectors_32": 0,
        "media_descriptor": 0xF0,
        "sectors_per_fat_16": 9,
        "sectors_per_fat_32": 0,
        "root_cluster": 0,
        "fs_info_sector": 0,
        "backup_boot_sector": 0,
        "volume_label": "TEST",
        "fs_type": "FAT12",
    }
    values.update(overrides)
    return FAT32BootSector(**values)


class BootSectorValidationTests(unittest.TestCase):
    def test_valid_fat12(self):
        self.assertTrue(boot_sector().is_valid_fat12)

    def test_generic_fat_label_does_not_bypass_bpb_checks(self):
        sector = boot_sector(
            bytes_per_sector=123,
            sectors_per_cluster=3,
            num_fats=9,
            root_entries=0,
            fs_type="FAT",
        )
        self.assertFalse(sector.is_valid_fat12)

    def test_filesystem_label_is_only_informational(self):
        self.assertTrue(boot_sector(fs_type="FAT16").is_valid_fat12)

    def test_fat16_geometry_is_not_accepted_as_fat12(self):
        sector = boot_sector(
            root_entries=512,
            total_sectors_16=20000,
            sectors_per_fat_16=80,
            fs_type="FAT12",
        )
        self.assertFalse(sector.is_valid_fat12)

    def test_fat12_rejects_undersized_fat(self):
        self.assertFalse(boot_sector(sectors_per_fat_16=1).is_valid_fat12)

    def test_declared_filesystem_must_fit_image(self):
        sector = boot_sector()
        self.assertTrue(sector.fits_image_size(1474560))
        self.assertFalse(sector.fits_image_size(1474559))

    def test_valid_fat32(self):
        sector = boot_sector(
            reserved_sectors=32,
            root_entries=0,
            total_sectors_16=0,
            total_sectors_32=100000,
            sectors_per_fat_16=0,
            sectors_per_fat_32=800,
            root_cluster=2,
            fs_info_sector=1,
            backup_boot_sector=6,
            fs_type="NOTFAT",
        )
        self.assertTrue(sector.is_valid_fat32)

    def test_fat32_rejects_undersized_fat(self):
        sector = boot_sector(
            reserved_sectors=32,
            root_entries=0,
            total_sectors_16=0,
            total_sectors_32=100000,
            sectors_per_fat_16=0,
            sectors_per_fat_32=1,
            root_cluster=2,
        )
        self.assertFalse(sector.is_valid_fat32)


if __name__ == "__main__":
    unittest.main()
