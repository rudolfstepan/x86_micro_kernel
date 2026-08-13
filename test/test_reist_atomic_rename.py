"""Contracts for journal-backed rename and safe editor replacement."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


class ReistAtomicRenameTests(unittest.TestCase):
    def test_syscall_is_appended_and_exported_by_sdk(self) -> None:
        kernel_h = read("lib/libc/stdlib.h")
        sdk_h = read("userspace/sdk/include/x86os.h")
        sdk = read("userspace/sdk/x86os.c")
        dispatch = read("kernel/syscall/syscall_table.c")
        self.assertIn("#define SYS_RENAME 47", kernel_h)
        self.assertIn("X86OS_SYS_RENAME = 47", sdk_h)
        self.assertIn("int x86os_rename(const char* old_path", sdk_h)
        self.assertIn("X86OS_SYS_RENAME", sdk)
        self.assertIn("case SYS_RENAME:", dispatch)
        self.assertIn("(void*)&syscall_rename", dispatch)

    def test_vfs_rename_is_one_mutation_transaction(self) -> None:
        header = read("fs/vfs/vfs.h")
        vfs = read("fs/vfs/vfs.c")
        self.assertIn("int vfs_rename(const char* old_path", header)
        start = vfs.index("int vfs_rename(const char* old_path")
        end = vfs.index("\n}", start)
        wrapper = vfs[start:end]
        self.assertEqual(wrapper.count("vfs_mutation_begin()"), 1)
        self.assertEqual(wrapper.count("vfs_mutation_finish("), 1)
        self.assertIn("vfs_rename_locked(old_path, new_path)", wrapper)

    def test_fat32_replace_commits_destination_before_tombstone(self) -> None:
        adapter = read("fs/fat32/fat32_vfs_adapter.c")
        start = adapter.index("static int fat32_vfs_rename_unlocked(")
        end = adapter.index("static int fat32_vfs_stat_unlocked(", start)
        rename = adapter[start:end]
        destination = rename.index(
            "update_directory_entry(new_parent, destination.name, &renamed)"
        )
        tombstone = rename.index(
            "update_directory_entry(old_parent, source.name, &tombstone)"
        )
        reclaim = rename.index("free_cluster_chain(&boot_sector, replaced_cluster)")
        self.assertLess(destination, tombstone)
        self.assertLess(tombstone, reclaim)
        self.assertIn("old_parent != new_parent", rename)

    def test_editor_never_unlinks_original_before_commit(self) -> None:
        editor = read("userspace/bin/edit.c")
        start = editor.index("static int save_file(")
        end = editor.index("static void move_cursor(", start)
        save = editor[start:end]
        self.assertIn("x86os_create(temp)", save)
        self.assertIn("x86os_rename(temp, path)", save)
        self.assertNotIn("x86os_unlink(path)", save)
        self.assertLess(save.index("x86os_close(descriptor)"),
                        save.index("x86os_rename(temp, path)"))


if __name__ == "__main__":
    unittest.main()
