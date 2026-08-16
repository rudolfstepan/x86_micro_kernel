import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class PartitionContractTests(unittest.TestCase):
    def read(self, path):
        return (ROOT / path).read_text(encoding="utf-8")

    def test_mbr_children_are_bounded_and_fail_closed(self):
        source = self.read("drivers/block/partition.c")
        drives = self.read("drivers/bus/drives.h")
        block = self.read("drivers/block/block_device.c")
        kernel = self.read("kernel/init/kernel.c")
        self.assertIn("DRIVE_TYPE_PARTITION", drives)
        self.assertIn("MBR_PRIMARY_COUNT 4U", source)
        self.assertIn("type == 0xEEU", source)
        self.assertIn("candidates_overlap", source)
        self.assertIn("(uint64_t)first + count > parent->sectors", source)
        self.assertIn("(size_t)drive_count + candidate_count > MAX_DRIVES", source)
        self.assertIn("child->parent_resource", source)
        self.assertIn("child->lba_offset", source)
        self.assertIn("partition_parent", block)
        self.assertIn("sector > UINT32_MAX - drive->lba_offset", block)
        self.assertIn("partition_discover_mbr();", kernel)

    def test_partition_compatibility_path_translates_legacy_io(self):
        ata = self.read("drivers/block/ata.c")
        filesystem = self.read("fs/vfs/filesystem.c")
        self.assertIn("ata_compat_partition_drive", ata)
        self.assertIn("ata_partition_translate", ata)
        self.assertIn("partition->lba_offset + lba", ata)
        self.assertIn("drive->has_partitions", filesystem)
        self.assertIn("DRIVE_TYPE_PARTITION", filesystem)


if __name__ == "__main__":
    unittest.main()
