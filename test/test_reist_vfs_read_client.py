import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


class ReistVfsReadClientTests(unittest.TestCase):
    def test_host_client_behavior(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = pathlib.Path(directory) / "vfs_read_client.exe"
            subprocess.run([
                "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                f"-I{ROOT}", f"-I{ROOT / 'userspace/sdk/include'}",
                str(ROOT / "test/test_vfs_read_client_host.c"),
                str(ROOT / "userspace/storage/lib/vfs_read_client.c"),
                str(ROOT / "userspace/storage/lib/vfs_path.c"),
                "-o", str(executable),
            ], check=True, cwd=ROOT)
            subprocess.run([str(executable)], check=True, cwd=ROOT)

    def test_clients_are_bounded_and_authoritative(self):
        source = read("userspace/storage/lib/vfs_read_client.c")
        for token in ("X86OS_VFS_SHADOW_FS_READ_AT",
                      "X86OS_VFS_SHADOW_FS_READDIR_AT",
                      "now >= deadline", "x86os_storage_cancel(handle)"):
            self.assertIn(token, source)
        for forbidden in ("x86os_open(", "x86os_read(", "x86os_readdir",
                          "malloc(", "free("):
            self.assertNotIn(forbidden, source)

    def test_cat_and_ls_have_no_legacy_fallback(self):
        cat = read("userspace/programs/cat.c")
        ls = read("userspace/programs/ls.c")
        build = read("scripts/build_system_programs.py")
        self.assertIn("reist_vfs_file_read(", cat)
        for forbidden in ("x86os_open(", "x86os_read(", "x86os_close("):
            self.assertNotIn(forbidden, cat)
        self.assertIn("reist_vfs_stat(", ls)
        self.assertIn("reist_vfs_readdir_at(", ls)
        self.assertNotIn("x86os_stat(", ls)
        self.assertNotIn("x86os_readdir", ls)
        self.assertIn('"CAT.PRG": (', build)
        self.assertIn('"LS.PRG": (', build)
        self.assertGreaterEqual(build.count("vfs_read_client.c"), 3)

    def test_find_and_tree_use_authoritative_bounded_walks(self):
        build = read("scripts/build_system_programs.py")
        guest = read("userspace/programs/guest_test.c")
        for name in ("find", "tree"):
            source = read(f"userspace/programs/{name}.c")
            self.assertIn("reist_vfs_stat(", source)
            self.assertIn("reist_vfs_readdir_at(", source)
            self.assertIn("x86os_monotonic_ms(", source)
            self.assertIn("DEADLINE_MS 5000U", source)
            self.assertIn("REQUEST_TIMEOUT_MS 1000U", source)
            self.assertIn("UINT64_MAX - started", source)
            self.assertNotIn("x86os_stat(", source)
            self.assertNotIn("x86os_readdir", source)
        for program in ("TREE.PRG", "FIND.PRG"):
            start = build.index(f'"{program}": (')
            end = build.index("),", start)
            mapping = build[start:end]
            self.assertIn("vfs_stat_client.c", mapping)
            self.assertIn("vfs_read_client.c", mapping)
            self.assertIn("vfs_path.c", mapping)
        self.assertIn("TEST_STAGE VFS_READONLY_WALKERS_OK", guest)
        self.assertIn('static const char root[] = "/htdocs"', guest)
        self.assertIn('static const char match[] = "about.txt"', guest)


if __name__ == "__main__":
    unittest.main()
