"""Contract tests for bounded transactional FAT ftruncate."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


class FtruncateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.abi = read("include/reist/abi/syscall.h")
        cls.sdk_h = read("userspace/sdk/include/x86os.h")
        cls.sdk_c = read("userspace/sdk/x86os.c")
        cls.process_h = read("kernel/proc/process.h")
        cls.process_c = read("kernel/proc/process.c")
        cls.syscall = read("kernel/syscall/syscall_table.c")
        cls.vfs_c = read("fs/vfs/vfs.c")
        cls.fat12 = read("fs/fat12/fat12_vfs_adapter.c")
        cls.fat32 = read("fs/fat32/fat32_vfs_adapter.c")
        cls.ext2 = read("fs/ext2/ext2_vfs_adapter.c")
        cls.guest = read("userspace/programs/guest_test.c")

    def test_syscall_is_append_only_and_public(self) -> None:
        self.assertIn("X(FTRUNCATE, FTRUNCATE, 123U)", self.abi)
        self.assertIn("X86OS_SYS_FTRUNCATE = 123", self.sdk_h)
        self.assertIn("int x86os_ftruncate(int descriptor, uint32_t size);",
                      self.sdk_h)
        self.assertIn("X86OS_SYS_FTRUNCATE", self.sdk_c)
        self.assertIn("case SYS_FTRUNCATE:", self.syscall)

    def test_descriptor_requires_write_and_preserves_offset(self) -> None:
        start = self.process_c.index("int process_file_truncate(")
        end = self.process_c.index("int process_file_seek(", start)
        body = self.process_c[start:end]
        self.assertIn("!file->writable", body)
        self.assertIn("vfs_truncate(file->node, size)", body)
        self.assertNotIn("file->offset =", body)
        self.assertIn("PROCESS_DOMAIN_SYSCALL_LIMIT 125U", self.process_h)

    def test_vfs_delegates_every_uint32_target_under_mutation_fence(self) -> None:
        start = self.vfs_c.index("static int vfs_truncate_locked(")
        end = self.vfs_c.index("static int vfs_fstat_locked(", start)
        body = self.vfs_c[start:end]
        self.assertNotIn("size != 0U", body)
        self.assertIn("node->fs->ops->truncate(node, size)", body)
        public = self.vfs_c[self.vfs_c.index("int vfs_truncate("):]
        self.assertIn("vfs_mutation_begin()", public)

    def test_fat12_preflights_and_commits_one_transaction(self) -> None:
        start = self.fat12.index("static int fat12_vfs_truncate(")
        end = self.fat12.index("static int fat12_vfs_", start + 20)
        body = self.fat12[start:end]
        self.assertIn("fat12_truncate_transaction_sectors", body)
        self.assertIn("FAT12_JOURNAL_MAX_ENTRIES", body)
        self.assertIn("fat12_zero_range", body)
        self.assertIn("fat12_detach_chain_suffix", body)
        self.assertLess(body.index("fat12_transaction_begin"),
                        body.index("fat12_write_entry"))
        self.assertIn("fat12_is_critical_name", body)

    def test_fat32_orders_extension_and_shrink_safely(self) -> None:
        start = self.fat32.index("static int fat32_vfs_truncate_unlocked(")
        end = self.fat32.index("static int fat32_vfs_", start + 20)
        body = self.fat32[start:end]
        grow = body[body.index("if (size > original_size)"):]
        self.assertLess(grow.index("write_file_data_at_checked"),
                        grow.index("fat32_commit_node_data"))
        shrink = body[body.index("if (size != 0U)"):]
        self.assertLess(shrink.index("fat32_commit_node_data"),
                        shrink.index("fat32_reclaim_chain_suffix"))
        self.assertNotIn("node->size = original_size", body)
        self.assertIn(".truncate = ext2_vfs_truncate", self.ext2)

    def test_guest_proves_size_offset_zero_fill_and_rights(self) -> None:
        self.assertIn("test_ftruncate", self.guest)
        for token in ("x86os_ftruncate", "FTRUNCATE_OK",
                      "X86OS_O_WRONLY", "REIST_EBADF",
                      "X86OS_SEEK_CUR"):
            self.assertIn(token, self.guest)


if __name__ == "__main__":
    unittest.main()
