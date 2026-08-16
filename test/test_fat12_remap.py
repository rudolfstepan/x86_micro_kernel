import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class Fat12RemapContracts(unittest.TestCase):
    def read(self, path):
        return (ROOT / path).read_text(encoding="utf-8")

    def test_remap_is_fixed_mirrored_and_crc_protected(self):
        header = self.read("fs/fat12/fat12_remap.h")
        source = self.read("fs/fat12/fat12_remap.c")
        self.assertIn("FAT12_REMAP_MAX_ENTRIES 16U", header)
        self.assertIn("primary_sector", header)
        self.assertIn("mirror_sector", header)
        self.assertIn("header_shape_valid", source)
        self.assertIn("write_headers", source)
        self.assertIn("table_crc", source)
        self.assertIn("write_verified", source)

    def test_remap_rejects_duplicate_targets_and_has_bounded_lookup(self):
        source = self.read("fs/fat12/fat12_remap.c")
        add = source[source.index("bool fat12_remap_add"):
                     source.index("bool fat12_remap_lookup")]
        lookup = source[source.index("bool fat12_remap_lookup"):]
        self.assertIn("entry_count >= FAT12_REMAP_MAX_ENTRIES", add)
        self.assertIn("replacement_sector == replacement_sector", add)
        self.assertIn("replacement_sector == bad_sector", add)
        self.assertIn("for (uint32_t i = 0U; i < table->header.entry_count", lookup)

    def test_fat12_confirms_defects_and_uses_reserved_spares(self):
        header = self.read("fs/fat12/fat12.h")
        source = self.read("fs/fat12/fat12.c")
        self.assertIn("FAT12_DEFECT_CONFIRM_READS  3U", header)
        self.assertIn("fat12_confirm_sector_defect", source)
        self.assertIn("fat12_quarantine_data_cluster", source)
        self.assertIn("FAT12_BAD_CLUSTER", source)
        self.assertIn("fat12_install_sector_remap", source)
        self.assertIn("FAT12_REMAP_SPARE_COUNT", source)
        self.assertIn("fat12_remap_add(&fat12->remap", source)


if __name__ == "__main__":
    unittest.main()
