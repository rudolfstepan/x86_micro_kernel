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
        magic, version, state, sequence, entry_count, header_crc = \
            struct.unpack_from("<6I", header)
        self.assertEqual((magic, version, state), (MAGIC, 2, 0))
        self.assertEqual((sequence, entry_count), (0, 0))
        check = bytearray(header)
        struct.pack_into("<I", check, 20, 0)
        self.assertEqual(header_crc, binascii.crc32(check) & 0xFFFFFFFF)
        image.seek((partition + 9) * SECTOR)
        self.assertEqual(image.read(SECTOR), bytes(SECTOR))
        image.seek((partition + 31) * SECTOR)
        self.assertEqual(image.read(SECTOR), header)

    def test_kernel_orders_undo_data_active_target_then_clean(self):
        source = (ROOT / "drivers/block/ata.c").read_text(encoding="utf-8")
        start = source.index("static bool ata_write_sector_journaled")
        end = source.index("bool ata_journal_attach", start)
        body = source[start:end]
        body = body[body.index("uint8_t old_data"):]
        ordered = [
            "ata_journal_read_transport(base, lba",
            "ata_journal_write_transport(base, ata_journal.data_lba + slot",
            "ata_journal_write_active()",
            "ata_journal_write_transport(base, lba, buffer",
            "ata_journal_transaction_end(result)",
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
        self.assertIn("primary.magic == ATA_JOURNAL_MAGIC", body)
        self.assertIn("mirror.magic == ATA_JOURNAL_MAGIC", body)
        self.assertIn("ata_journal_record_valid(&record)", body)
        self.assertIn("record.entries[index].data_crc32", body)
        self.assertIn("data_lba + index", body)
        self.assertIn("ata_journal_write_transport(base, target, data", body)
        self.assertIn("if (!result) ata_fence_writes();", body)

    def test_vfs_transaction_spans_multiple_unique_sector_updates(self):
        ata = (ROOT / "drivers/block/ata.c").read_text(encoding="utf-8")
        fs = (ROOT / "kernel/init/filesystem_safety.c").read_text(encoding="utf-8")
        self.assertIn("#define ATA_JOURNAL_MAX_ENTRIES 20U", ata)
        self.assertIn("ata_journal.entries[i].target_lba == lba", ata)
        self.assertIn("ata_journal.entry_count >= ATA_JOURNAL_MAX_ENTRIES", ata)
        self.assertIn("ata_journal_transaction_begin()", fs)
        self.assertIn("ata_journal_transaction_end(commit)", fs)

    def test_redundant_headers_select_conservatively_and_self_repair(self):
        source = (ROOT / "drivers/block/ata.c").read_text(encoding="utf-8")
        self.assertIn("#define ATA_JOURNAL_MIRROR_OFFSET 31U", source)
        self.assertIn("primary.state != mirror.state", source)
        self.assertIn("primary.state == ATA_JOURNAL_ACTIVE ? primary : mirror", source)
        self.assertIn("else if (result && repair_headers)", source)
        writer = source[source.index("static bool ata_journal_write_record"):
                        source.index("static bool ata_journal_clear")]
        self.assertIn("ata_journal.header_lba", writer)
        self.assertIn("ata_journal.mirror_lba", writer)

    def test_recovery_runs_before_mutable_fat_metadata_is_consumed(self):
        source = (ROOT / "fs/fat32/fat32.c").read_text(encoding="utf-8")
        self.assertLess(source.index("ata_journal_attach("),
                        source.index("candidate_boot.fs_info"))

    def test_ahci_uses_the_same_journal_and_recovery_transport(self):
        source = (ROOT / "drivers/block/ata.c").read_text(encoding="utf-8")
        self.assertIn("ahci_write_sector_recovery", source)
        self.assertIn("ata_journal_recover_resource", source)
        partition_write = source[source.index("bool ata_write_sector("):
                                 source.index("bool ata_write_sectors(")]
        self.assertIn("ata_write_sector_journaled(parent->base", partition_write)
        self.assertIn("ata_write_sector_journaled(base, lba", partition_write)

    def test_power_loss_recovery_is_a_ci_gate(self):
        runner = (ROOT / "scripts/test_journal_recovery.py").read_text(encoding="utf-8")
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        workflow = (ROOT / ".github/workflows/build.yml").read_text(encoding="utf-8")
        self.assertIn('"--persistent"', runner)
        self.assertIn('"--expect-reist-probe"', runner)
        self.assertIn('"--expect-storage-self-test"', runner)
        self.assertIn("if not restored or state != 0", runner)
        self.assertIn("targets = [DATA_PARTITION_START + 6", runner)
        self.assertIn("mirror_lba = DATA_PARTITION_START + 31", runner)
        self.assertIn("headers_match", runner)
        self.assertIn("test-smoke-journal-recovery:", makefile)
        self.assertIn("make test-smoke-journal-recovery", workflow)


if __name__ == "__main__":
    unittest.main()
