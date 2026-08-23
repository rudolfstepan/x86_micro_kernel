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

    def test_fat32_writes_require_the_exact_current_journal_binding(self):
        ata = (ROOT / "drivers/block/ata.c").read_text(encoding="utf-8")
        query = ata[ata.index("static bool ata_journal_is_attached_impl"):
                    ata.index("bool ata_journal_recover_resource")]
        for token in (
            "ata_journal.enabled",
            "ata_journal.base == base",
            "ata_journal.is_master == is_master",
            "ata_journal.volume_start_lba == partition_lba",
            "ata_journal.volume_end_lba == partition_lba + volume_sectors",
            "ata_partition_translate(partition, partition_lba",
        ):
            self.assertIn(token, query)

        fat32 = (ROOT / "fs/fat32/fat32.c").read_text(encoding="utf-8")
        prepare = fat32[fat32.index("bool fat32_prepare_write"):
                        fat32.index("// Forward declarations")]
        self.assertLess(prepare.index("ata_journal_is_attached("),
                        prepare.index("ata_journal_attach("))
        self.assertIn("fat32_write_supported = false", prepare)

        transaction = ata[ata.index("bool ata_journal_transaction_begin"):
                          ata.index("static bool ata_write_sector_journaled")]
        self.assertNotIn("if (!ata_journal.enabled) return true", transaction)
        self.assertIn("ata_journal.transaction_depth++", transaction)
        self.assertIn("if (!ata_journal.enabled)", transaction)
        attach = ata[ata.index("static bool ata_journal_attach_impl"):
                     ata.index("bool ata_journal_attach(")]
        self.assertIn("inherited_transaction_depth", attach)
        self.assertIn("ata_journal.entry_count != 0U", attach)
        self.assertIn("ata_journal.transaction_depth = inherited_transaction_depth",
                      attach)

        direct_writes = []
        for path in (ROOT / "fs/fat32").glob("*.c"):
            for number, line in enumerate(
                    path.read_text(encoding="utf-8").splitlines(), 1):
                if "ata_write_sector(" in line:
                    direct_writes.append((path.name, number, line.strip()))
        self.assertEqual(len(direct_writes), 1)
        self.assertEqual(direct_writes[0][0], "fat32.c")
        self.assertEqual(
            direct_writes[0][2],
            "ata_write_sector(ata_base_address, lba, buffer, ata_is_master);",
        )

        adapter = (ROOT / "fs/fat32/fat32_vfs_adapter.c").read_text(
            encoding="utf-8")
        self.assertIn("static int fat32_require_write", adapter)
        for operation in ("write", "mkdir", "rmdir", "create", "delete",
                          "rename"):
            start = adapter.index(f"static int fat32_vfs_{operation}_unlocked")
            body = adapter[start:adapter.index("\n}\n", start)]
            self.assertIn("fat32_require_write", body, operation)
        touch = adapter[adapter.index("static int fat32_vfs_touch("):
                        adapter.index("static int fat32_vfs_stat(")]
        self.assertIn("fat32_require_write", touch)

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
