import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class Fat12JournalContracts(unittest.TestCase):
    def read(self, path):
        return (ROOT / path).read_text(encoding="utf-8")

    def test_journal_is_fixed_and_crc_protected(self):
        header = self.read("fs/fat12/fat12_journal.h")
        source = self.read("fs/fat12/fat12_journal.c")
        self.assertIn("FAT12_JOURNAL_MAX_ENTRIES 8U", header)
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
        self.assertIn("FAT12_JOURNAL_CLEAN", recover)

    def test_journal_rejects_header_targets(self):
        source = self.read("fs/fat12/fat12_journal.c")
        record = source[source.index("bool fat12_journal_record"):
                        source.index("bool fat12_journal_commit")]
        self.assertIn("target_sector == journal->primary_header_sector", record)
        self.assertIn("target_sector == journal->mirror_header_sector", record)


if __name__ == "__main__":
    unittest.main()
