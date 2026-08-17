import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class PartitionProvisioningContractTests(unittest.TestCase):
    def test_partition_driver_exposes_verified_empty_disk_provisioning(self):
        header = (ROOT / "drivers/block/partition.h").read_text(encoding="utf-8")
        source = (ROOT / "drivers/block/partition.c").read_text(encoding="utf-8")
        self.assertIn("partition_provision_mbr", header)
        self.assertIn("DRIVE_TYPE_ATA", source)
        self.assertIn("DRIVE_TYPE_AHCI", source)
        self.assertIn("PARTITION_ALIGN_LBA", source)
        self.assertIn("block_device_flush(drive)", source)
        self.assertIn("memcmp(mbr, verify, sizeof(mbr))", source)

    def test_provisioning_rejects_existing_entries_and_overflow(self):
        source = (ROOT / "drivers/block/partition.c").read_text(encoding="utf-8")
        self.assertIn("return -16", source)
        self.assertIn("(uint64_t)first_lba + sectors > drive->sectors", source)
        self.assertIn("sectors % PARTITION_ALIGN_LBA", source)

    def test_fat32_formatter_provisions_the_reist_journal(self):
        source = (ROOT / "userspace/programs/storage_service.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("FORMAT32_JOURNAL_HEADER", source)
        self.assertIn("FORMAT32_JOURNAL_MIRROR", source)
        self.assertIn("0x4A545352U", source)
        self.assertIn("format32_crc32(sector)", source)
        self.assertNotIn("journal_data <= FORMAT32_JOURNAL_HEADER + 20U", source)

    def test_fat32_quick_and_full_modes_are_bounded_and_fail_closed(self):
        command = (ROOT / "userspace/programs/format.c").read_text(
            encoding="utf-8"
        )
        service = (ROOT / "userspace/programs/storage_service.c").read_text(
            encoding="utf-8"
        )
        syscall = (ROOT / "kernel/syscall/syscall_table.c").read_text(
            encoding="utf-8"
        )
        self.assertIn('equal(argv[2], "--quick")', command)
        self.assertIn('equal(argv[2], "--full")', command)
        self.assertIn("FORMAT_FULL_SECTOR_BUDGET_MS", command)
        self.assertIn("X86OS_STORAGE_FORMAT_FAT32_PREPARE", command)
        self.assertIn("X86OS_STORAGE_FORMAT_FAT32_SCAN", command)
        self.assertIn("FORMAT32_FAT_CHUNK_SECTORS 256U", service)
        self.assertIn("FORMAT32_SCAN_CHUNK_CLUSTERS 256U", service)
        self.assertIn("FORMAT32_BAD_CLUSTER 0x0FFFFFF7U", service)
        self.assertIn("format32_mark_bad", service)
        self.assertIn("for (uint32_t attempt = 0U; attempt < 3U; ++attempt)",
                      syscall)
        self.assertIn("storage_service_report_media_failure(resource, true)",
                      syscall)
        self.assertIn("storage_service_accept_partition_layout", syscall)
        self.assertIn("storage_service_requalify_media(drive->parent_resource",
                      (ROOT / "kernel/init/storage_service.c").read_text(
                          encoding="utf-8"))

    def test_fat32_prepare_invalidates_boot_before_clearing_both_fats(self):
        source = (ROOT / "userspace/programs/storage_service.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("cursor == 0U && format32_write(resource, 0U, zero)",
                      source)
        self.assertIn("FORMAT32_RESERVED + fat_sectors + index", source)
        self.assertIn("x86os_storage_block_flush(resource)", source)

    def test_format_operations_require_the_exact_admin_program_domain(self):
        process = (ROOT / "kernel/proc/process.c").read_text(encoding="utf-8")
        syscall = (ROOT / "kernel/syscall/syscall_table.c").read_text(
            encoding="utf-8"
        )
        self.assertIn('strcmp(resolved, "/sbin/format.prg") == 0', process)
        self.assertIn("SYS_STORAGE_SUBMIT, SYS_STORAGE_COLLECT", process)
        self.assertIn("request.operation >= STORAGE_REQUEST_FORMAT_FAT12",
                      syscall)
        self.assertIn("process->domain_profile.kind != PROCESS_DOMAIN_ADMIN",
                      syscall)

    def test_fdisk_uses_confirmed_kernel_partition_request(self):
        source = (ROOT / "userspace/programs/fdisk.c").read_text(encoding="utf-8")
        self.assertIn('"--create"', source)
        self.assertIn('"--confirm"', source)
        self.assertIn("x86os_partition_create", source)
        self.assertIn("X86OS_PARTITION_REQUEST_VERSION", source)
        self.assertIn("X86OS_DRIVE_PARTITION) x86os_puts(\"PART\")", source)


if __name__ == "__main__":
    unittest.main()
