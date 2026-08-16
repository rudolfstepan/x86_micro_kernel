import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class Fat12JournalContracts(unittest.TestCase):
    def read(self, path):
        return (ROOT / path).read_text(encoding="utf-8")

    def test_journal_is_fixed_and_crc_protected(self):
        header = self.read("fs/fat12/fat12_journal.h")
        source = self.read("fs/fat12/fat12_journal.c")
        self.assertIn("FAT12_JOURNAL_MAX_ENTRIES 64U", header)
        self.assertIn("FAT12_JOURNAL_VERSION 2U", header)
        self.assertIn("metadata_crc32", header)
        self.assertIn("fat12_journal_crc32", source)
        self.assertIn("header_valid", source)
        self.assertIn("primary_header_sector", source)
        self.assertIn("mirror_header_sector", source)

    def test_record_uses_two_512_byte_sectors_and_recovery_checks_crc(self):
        source = self.read("fs/fat12/fat12_journal.c")
        record = source[source.index("bool fat12_journal_record"):
                        source.index("bool fat12_journal_commit")]
        recover = source[source.index("bool fat12_journal_recover"):]
        self.assertIn("index * 2U", record)
        self.assertIn("FAT12_JOURNAL_SECTOR_SIZE", record)
        self.assertIn("data_crc32 != fat12_journal_crc32", recover)
        self.assertIn("memcmp(old_sector, verify", recover)
        self.assertIn("metadata_crc != fat12_journal_crc32", recover)
        self.assertIn("FAT12_JOURNAL_CLEAN", recover)

    def test_every_persistent_journal_write_is_read_back(self):
        source = self.read("fs/fat12/fat12_journal.c")
        self.assertIn("static bool write_verified", source)
        self.assertIn("memcmp(data, verify, sizeof(verify)) == 0", source)
        self.assertIn("write_verified(read, write, context", source)
        self.assertIn("first.sequence == second.sequence", source)
        self.assertIn("memcmp(&first, &second, sizeof(first)) != 0", source)

    def test_vfs_orders_one_bounded_transaction(self):
        core = self.read("fs/fat12/fat12.c")
        vfs = self.read("fs/fat12/fat12_vfs_adapter.c")
        self.assertIn("fat12_transaction_begin", core)
        self.assertIn("fat12_transaction_commit", core)
        self.assertIn("fat12_transaction_fail", core)
        begin = vfs.index("fat12_transaction_begin(maximum_sectors)")
        data = vfs.index("fat12_write_bytes(start, offset", begin)
        fat = vfs.index("fat12_sync_fat()", data)
        directory = vfs.index("fat12_write_entry(&location", fat)
        replica = vfs.index("fat12_publish_critical_replica", directory)
        commit = vfs.index("fat12_transaction_commit()", replica)
        self.assertLess(begin, data)
        self.assertLess(data, fat)
        self.assertLess(fat, directory)
        self.assertLess(directory, replica)
        self.assertLess(replica, commit)

    def test_journal_rejects_header_targets(self):
        source = self.read("fs/fat12/fat12_journal.c")
        record = source[source.index("bool fat12_journal_record"):
                        source.index("bool fat12_journal_commit")]
        self.assertIn("target_sector == journal->primary_header_sector", record)
        self.assertIn("target_sector == journal->mirror_header_sector", record)
        self.assertIn("entries[index].target_sector == target_sector", record)


if __name__ == "__main__":
    unittest.main()
