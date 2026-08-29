import binascii
import io
import pathlib
import struct
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from scripts.create_native_boot_image import write_fat32_volume


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

    def test_kernel_orders_batched_undo_active_target_then_clean(self):
        source = (ROOT / "drivers/block/ata_journal.c").read_text(
            encoding="utf-8")
        start = source.index("bool ata_undo_journal_write_sector")
        end = source.index("bool ata_undo_journal_attach", start)
        body = source[start:end]
        self.assertIn("read_sector(journal, base, lba, journal->undo_data[slot]",
                      body)
        self.assertIn("memcpy(journal->pending_data[slot]", body)
        self.assertNotIn("write_active(journal", body)

        commit = source[source.index("static bool transaction_end"):
                        source.index("bool ata_undo_journal_transaction_end")]
        ordered = [
            "journal->transport->write_sectors_deferred(",
            "flush_deferred(journal)",
            "write_active(journal, deferred)",
            "flush_deferred(journal)",
            "commit_targets_deferred(journal)",
            "clear_journal(journal, deferred)",
            "flush_deferred(journal)",
        ]
        position = -1
        for token in ordered:
            position = commit.index(token, position + 1)

    def test_deferred_transport_is_fail_closed_and_pio_flushes_are_coalesced(self):
        core = (ROOT / "drivers/block/ata_journal.c").read_text(
            encoding="utf-8")
        ready = core[core.index("static bool deferred_transport_ready"):
                     core.index("static bool read_sector")]
        self.assertIn("journal->transport->write_deferred != NULL", ready)
        self.assertIn("journal->transport->flush != NULL", ready)
        self.assertIn("journal->transport->commit_begin != NULL", ready)
        self.assertIn("journal->transport->commit_write_deferred != NULL",
                      ready)
        self.assertIn("journal->transport->commit_end != NULL", ready)

        ata = (ROOT / "drivers/block/ata.c").read_text(encoding="utf-8")
        self.assertIn("ata_journal_write_deferred_transport", ata)
        self.assertIn("ahci_write_sectors_deferred", ata)
        self.assertIn("ata_write_sector_impl(base, lba, (void *)buffer, "
                      "is_master, false)", ata)
        transport = ata[ata.index("static const ata_journal_transport_t"):
                        ata.index("static void ata_journal_ensure_initialized")]
        self.assertIn(".write_deferred = ata_journal_core_write_deferred",
                      transport)
        self.assertIn(
            ".write_sectors_deferred = "
            "ata_journal_core_write_sectors_deferred",
            transport,
        )
        self.assertIn(".flush = ata_journal_core_flush", transport)
        self.assertIn(".commit_begin = ata_journal_core_commit_begin",
                      transport)
        self.assertIn(
            ".commit_write_deferred = ata_journal_core_commit_write_deferred",
            transport)
        self.assertIn(
            ".commit_write_sectors_deferred =",
            transport)
        self.assertIn(".commit_end = ata_journal_core_commit_end", transport)
        batch = ata[ata.index("static bool ata_journal_core_commit_begin"):
                    ata.index("static bool ata_journal_core_commit_write(")]
        self.assertIn("storage_write_begin", batch)
        self.assertIn("ata_journal_flush_transport", batch)
        self.assertIn("storage_write_end(durable)", batch)

        targets = core[core.index("static bool commit_targets_deferred"):
                       core.index("static bool transaction_end")]
        self.assertIn("commit_write_sectors_deferred", targets)
        self.assertIn("target_lba + 1U", targets)
        self.assertIn("journal->pending_data[index]", targets)

    def test_recovery_validates_crc_before_restoring_and_clearing(self):
        source = (ROOT / "drivers/block/ata_journal.c").read_text(
            encoding="utf-8")
        start = source.index("bool ata_undo_journal_attach")
        end = source.index("bool ata_undo_journal_is_attached", start)
        body = source[start:end]
        self.assertIn("primary.magic == ATA_JOURNAL_MAGIC", body)
        self.assertIn("mirror.magic == ATA_JOURNAL_MAGIC", body)
        self.assertIn("record_valid(&record)", body)
        self.assertIn("record.entries[index].data_crc32", body)
        self.assertIn("data_lba + index", body)
        self.assertIn("write_sector(journal, base, target, data", body)
        ata = (ROOT / "drivers/block/ata.c").read_text(encoding="utf-8")
        self.assertIn("if (!result) ata_fence_writes();", ata)

    def test_vfs_transaction_spans_multiple_unique_sector_updates(self):
        ata = (ROOT / "drivers/block/ata_journal.c").read_text(
            encoding="utf-8")
        header = (ROOT / "drivers/block/ata_journal.h").read_text(
            encoding="utf-8")
        fs = (ROOT / "kernel/init/filesystem_safety.c").read_text(encoding="utf-8")
        self.assertIn("#define ATA_JOURNAL_MAX_ENTRIES 20U", header)
        self.assertIn("journal->entries[i].target_lba == lba", ata)
        self.assertIn("journal->entry_count >= ATA_JOURNAL_MAX_ENTRIES", ata)
        self.assertIn("ata_journal_transaction_begin()", fs)
        self.assertIn("ata_journal_transaction_end(commit)", fs)

    def test_redundant_headers_select_conservatively_and_self_repair(self):
        source = (ROOT / "drivers/block/ata_journal.c").read_text(
            encoding="utf-8")
        header = (ROOT / "drivers/block/ata_journal.h").read_text(
            encoding="utf-8")
        self.assertIn("#define ATA_JOURNAL_MIRROR_OFFSET 31U", header)
        self.assertIn("primary.state != mirror.state", source)
        self.assertIn("primary.state == ATA_JOURNAL_ACTIVE ? primary : mirror", source)
        self.assertIn("record.state == ATA_JOURNAL_ACTIVE || repair_headers",
                      source)
        writer = source[source.index("static bool write_record"):
                        source.index("void ata_undo_journal_make_clean")]
        self.assertIn("journal->header_lba", writer)
        self.assertIn("journal->mirror_lba", writer)

    def test_recovery_runs_before_mutable_fat_metadata_is_consumed(self):
        source = (ROOT / "fs/fat32/fat32.c").read_text(encoding="utf-8")
        self.assertLess(source.index("ata_journal_attach("),
                        source.index("candidate_boot.fs_info"))

    def test_fat32_writes_require_the_exact_current_journal_binding(self):
        ata = (ROOT / "drivers/block/ata.c").read_text(encoding="utf-8")
        core = (ROOT / "drivers/block/ata_journal.c").read_text(
            encoding="utf-8")
        query = core[core.index("bool ata_undo_journal_is_attached"):]
        for token in (
            "journal->enabled",
            "journal->base == base",
            "journal->is_master == is_master",
            "journal->volume_start_lba == partition_lba",
            "journal->volume_end_lba == partition_lba + volume_sectors",
        ):
            self.assertIn(token, query)
        self.assertIn("ata_partition_translate(partition, partition_lba", ata)

        fat32 = (ROOT / "fs/fat32/fat32.c").read_text(encoding="utf-8")
        prepare = fat32[fat32.index("bool fat32_prepare_write"):
                        fat32.index("// Forward declarations")]
        self.assertLess(prepare.index("ata_journal_is_attached("),
                        prepare.index("ata_journal_attach("))
        self.assertIn("fat32_write_supported = false", prepare)

        transaction = core[
            core.index("bool ata_undo_journal_transaction_begin"):
            core.index("bool ata_undo_journal_write_sector")]
        self.assertNotIn("if (!journal->enabled) return true", transaction)
        self.assertIn("journal->transaction_depth++", transaction)
        self.assertIn("if (!journal->enabled)", transaction)
        attach = core[core.index("bool ata_undo_journal_attach"):
                      core.index("bool ata_undo_journal_is_attached")]
        self.assertIn("inherited_depth", attach)
        self.assertIn("journal->entry_count != 0U", attach)
        self.assertIn("journal->transaction_depth = inherited_depth", attach)

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
