"""Build and run the filesystem host regression harnesses."""

from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
GCC = shutil.which("gcc")


class FilesystemSourceTests(unittest.TestCase):
    def test_fat12_directory_reads_do_not_print_diagnostics(self) -> None:
        source = (ROOT / "fs" / "fat12" / "fat12.c").read_text(encoding="utf-8")
        self.assertNotIn("Reading subdirectory. Start cluster:", source)


@unittest.skipUnless(GCC, "gcc is required for the C host regressions")
class FilesystemHostTests(unittest.TestCase):
    def build_and_run(self, name: str, sources: list[str]) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / (
                name + (".exe" if os.name == "nt" else "")
            )
            command = [
                GCC,
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fno-builtin",
                "-DKERNEL_HOST_TEST",
                f"-I{ROOT}",
                *(str(ROOT / source) for source in sources),
                "-o",
                str(executable),
            ]
            subprocess.run(command, check=True, cwd=ROOT, capture_output=True)
            subprocess.run([str(executable)], check=True, cwd=ROOT,
                           capture_output=True, timeout=10)

    def test_vfs_mount_lifecycle_and_path_boundaries(self) -> None:
        self.build_and_run(
            "test_vfs_host",
            ["test/test_vfs_host.c", "fs/vfs/vfs.c"],
        )

    def test_shell_path_normalization_and_drive_syntax(self) -> None:
        self.build_and_run(
            "test_shell_path_host",
            [
                "test/test_shell_path_host.c",
                "kernel/shell/path_resolver.c",
            ],
        )

    def test_custom_program_image_validation(self) -> None:
        self.build_and_run(
            "test_program_image_host",
            [
                "test/test_program_image_host.c",
                "kernel/proc/program_image.c",
            ],
        )

    def test_fat32_write_truncate_and_directory_extension(self) -> None:
        self.build_and_run(
            "test_fat32_host",
            [
                "test/test_fat32_host.c",
                "fs/fat32/fat32.c",
                "fs/fat32/fat32_cluster.c",
                "fs/fat32/fat32_dir.c",
                "fs/fat32/fat32_files.c",
                "fs/fat32/fat32_vfs_adapter.c",
                "fs/vfs/vfs.c",
            ],
        )

    def test_ext2_partition_directory_and_indirect_blocks(self) -> None:
        self.build_and_run(
            "test_ext2_host",
            ["test/test_ext2_host.c", "fs/ext2/ext2.c"],
        )

    def test_fat12_cluster_chain_seek_and_subdirectory_lifetime(self) -> None:
        self.build_and_run(
            "test_fat12_host",
            [
                "test/test_fat12_host.c",
                "fs/fat12/fat12.c",
                "fs/fat12/fat12_vfs_adapter.c",
                "lib/libc/string.c",
            ],
        )


if __name__ == "__main__":
    unittest.main()
