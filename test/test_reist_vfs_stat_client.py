import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


class ReistVfsStatClientTests(unittest.TestCase):
    def test_host_client_behavior(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = pathlib.Path(directory) / "vfs_stat_client.exe"
            subprocess.run([
                "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                f"-I{ROOT}", f"-I{ROOT / 'userspace/sdk/include'}",
                str(ROOT / "test/test_vfs_stat_client_host.c"),
                str(ROOT / "userspace/storage/lib/vfs_stat_client.c"),
                "-o", str(executable),
            ], check=True, cwd=ROOT)
            subprocess.run([str(executable)], check=True, cwd=ROOT)

    def test_stat_program_has_no_legacy_fallback(self):
        program = read("userspace/programs/stat.c")
        build = read("scripts/build_system_programs.py")
        self.assertIn("reist_vfs_stat(", program)
        self.assertNotIn("x86os_stat(", program)
        self.assertIn('ROOT / "userspace/storage/lib/vfs_stat_client.c"', build)
        mapping = build[build.index('"STAT.PRG"'):build.index('"DF.PRG"')]
        self.assertIn("vfs_stat_client.c", mapping)

    def test_adapter_is_fixed_bounded_and_generic_fat_only(self):
        source = read("userspace/storage/lib/vfs_stat_client.c")
        self.assertIn("X86OS_VFS_SHADOW_FAT_STAT", source)
        self.assertNotIn("X86OS_VFS_SHADOW_FAT32_STAT", source)
        self.assertIn("now >= deadline", source)
        self.assertIn("x86os_sleep_ms(1U)", source)
        self.assertIn("x86os_storage_cancel(handle)", source)
        self.assertIn("client_frame_valid", source)
        self.assertNotIn("x86os_stat(", source)
        self.assertNotIn("malloc(", source)

    def test_guest_executes_packaged_stat_client(self):
        guest = read("userspace/programs/guest_test.c")
        self.assertIn('"/bin/stat.prg", "/GUEST.TMP"', guest)
        self.assertIn("STORAGE_VFS_STAT_CLIENT_OK", guest)


if __name__ == "__main__":
    unittest.main()
