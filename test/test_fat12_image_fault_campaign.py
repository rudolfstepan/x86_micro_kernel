"""Source contract for the bounded full-image FAT12 write-cut campaign."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class Fat12ImageFaultCampaignTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = (ROOT / "test/test_fat12_host.c").read_text(
            encoding="utf-8")

    def test_campaign_has_a_fixed_write_ceiling(self) -> None:
        self.assertIn("#define FAT12_FAULT_CAMPAIGN_MAX_WRITES 384u",
                      self.source)
        self.assertIn("write_count > FAT12_FAULT_CAMPAIGN_MAX_WRITES",
                      self.source)

    def test_power_cut_completes_selected_write_then_blocks_transport(self) -> None:
        write = self.source[self.source.index("bool fdd_write_sector"):
                            self.source.index("bool fdc_write_sectors")]
        self.assertLess(write.index("memcpy(floppy[logical]"),
                        write.index("campaign_power_cut = true"))
        read = self.source[self.source.index("bool fdc_read_sector"):
                           self.source.index("bool fdc_read_sectors")]
        self.assertIn("campaign_power_cut", read)

    def test_every_measured_write_index_is_exercised(self) -> None:
        campaign = self.source[
            self.source.index("run_fat12_image_fault_campaign"):]
        self.assertIn("for (uint32_t cut = 1U; cut <= write_count; ++cut)",
                      campaign)
        self.assertIn("campaign_write_count != cut", campaign)

    def test_host_verifier_rejects_unknown_whole_sector_bytes(self) -> None:
        verifier = self.source[
            self.source.index("campaign_image_uses_known_sectors"):
            self.source.index("campaign_image_matches")]
        self.assertIn("campaign_baseline[sector]", verifier)
        self.assertIn("campaign_committed[sector]", verifier)
        self.assertIn("FAT12_SECTOR_SIZE", verifier)

    def test_successful_recovery_requires_whole_image_and_semantics(self) -> None:
        verify = self.source[
            self.source.index("campaign_verify_mounted_state"):
            self.source.index("run_fat12_image_fault_campaign")]
        self.assertIn("campaign_image_matches(campaign_baseline)", verify)
        self.assertIn("campaign_image_matches(campaign_committed)", verify)
        self.assertIn("fat12_get_fat_entry(second) >= FAT12_EOC_MIN", verify)
        self.assertIn("memcmp(floppy[TEST_FAT1_SECTOR]", verify)
        self.assertIn('fs->ops->open(fs, "/FILE.BIN"', verify)

    def test_mount_rejection_is_separate_from_recovery(self) -> None:
        campaign = self.source[
            self.source.index("run_fat12_image_fault_campaign"):]
        self.assertIn("++recovered", campaign)
        self.assertIn("++rejected", campaign)
        self.assertIn("recovered + rejected != write_count", campaign)


if __name__ == "__main__":
    unittest.main()
