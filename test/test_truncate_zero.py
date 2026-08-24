"""Source contract for transactional FAT truncate-to-zero."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


class TruncateZeroTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.vfs_h = read("fs/vfs/vfs.h")
        cls.vfs_c = read("fs/vfs/vfs.c")
        cls.fat12 = read("fs/fat12/fat12_vfs_adapter.c")
        cls.fat32 = read("fs/fat32/fat32_vfs_adapter.c")
        cls.process = read("kernel/proc/process.c")
        cls.guest = read("userspace/programs/guest_test.c")
        cls.fat12_test = read("test/test_fat12_host.c")
        cls.fat32_test = read("test/test_fat32_host.c")

    def test_vfs_exposes_bounded_adapter_truncate(self) -> None:
        self.assertIn("int (*truncate)(vfs_node_t* node, uint32_t size);",
                      self.vfs_h)
        self.assertIn("int vfs_truncate(vfs_node_t* node, uint32_t size);",
                      self.vfs_h)
        start = self.vfs_c.index("static int vfs_truncate_locked(")
        end = self.vfs_c.index("static int vfs_", start + 20)
        body = self.vfs_c[start:end]
        self.assertIn("node->type != VFS_FILE", body)
        self.assertNotIn("size != 0U", body)
        self.assertIn("!node->fs->ops->truncate", body)
        public = self.vfs_c[self.vfs_c.index("int vfs_truncate("):]
        self.assertIn("vfs_mutation_begin()", public)
        self.assertIn("vfs_mutation_finish(armed, result)", public)

    def test_fat12_uses_one_existing_undo_transaction(self) -> None:
        start = self.fat12.index("static int fat12_vfs_truncate(")
        end = self.fat12.index("static int fat12_vfs_", start + 20)
        body = self.fat12[start:end]
        begin = body.index("fat12_transaction_begin(transaction_sectors)")
        free = body.index("fat12_detach_chain_suffix")
        publish = body.index("fat12_write_entry")
        commit = body.index("fat12_transaction_commit")
        self.assertLess(begin, free)
        self.assertLess(free, publish)
        self.assertLess(publish, commit)
        self.assertIn("FAT12_JOURNAL_MAX_ENTRIES", body)
        self.assertIn("fat12_is_critical_name", body)
        self.assertIn(".truncate = fat12_vfs_truncate", self.fat12)

    def test_fat32_detaches_namespace_before_reclaim(self) -> None:
        start = self.fat32.index("static int fat32_vfs_truncate_unlocked(")
        end = self.fat32.index("static int fat32_vfs_", start + 20)
        body = self.fat32[start:end]
        shrink = body[body.index("uint32_t published_cluster"):]
        detach = shrink.index("fat32_commit_node_data(node, published_cluster")
        reclaim = shrink.index("free_cluster_chain")
        self.assertLess(detach, reclaim)
        self.assertNotIn("node->size = original_size", body)
        self.assertIn(".truncate = fat32_vfs_truncate", self.fat32)

    def test_open_truncates_before_descriptor_publication(self) -> None:
        start = self.process.index("int process_file_open_flags(")
        end = self.process.index("int process_file_read(", start)
        body = self.process[start:end]
        self.assertIn("PROCESS_OPEN_TRUNC", body)
        self.assertIn("access_mode == PROCESS_OPEN_RDONLY", body)
        truncate = body.index("vfs_truncate(node, 0U)")
        publish = body.index("process->files[slot] =")
        self.assertLess(truncate, publish)
        self.assertIn("if (created) (void)vfs_delete(resolved);", body)

    def test_host_and_guest_prove_success_failure_and_read_only(self) -> None:
        self.assertGreaterEqual(self.fat12_test.count("ops->truncate"), 3)
        self.assertGreaterEqual(self.fat32_test.count("vfs_truncate"), 4)
        self.assertIn("fail_directory_write_once = true", self.fat32_test)
        self.assertIn("X86OS_O_WRONLY | X86OS_O_TRUNC", self.guest)
        self.assertIn("info.size != 0U", self.guest)
        self.assertIn("TEST_STAGE OPEN_FLAGS_OK", self.guest)


if __name__ == "__main__":
    unittest.main()
