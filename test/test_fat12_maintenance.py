import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class Fat12MaintenanceContracts(unittest.TestCase):
    def read(self, path):
        return (ROOT / path).read_text(encoding="utf-8")

    def test_block_write_is_append_only_and_storage_authorized(self):
        sdk = self.read("userspace/sdk/include/x86os.h")
        syscall = self.read("kernel/syscall/syscall_table.c")
        process = self.read("kernel/proc/process.c")
        self.assertIn("X86OS_SYS_STORAGE_BLOCK_WRITE = 85", sdk)
        body = syscall[syscall.index("static int syscall_storage_block_write"):
                       syscall.index("static int syscall_storage_complete")]
        self.assertIn("storage_service_authorized", body)
        self.assertIn("DRIVE_TYPE_FDD", body)
        self.assertIn("storage_service_resource_read_only", body)
        self.assertIn("fdc_write_sectors", body)
        self.assertIn("memcmp(data, verify", body)
        self.assertIn("storage_service_report_media_failure(resource, true)", body)
        self.assertIn("SYS_STORAGE_BLOCK_WRITE", process)

    def test_storage_service_mediates_fdd_writes(self):
        service = self.read("userspace/programs/storage_service.c")
        self.assertIn("X86OS_STORAGE_BLOCK_WRITE", service)
        self.assertIn("x86os_storage_block_write", service)

    def test_chkdsk_is_read_only_and_bounded(self):
        source = self.read("userspace/programs/chkdsk.c")
        self.assertIn("MAX_NODES", source)
        self.assertNotIn("x86os_write(", source)
        self.assertNotIn("x86os_unlink(", source)


if __name__ == "__main__":
    unittest.main()
