import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class ReistVfsShadowFat32Tests(unittest.TestCase):
    def test_host_parser_behavior(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = pathlib.Path(directory) / "vfs_shadow_fat32.exe"
            subprocess.run([
                "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                f"-I{ROOT}", f"-I{ROOT / 'userspace/sdk/include'}",
                str(ROOT / "test/test_vfs_shadow_fat32_host.c"),
                str(ROOT / "userspace/storage/lib/vfs_shadow_fat32.c"),
                "-o", str(executable),
            ], check=True, cwd=ROOT)
            subprocess.run([str(executable)], check=True, cwd=ROOT)

    def test_parser_has_fixed_authority_and_work_bounds(self):
        source = (ROOT / "userspace/storage/lib/vfs_shadow_fat32.c").read_text()
        header = (ROOT / "userspace/storage/include/reist/vfs_shadow_fat32.h").read_text()
        for bound in ("MAX_RESOURCES 22U", "MAX_COMPONENTS 32U",
                      "MAX_CHAIN_CLUSTERS 32U", "MAX_SECTOR_READS 64U"):
            self.assertIn(bound, header)
        self.assertIn("volume->reads >= REIST_VFS_SHADOW_MAX_SECTOR_READS", source)
        self.assertIn("visited[REIST_VFS_SHADOW_MAX_CHAIN_CLUSTERS]", source)
        self.assertIn("FAT12_CLUSTER_LIMIT 4085U", source)
        self.assertIn("cluster + cluster / 2U", source)
        self.assertIn("volume->root_dir_start + index", source)
        self.assertIn("reist_vfs_shadow_fat_stat", header)
        self.assertNotIn("malloc(", source)
        self.assertNotIn("x86os_syscall", source)

    def test_service_publishes_only_exact_legacy_equivalence(self):
        service = (ROOT / "userspace/programs/storage_service.c").read_text()
        self.assertIn("reist_vfs_shadow_fat32_stat", service)
        self.assertIn("reist_vfs_shadow_fat_stat", service)
        self.assertIn("parsed_status != legacy_status", service)
        self.assertIn("format_equal(\n                parsed_bytes, legacy_bytes", service)
        self.assertIn("status = -84", service)
        self.assertIn("selected = &parsed_info", service)


if __name__ == "__main__":
    unittest.main()
