import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class AtaDriverSourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = (ROOT / "drivers/block/ata.c").read_text(encoding="utf-8")

    def test_repeated_sector_reads_use_a_bounded_cache(self):
        self.assertIn("ATA_READ_CACHE_ENTRIES 32", self.source)
        self.assertIn("memcpy(buffer, cached->data, SECTOR_SIZE)", self.source)
        self.assertIn("memcpy(cached->data, buffer, SECTOR_SIZE)", self.source)

    def test_writes_invalidate_matching_cached_sector(self):
        write = self.source[self.source.index("bool ata_write_sector") :]
        self.assertIn("cached->valid = false", write)

    def test_drive_selection_uses_status_polling_not_fixed_50ms_delay(self):
        read = self.source[self.source.index("bool ata_read_sector") :]
        read = read[:read.index("void ata_reset_error_counter")]
        self.assertNotIn("pit_delay(50)", read)
        self.assertIn("wait_for_drive_ready", read)

    def test_ls_uses_authoritative_ring3_directory_reads(self):
        fat32 = (ROOT / "fs/fat32/fat32_vfs_adapter.c").read_text(
            encoding="utf-8"
        )
        ls = (ROOT / "userspace/programs/ls.c").read_text(encoding="utf-8")
        self.assertIn("fat32_vfs_readdir_batch_unlocked", fat32)
        self.assertIn(".readdir_batch = fat32_vfs_readdir_batch", fat32)
        self.assertIn("reist_vfs_readdir_at(", ls)
        self.assertNotIn("x86os_readdir", ls)


if __name__ == "__main__":
    unittest.main()
