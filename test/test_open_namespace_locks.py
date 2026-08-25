"""Source-contract tests for bounded open-object namespace locks."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class OpenNamespaceLockTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (ROOT / "fs/vfs/vfs.h").read_text(encoding="utf-8")
        cls.vfs = (ROOT / "fs/vfs/vfs.c").read_text(encoding="utf-8")
        cls.fat12 = (ROOT / "fs/fat12/fat12_vfs_adapter.c").read_text(
            encoding="utf-8")
        cls.fat32 = (ROOT / "fs/fat32/fat32_vfs_adapter.c").read_text(
            encoding="utf-8")
        cls.guest = (ROOT / "userspace/programs/guest_test.c").read_text(
            encoding="utf-8")

    def test_fixed_table_is_publicly_bounded(self) -> None:
        self.assertIn("#define VFS_OPEN_NODE_CAPACITY 256U", self.header)
        self.assertIn("open_node_records[VFS_OPEN_NODE_CAPACITY]", self.vfs)
        self.assertIn("vfs_open_record_free_slot", self.vfs)

    def test_registration_precedes_publication_and_close_is_success_bound(self) -> None:
        opened = self.vfs[self.vfs.index("static int vfs_open_locked"):
                          self.vfs.index("static int vfs_close_locked")]
        self.assertLess(opened.index("open_node_records[record_slot].node"),
                        opened.index("fs->open_nodes++"))
        closed = self.vfs[self.vfs.index("static int vfs_close_locked"):
                          self.vfs.index("static int vfs_path_open_locked")]
        self.assertIn("if (result == VFS_OK", closed)
        self.assertLess(closed.index("fs->ops->close(node)"),
                        closed.index("open_node_records[record_slot].node = NULL"))

    def test_all_destructive_namespace_paths_check_the_lock(self) -> None:
        for start, end in (
            ("static int vfs_rmdir_locked", "static int vfs_space_locked"),
            ("static int vfs_delete_locked", "static int vfs_stat_locked"),
            ("static int vfs_rename_locked", "static int vfs_touch_locked"),
        ):
            body = self.vfs[self.vfs.index(start):self.vfs.index(end)]
            self.assertIn("vfs_path_open_locked", body)
            self.assertIn("VFS_ERR_BUSY", body)

    def test_fat_identity_uses_directory_locators(self) -> None:
        fat12 = self.fat12[self.fat12.index("fat12_vfs_same_object"):
                           self.fat12.index("static int fat12_vfs_space")]
        self.assertIn("directory_sector", fat12)
        self.assertIn("directory_slot", fat12)
        start = self.fat32.index("fat32_vfs_same_object")
        fat32 = self.fat32[
            start:self.fat32.index("static int fat32_vfs_mount(", start)]
        self.assertIn("parent_cluster", fat32)
        self.assertIn("left->entry.name", fat32)

    def test_guest_proves_lock_and_release(self) -> None:
        self.assertIn("test_open_namespace_locks", self.guest)
        self.assertIn("TEST_STAGE OPEN_NAMESPACE_LOCKS_OK", self.guest)


if __name__ == "__main__":
    unittest.main()
