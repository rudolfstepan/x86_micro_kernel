import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class Fat12ToolContracts(unittest.TestCase):
    def read(self, path):
        return (ROOT / path).read_text(encoding="utf-8")

    def test_tools_are_registered_and_fdisk_requires_explicit_confirmation(self):
        programs = self.read("scripts/build_system_programs.py")
        fdisk = self.read("userspace/programs/fdisk.c")
        self.assertIn('"FDISK.PRG"', programs)
        self.assertIn("x86os_drive_info", fdisk)
        self.assertIn('"--confirm"', fdisk)
        self.assertIn("x86os_partition_create", fdisk)

    def test_format_is_confirmed_bounded_and_service_mediated(self):
        source = self.read("userspace/programs/format.c")
        service = self.read("userspace/programs/storage_service.c")
        programs = self.read("scripts/build_system_programs.py")
        self.assertIn('"FORMAT.PRG"', programs)
        self.assertIn('"--reist-fat12"', source)
        self.assertIn('"--confirm"', source)
        self.assertIn("X86OS_STORAGE_FORMAT_FAT12", source)
        self.assertIn("FORMAT_TIMEOUT_MS", source)
        self.assertIn("#define FORMAT_TIMEOUT_MS 60000U", source)
        self.assertIn("x86os_storage_submit", source)
        self.assertIn("x86os_storage_collect", source)
        self.assertNotIn("x86os_storage_block_write", source)
        self.assertIn("FORMAT_FAT12_RESERVED (1U + FORMAT_FAT12_SAFETY_SECTORS)", service)
        self.assertIn("FORMAT_FAT12_JOURNAL_ENTRIES 64U", service)
        self.assertIn("FORMAT_FAT12_REMAP_SPARES 8U", service)
        self.assertIn("FORMAT_FAT12_REPLICA_SECTORS 54U", service)
        self.assertIn(".magic = 0x524A3132U, .version = 2U", service)
        self.assertIn("format_fat12(request.resource)", service)
        self.assertIn("format_equal(sector, expected", service)
        self.assertIn("x86os_storage_block_read(resource, sector, verify)", service)

    def test_chkdsk_delegates_repair_without_raw_media_access(self):
        chkdsk = self.read("userspace/programs/chkdsk.c")
        self.assertIn("X86OS_STORAGE_CHECK_FAT12", chkdsk)
        self.assertIn("X86OS_STORAGE_REPAIR_FAT12_MIRROR", chkdsk)
        self.assertIn("X86OS_STORAGE_REPAIR_FAT12_CHAINS", chkdsk)
        self.assertIn("X86OS_STORAGE_REPAIR_FAT12_SHORT_FILES", chkdsk)
        self.assertIn("X86OS_STORAGE_RECLAIM_FAT12_ORPHANS", chkdsk)
        self.assertIn("X86OS_STORAGE_REPAIR_FAT12_LOOPS", chkdsk)
        self.assertIn("X86OS_STORAGE_REPAIR_FAT12_DIRECTORY_LOOPS", chkdsk)
        self.assertIn("X86OS_STORAGE_REPAIR_FAT12_SHORT_LOOPS", chkdsk)
        self.assertIn("X86OS_STORAGE_REPAIR_FAT12_CROSSLINKS", chkdsk)
        self.assertIn("X86OS_STORAGE_REPAIR_FAT12_DIRECTORY_SIZE", chkdsk)
        self.assertIn("X86OS_STORAGE_REPAIR_FAT12_VOLUME_LABEL", chkdsk)
        self.assertIn("X86OS_STORAGE_REPAIR_FAT12_ZERO_FILES", chkdsk)
        self.assertIn('"--repair"', chkdsk)
        self.assertIn('"--repair-chains"', chkdsk)
        self.assertIn('"--repair-short"', chkdsk)
        self.assertIn('"--reclaim-orphans"', chkdsk)
        self.assertIn('"--repair-loops"', chkdsk)
        self.assertIn('"--repair-dir-loops"', chkdsk)
        self.assertIn('"--repair-short-loops"', chkdsk)
        self.assertIn('"--repair-crosslinks"', chkdsk)
        self.assertIn('"--repair-dir-size"', chkdsk)
        self.assertIn('"--repair-volume-label"', chkdsk)
        self.assertIn('"--repair-zero-files"', chkdsk)
        self.assertIn('"--confirm"', chkdsk)
        self.assertNotIn("x86os_storage_block_read", chkdsk)
        self.assertNotIn("x86os_storage_block_write", chkdsk)
        self.assertNotIn("x86os_write(", chkdsk)
        self.assertNotIn("x86os_unlink(", chkdsk)


if __name__ == "__main__":
    unittest.main()
