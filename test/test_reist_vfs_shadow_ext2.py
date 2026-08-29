import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class ReistVfsShadowExt2Tests(unittest.TestCase):
    def test_host_parser_behavior(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = pathlib.Path(directory) / "vfs_shadow_ext2.exe"
            subprocess.run([
                "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                f"-I{ROOT}", f"-I{ROOT / 'userspace/sdk/include'}",
                str(ROOT / "test/test_vfs_shadow_ext2_host.c"),
                str(ROOT / "userspace/storage/lib/vfs_shadow_ext2.c"),
                "-o", str(executable),
            ], check=True, cwd=ROOT)
            subprocess.run([str(executable)], check=True, cwd=ROOT)

    def test_parser_has_fixed_standard_subset_and_work_bounds(self):
        source = (ROOT / "userspace/storage/lib/vfs_shadow_ext2.c").read_text()
        header = (ROOT / "userspace/storage/include/reist/vfs_shadow_ext2.h").read_text()
        for bound in ("MAX_COMPONENTS 16U", "MAX_DIRECTORY_BLOCKS 32U",
                      "MAX_SECTOR_READS 192U", "MAX_BLOCK_SIZE 4096U"):
            self.assertIn(bound, header)
        self.assertIn("EXT2_SIGNATURE 0xEF53U", source)
        self.assertIn("EXT2_INCOMPAT_FILETYPE", source)
        self.assertIn("EXT2_SINGLE_INDIRECT_INDEX", source)
        self.assertIn("reist_vfs_shadow_ext2_read", source)
        self.assertIn("reist_vfs_shadow_ext2_readdir", source)
        self.assertIn("reist_vfs_shadow_ext2_object_open", header)
        self.assertIn("reist_vfs_shadow_ext2_object_read", header)
        self.assertIn("reist_vfs_shadow_ext2_readdir_cursor_t", header)
        self.assertIn("reist_vfs_shadow_ext2_readdir_continue", header)
        self.assertIn("ext2_readdir_resume_valid", source)
        self.assertIn("cursor->directory_signature", source)
        self.assertIn("volume->signature", source)
        self.assertIn("object->locator_a", source)
        self.assertIn("inode->bytes + 100U", source)
        self.assertIn("volume->reads >= REIST_VFS_SHADOW_EXT2_MAX_SECTOR_READS",
                      source)
        self.assertNotIn("malloc(", source)
        self.assertNotIn("free(", source)
        self.assertNotIn("x86os_syscall", source)


if __name__ == "__main__":
    unittest.main()
