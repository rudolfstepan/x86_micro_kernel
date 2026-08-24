import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


class ReistVfsFileClientTests(unittest.TestCase):
    def test_host_session_behavior(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = pathlib.Path(directory) / "vfs_file_client.exe"
            subprocess.run([
                "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                f"-I{ROOT}", f"-I{ROOT / 'userspace/sdk/include'}",
                str(ROOT / "test/test_vfs_file_client_host.c"),
                str(ROOT / "userspace/storage/lib/vfs_file_client.c"),
                "-o", str(executable),
            ], check=True, cwd=ROOT)
            subprocess.run([str(executable)], check=True, cwd=ROOT)

    def test_session_is_fixed_generation_safe_and_path_backed(self):
        source = read("userspace/storage/lib/vfs_file_client.c")
        header = read("userspace/storage/include/reist/vfs_file_client.h")
        for token in ("REIST_VFS_FILE_CAPACITY 4U", "REIST_VFS_SEEK_SET",
                      "REIST_VFS_SEEK_CUR", "REIST_VFS_SEEK_END"):
            self.assertIn(token, header)
        for token in ("FILE_HANDLE_GENERATION_MAX", "session->retired = 1U",
                      "reist_vfs_resolve_path", "reist_vfs_read_at",
                      "reist_vfs_stat"):
            self.assertIn(token, source)
        for forbidden in ("malloc(", "free(", "x86os_open(", "x86os_read(",
                          "x86os_close(", "x86os_readdir"):
            self.assertNotIn(forbidden, source)

    def test_cat_and_http_use_only_ring3_read_clients(self):
        cat = read("userspace/programs/cat.c")
        http = read("userspace/programs/httpd.c")
        self.assertIn("reist_vfs_file_open(", cat)
        self.assertIn("reist_vfs_file_read(", cat)
        self.assertIn("reist_vfs_file_close(", cat)
        for token in ("reist_vfs_file_open(", "reist_vfs_file_read(",
                      "reist_vfs_file_close(", "reist_vfs_readdir_at("):
            self.assertIn(token, http)
        for source in (cat, http):
            for forbidden in ("x86os_open(", "x86os_read(", "x86os_close(",
                              "x86os_readdir(", "x86os_readdir_batch(",
                              "x86os_stat("):
                self.assertNotIn(forbidden, source)


if __name__ == "__main__":
    unittest.main()
