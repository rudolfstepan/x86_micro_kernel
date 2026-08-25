"""Source contract for the bounded full-image FAT32/ATA write-cut campaign."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class Fat32ImageFaultCampaignTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.host = (ROOT / "test/test_fat32_host.c").read_text(
            encoding="utf-8")
        cls.ata = (ROOT / "drivers/block/ata.c").read_text(encoding="utf-8")
        cls.core = (ROOT / "drivers/block/ata_journal.c").read_text(
            encoding="utf-8")

    def test_campaign_has_a_fixed_write_ceiling(self) -> None:
        self.assertIn("#define FAT32_FAULT_CAMPAIGN_MAX_WRITES 384u",
                      self.host)
        self.assertIn("measured_writes <= FAT32_FAULT_CAMPAIGN_MAX_WRITES",
                      self.host)

    def test_production_driver_and_host_share_the_same_core(self) -> None:
        self.assertIn('#include "ata_journal.h"', self.ata)
        self.assertIn("ata_undo_journal_write_sector(&ata_journal",
                      self.ata)
        self.assertIn("ata_undo_journal_write_sector(&host_journal",
                      self.host)

    def test_power_cut_completes_selected_write_then_blocks_transport(self) -> None:
        write = self.host[self.host.index("static bool host_raw_write"):
                          self.host.index("static bool host_raw_read")]
        self.assertLess(write.index("memcpy(test_disk[lba]"),
                        write.index("campaign_power_cut = true"))
        read = self.host[self.host.index("static bool host_disk_read"):
                         self.host.index("static bool host_raw_write")]
        self.assertIn("campaign_power_cut", read)

    def test_every_measured_write_index_is_exercised(self) -> None:
        campaign = self.host[self.host.index(
            "run_fat32_image_fault_campaign"):]
        self.assertIn(
            "for (unsigned int cut = 1U; cut <= measured_writes; ++cut)",
            campaign)
        self.assertIn("campaign_write_count == cut", campaign)

    def test_host_verifier_rejects_unknown_whole_sector_bytes(self) -> None:
        verifier = self.host[
            self.host.index("campaign_image_uses_known_sectors"):
            self.host.index("campaign_image_matches")]
        self.assertIn("campaign_baseline[sector]", verifier)
        self.assertIn("campaign_committed[sector]", verifier)
        self.assertIn("SECTOR_SIZE", verifier)

    def test_successful_recovery_requires_whole_image_and_semantics(self) -> None:
        verify = self.host[
            self.host.index("campaign_verify_mounted_state"):
            self.host.index("run_fat32_image_fault_campaign")]
        self.assertIn("CAMPAIGN_FAT1_LBA", verify)
        self.assertIn("CAMPAIGN_FAT2_LBA", verify)
        self.assertIn("fat[next]", verify)
        self.assertIn('vfs_open("/KEEP.BIN"', verify)
        campaign = self.host[self.host.index(
            "run_fat32_image_fault_campaign"):]
        self.assertIn("campaign_image_matches(campaign_baseline)", campaign)
        self.assertIn("campaign_image_matches(campaign_committed)", campaign)

    def test_mount_rejection_is_separate_from_recovery(self) -> None:
        campaign = self.host[self.host.index(
            "run_fat32_image_fault_campaign"):]
        self.assertIn("++recovered", campaign)
        self.assertIn("++rejected", campaign)
        self.assertIn("recovered + rejected == measured_writes", campaign)

    def test_lfn_replace_has_its_own_complete_write_cut_campaign(self) -> None:
        campaign = self.host[self.host.index(
            "run_fat32_lfn_replace_fault_campaign"):]
        self.assertIn("vfs_rename(source, target)", campaign)
        self.assertIn("campaign_verify_lfn_replace(false)", campaign)
        self.assertIn("campaign_verify_lfn_replace(true)", campaign)
        self.assertIn(
            "for (unsigned int cut = 1U; cut <= measured_writes; ++cut)",
            campaign,
        )
        self.assertIn("old_image != new_image", campaign)
        self.assertIn("campaign_verify_lfn_replace(new_image)", campaign)


if __name__ == "__main__":
    unittest.main()
