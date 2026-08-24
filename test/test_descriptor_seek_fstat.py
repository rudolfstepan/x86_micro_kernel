"""Contract tests for append-only descriptor seek and node-based fstat."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


class DescriptorSeekFstatTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.abi = read("include/reist/abi/syscall.h")
        cls.sdk_h = read("userspace/sdk/include/x86os.h")
        cls.sdk_c = read("userspace/sdk/x86os.c")
        cls.process_h = read("kernel/proc/process.h")
        cls.process_c = read("kernel/proc/process.c")
        cls.syscall = read("kernel/syscall/syscall_table.c")
        cls.vfs_h = read("fs/vfs/vfs.h")
        cls.vfs_c = read("fs/vfs/vfs.c")
        cls.fat12 = read("fs/fat12/fat12_vfs_adapter.c")
        cls.fat32 = read("fs/fat32/fat32_vfs_adapter.c")
        cls.ext2 = read("fs/ext2/ext2_vfs_adapter.c")
        cls.guest = read("userspace/programs/guest_test.c")

    def test_syscalls_are_append_only_and_sdk_is_posix_shaped(self) -> None:
        self.assertIn("X(LSEEK, LSEEK, 121U)", self.abi)
        self.assertIn("X(FSTAT, FSTAT, 122U)", self.abi)
        self.assertIn("REIST_ESPIPE = 29", self.abi)
        self.assertIn("X86OS_SEEK_SET 0U", self.sdk_h)
        self.assertIn("X86OS_SEEK_CUR 1U", self.sdk_h)
        self.assertIn("X86OS_SEEK_END 2U", self.sdk_h)
        self.assertIn("int32_t x86os_lseek(", self.sdk_h)
        self.assertIn("int x86os_fstat(", self.sdk_h)
        self.assertIn("X86OS_SYS_LSEEK", self.sdk_c)
        self.assertIn("X86OS_SYS_FSTAT", self.sdk_c)

    def test_seek_validates_before_publishing_offset(self) -> None:
        start = self.process_c.index("int process_file_seek(")
        end = self.process_c.index("int process_file_fstat(", start)
        block = self.process_c[start:end]
        self.assertIn("int64_t candidate", block)
        self.assertIn("REIST_EOVERFLOW", block)
        self.assertIn("REIST_ESPIPE", block)
        self.assertIn("file->offset = (uint32_t)candidate;", block)
        self.assertLess(block.index("candidate > INT32_MAX"),
                        block.index("file->offset = (uint32_t)candidate;"))

    def test_seek_end_and_fstat_share_node_metadata(self) -> None:
        self.assertIn("int vfs_fstat(vfs_node_t* node,",
                      self.vfs_h)
        self.assertIn("vfs_fstat(file->node, &entry)", self.process_c)
        self.assertNotIn("vfs_stat(", self.process_c[
            self.process_c.index("int process_file_seek("):
            self.process_c.index("int process_file_close(")])

    def test_adapters_revalidate_the_open_identity(self) -> None:
        self.assertIn("fat12_vfs_fstat", self.fat12)
        self.assertIn("handle->directory_sector", self.fat12[
            self.fat12.index("fat12_vfs_fstat"):])
        self.assertIn("fat32_vfs_fstat", self.fat32)
        self.assertIn("fat32_refresh_file_node(node)", self.fat32[
            self.fat32.index("fat32_vfs_fstat"):])
        self.assertIn("ext2_vfs_fstat", self.ext2)
        self.assertIn("ext2_read_inode", self.ext2[
            self.ext2.index("ext2_vfs_fstat"):])

    def test_syscalls_validate_output_and_non_file_descriptors(self) -> None:
        self.assertIn("case SYS_LSEEK:", self.syscall)
        self.assertIn("case SYS_FSTAT:", self.syscall)
        start = self.syscall.index("static int syscall_fstat(")
        end = self.syscall.index("static int syscall_readdir(", start)
        block = self.syscall[start:end]
        self.assertLess(block.index("user_range_accessible"),
                        block.index("process_file_fstat"))
        self.assertIn("return -REIST_ESPIPE;", self.process_c)

    def test_guest_proves_partial_eof_gap_append_and_metadata(self) -> None:
        self.assertIn("test_descriptor_seek_fstat", self.guest)
        for token in ("x86os_lseek", "x86os_fstat", "X86OS_SEEK_SET",
                      "X86OS_SEEK_CUR", "X86OS_SEEK_END",
                      "DESCRIPTOR_SEEK_FSTAT_OK"):
            self.assertIn(token, self.guest)


if __name__ == "__main__":
    unittest.main()
