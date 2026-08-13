import binascii
import io
import pathlib
import struct
import unittest

from scripts.create_native_boot_image import write_fat32_volume


ROOT = pathlib.Path(__file__).resolve().parents[1]
SECTOR = 512
MAGIC = 0x4A545352


class ReistUndoJournalTests(unittest.TestCase):
    def test_builder_provisions_clean_crc_protected_journal(self):
        image = io.BytesIO(bytearray(16 * 1024 * 1024))
        partition = 2048
        write_fat32_volume(image, partition, 30000, 0x12345678)
        image.seek((partition + 8) * SECTOR)
        header = image.read(SECTOR)
        magic, version, state, target, data_crc, sequence, header_crc = \
            struct.unpack_from("<7I", header)
        self.assertEqual((magic, version, state), (MAGIC, 1, 0))
        self.assertEqual((target, data_crc, sequence), (0, 0, 0))
        self.assertEqual(header_crc, binascii.crc32(header[:24]) & 0xFFFFFFFF)
        image.seek((partition + 9) * SECTOR)
        self.assertEqual(image.read(SECTOR), bytes(SECTOR))

    def test_kernel_orders_undo_data_active_target_then_clean(self):
        source = (ROOT / "drivers/block/ata.c").read_text(encoding="utf-8")
        start = source.index("static bool ata_write_sector_journaled")
        end = source.index("bool ata_journal_attach", start)
        body = source[start:end]
        body = body[body.index("uint8_t old_data"):]
        ordered = [
            "ata_read_sector_impl(base, lba",
            "ata_write_sector_impl(base, ata_journal.data_lba",
            "active.state = ATA_JOURNAL_ACTIVE",
            "ata_write_sector_impl(base, ata_journal.header_lba",
            "ata_write_sector_impl(base, lba, buffer",
            "ata_journal_clear()",
        ]
        position = -1
        for token in ordered:
            next_position = body.index(token)
            self.assertGreater(next_position, position, token)
            position = next_position

    def test_recovery_validates_crc_before_restoring_and_clearing(self):
        source = (ROOT / "drivers/block/ata.c").read_text(encoding="utf-8")
        start = source.index("bool ata_journal_attach")
        end = source.index("bool ata_write_sector(", start)
        body = source[start:end]
        self.assertIn("record.magic != ATA_JOURNAL_MAGIC", body)
        self.assertIn("ata_journal_record_valid(&record)", body)
        self.assertIn("ata_journal_crc32(old_data", body)
        self.assertLess(body.index("ata_journal_crc32(old_data"),
                        body.index("record.target_lba, old_data"))
        self.assertLess(body.index("record.target_lba, old_data"),
                        body.index("ata_journal_clear()"))
        self.assertIn("if (!result) ata_fence_writes();", body)

    def test_recovery_runs_before_mutable_fat_metadata_is_consumed(self):
        source = (ROOT / "fs/fat32/fat32.c").read_text(encoding="utf-8")
        self.assertLess(source.index("ata_journal_attach("),
                        source.index("candidate_boot.fs_info"))

    def test_power_loss_recovery_is_a_ci_gate(self):
        runner = (ROOT / "scripts/test_journal_recovery.py").read_text(encoding="utf-8")
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        workflow = (ROOT / ".github/workflows/build.yml").read_text(encoding="utf-8")
        self.assertIn('"--persistent"', runner)
        self.assertIn("restored != expected or state != 0", runner)
        self.assertIn("test-smoke-journal-recovery:", makefile)
        self.assertIn("make test-smoke-journal-recovery", workflow)


if __name__ == "__main__":
    unittest.main()
