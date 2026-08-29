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

    def test_ahci_batch_reads_preserve_pending_journal_view(self):
        batch = self.source[self.source.index("bool ata_read_sectors("):
                            self.source.index("bool ata_read_sector(")]
        self.assertIn("ata_transaction_begin()", batch)
        self.assertIn("ata_journal_range_has_pending", batch)
        self.assertIn("ata_read_pending_range", batch)
        self.assertIn("ahci_read_sectors(parent, absolute, count, buffer)",
                      batch)
        self.assertIn("ahci_read_sectors(ahci_drive, lba, count, buffer)",
                      batch)
        self.assertLess(batch.index("ata_journal_range_has_pending"),
                        batch.index("ahci_read_sectors(parent"))

    def test_unpublished_write_is_bound_to_exact_active_ahci_journal(self):
        start = self.source.index(
            "bool ata_write_unpublished_sectors_verified("
        )
        end = self.source.index("static bool ata_write_sector_journaled", start)
        body = self.source[start:end]
        for token in (
            "ata_journal.transaction_depth != 0U",
            "ata_journal.volume_start_lba == absolute_volume",
            "ata_journal.volume_end_lba == absolute_volume + volume_sectors",
            "!ata_unpublished_overlaps_journal",
            "!ata_journal_range_has_pending",
            "storage_write_begin",
            "ahci_write_unpublished_sectors_verified",
            "storage_write_end(result)",
        ):
            self.assertIn(token, body)
        resolver = self.source[
            self.source.index("static drive_t *ata_unpublished_ahci_parent"):
            start
        ]
        self.assertIn("parent->type != DRIVE_TYPE_AHCI", resolver)
        self.assertIn("ata_compat_ahci_drive(base)", resolver)

    def test_fat_cache_is_fixed_identity_bound_and_transaction_safe(self):
        fat = (ROOT / "fs/fat32/fat32.c").read_text(encoding="utf-8")
        self.assertIn("FAT32_FAT_CACHE_ENTRIES 8U", fat)
        self.assertIn("entry->base == ata_base_address", fat)
        self.assertIn("entry->is_master == ata_is_master", fat)
        self.assertIn("entry->partition_lba == partition_lba_offset", fat)
        reader = fat[
            fat.index("static bool fat32_read_fat_sector"):
            fat.index("#define FAT32_OPERATION_LOCK_TIMEOUT_MS")
        ]
        self.assertLess(reader.index("ata_journal_transaction_active()"),
                        reader.index("for (uint32_t index = 0U"))
        writer = fat[
            fat.index("bool fat32_write_sector"):
            fat.index("// Forward declarations")
        ]
        self.assertIn("fat32_fat_cache_invalidate();", writer)

    def test_drive_selection_uses_status_polling_not_fixed_50ms_delay(self):
        read = self.source[self.source.index("bool ata_read_sector") :]
        read = read[:read.index("void ata_reset_error_counter")]
        self.assertNotIn("pit_delay(50)", read)
        self.assertIn("wait_for_drive_ready", read)

    def test_flush_has_settling_delay_and_bounded_error_diagnostics(self):
        start = self.source.index("static bool ata_wait_flush_complete(")
        end = self.source.index("static bool ata_write_sector_impl(", start)
        flush = self.source[start:end]
        self.assertIn("pit_monotonic_ms()", flush)
        self.assertIn("ATA_FLUSH_MAX_POLLS", flush)
        self.assertIn("status == 0U || status == 0xFFU", flush)
        self.assertIn("ATA_STATUS_ERR", flush)
        self.assertIn("ATA_STATUS_DF", flush)
        self.assertIn("ATA_STATUS_DRQ", flush)
        self.assertIn("ATA_FLUSH_FAILED", flush)
        self.assertIn("ata_selection_delay(base);", flush)
        self.assertNotIn("wait_for_drive_ready(base", flush)

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
