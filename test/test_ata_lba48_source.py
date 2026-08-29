import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class AtaLba48ContractTests(unittest.TestCase):
    def test_identify_and_ext_commands_are_bounded(self):
        header = (ROOT / "drivers/block/ata.h").read_text(encoding="utf-8")
        source = (ROOT / "drivers/block/ata.c").read_text(encoding="utf-8")
        drives = (ROOT / "drivers/bus/drives.h").read_text(encoding="utf-8")
        self.assertIn("ATA_READ_SECTORS_EXT 0x24", header)
        self.assertIn("ATA_WRITE_SECTORS_EXT 0x34", header)
        self.assertIn("ATA_FLUSH_CACHE      0xE7", header)
        self.assertIn("ATA_FLUSH_CACHE_EXT", header)
        self.assertIn("ATA_IDENTIFY_COMMAND_SET_VALID_MASK", header)
        self.assertIn("ATA_IDENTIFY_FLUSH_CACHE", header)
        self.assertIn("ATA_IDENTIFY_FLUSH_CACHE_EXT", header)
        self.assertIn("command_set2 & ATA_IDENTIFY_LBA48", source)
        self.assertIn("identify_data[100]", source)
        self.assertIn("identify_data[103]", source)
        self.assertIn("sectors > UINT32_MAX ? UINT32_MAX", source)
        self.assertIn("lba48_supported", drives)
        self.assertIn("flush_cache_supported", drives)
        self.assertIn("flush_cache_ext_supported", drives)
        self.assertIn("drive == NULL || !drive->lba48_supported", source)
        self.assertIn("use_lba48 ? ATA_READ_SECTORS_EXT", source)
        self.assertIn("use_lba48 ? ATA_WRITE_SECTORS_EXT", source)

    def test_flush_selection_is_independent_of_lba48(self):
        source = (ROOT / "drivers/block/ata.c").read_text(encoding="utf-8")
        start = source.index("static uint8_t ata_flush_command_for_drive(")
        end = source.index("static bool ata_flush_cache_impl(", start)
        select = source[start:end]
        self.assertIn("drive->flush_cache_ext_supported", select)
        self.assertIn("ATA_FLUSH_CACHE_EXT", select)
        self.assertIn("drive->flush_cache_supported", select)
        self.assertIn("ATA_FLUSH_CACHE", select)
        self.assertNotIn("lba48_supported", select)
        self.assertLess(select.index("flush_cache_ext_supported"),
                        select.index("flush_cache_supported"))

        identify = source[source.index("uint16_t command_set2 ="):
                          source.index("drive_info->sectors =", start)]
        self.assertIn("ATA_IDENTIFY_COMMAND_SET_VALID_MASK", identify)
        self.assertIn("ATA_IDENTIFY_COMMAND_SET_VALID", identify)
        self.assertIn("ATA_IDENTIFY_LBA48", identify)
        self.assertIn("ATA_IDENTIFY_FLUSH_CACHE", identify)
        self.assertIn("ATA_IDENTIFY_FLUSH_CACHE_EXT", identify)

    def test_pio_batches_are_bounded_and_verified(self):
        header = (ROOT / "drivers/block/ata.h").read_text(encoding="utf-8")
        source = (ROOT / "drivers/block/ata.c").read_text(encoding="utf-8")
        block = (ROOT / "drivers/block/block_device.c").read_text(
            encoding="utf-8"
        )
        fat32 = (ROOT / "fs/fat32/fat32_cluster.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("ATA_PIO_MAX_SECTORS 20U", header)
        self.assertIn("count <= ATA_PIO_MAX_SECTORS", source)
        self.assertIn("count <= drive->sectors - lba", source)
        self.assertIn("ata_program_pio_batch", source)
        self.assertIn("outb(ATA_SECTOR_CNT(base), (uint8_t)count)", source)
        self.assertIn("ata_batch_verify", source)
        self.assertIn("memcmp(ata_batch_verify", source)
        self.assertIn("block_device_read_sectors", block)
        self.assertIn("block_device_write_sectors", block)
        self.assertIn("ata_read_sectors(ata_base_address", fat32)
        self.assertIn("ata_write_sectors(ata_base_address", fat32)

    def test_ahci_batches_are_bounded_and_report_exact_dma_completion(self):
        header = (ROOT / "drivers/block/ahci.h").read_text(encoding="utf-8")
        source = (ROOT / "drivers/block/ahci.c").read_text(encoding="utf-8")
        files = (ROOT / "fs/fat32/fat32_files.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("AHCI_DMA_MAX_SECTORS 20U", header)
        self.assertIn("count <= AHCI_DMA_MAX_SECTORS", source)
        self.assertIn("count <= drive->sectors - sector", source)
        self.assertIn("fis->count_low = (uint8_t)count", source)
        self.assertIn("fis->count_high = (uint8_t)(count >> 8U)", source)
        self.assertIn("header->bytes_transferred == expected", source)
        self.assertIn("ata_read_sectors(ata_base_address, sector, run",
                      files)

    def test_detection_zero_initializes_extended_drive_metadata(self):
        source = (ROOT / "drivers/block/ata.c").read_text(encoding="utf-8")
        self.assertIn("memset(&temp_drive, 0, sizeof(temp_drive))", source)


if __name__ == "__main__":
    unittest.main()
