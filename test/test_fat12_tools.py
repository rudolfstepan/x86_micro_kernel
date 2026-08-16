import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class Fat12ToolContracts(unittest.TestCase):
    def read(self, path):
        return (ROOT / path).read_text(encoding="utf-8")

    def test_tools_are_registered_and_fdisk_is_read_only(self):
        programs = self.read("scripts/build_system_programs.py")
        fdisk = self.read("examples/userspace/fdisk.c")
        self.assertIn('"FDISK.PRG"', programs)
        self.assertIn("x86os_drive_info", fdisk)
        self.assertNotIn("x86os_storage_block_write", fdisk)

    def test_format_is_confirmed_bounded_and_service_mediated(self):
        source = self.read("examples/userspace/format.c")
        service = self.read("examples/userspace/storage_service.c")
        programs = self.read("scripts/build_system_programs.py")
        self.assertIn('"FORMAT.PRG"', programs)
        self.assertIn('"--reist-fat12"', source)
        self.assertIn('"--confirm"', source)
        self.assertIn("X86OS_STORAGE_FORMAT_FAT12", source)
        self.assertIn("FORMAT_TIMEOUT_MS", source)
        self.assertIn("#define FORMAT_TIMEOUT_MS 30000U", source)
        self.assertIn("x86os_storage_submit", source)
        self.assertIn("x86os_storage_collect", source)
        self.assertNotIn("x86os_storage_block_write", source)
        self.assertIn("FORMAT_FAT12_RESERVED 23U", service)
        self.assertIn("format_fat12(request.resource)", service)
        self.assertIn("format_equal(sector, expected", service)

    def test_chkdsk_remains_read_only(self):
        chkdsk = self.read("examples/userspace/chkdsk.c")
        self.assertNotIn("x86os_write(", chkdsk)
        self.assertNotIn("x86os_unlink(", chkdsk)


if __name__ == "__main__":
    unittest.main()
