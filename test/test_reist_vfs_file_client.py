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

    def test_session_is_fixed_generation_safe_and_service_object_backed(self):
        source = read("userspace/storage/lib/vfs_file_client.c")
        header = read("userspace/storage/include/reist/vfs_file_client.h")
        for token in ("REIST_VFS_FILE_CAPACITY 4U", "REIST_VFS_SEEK_SET",
                      "REIST_VFS_SEEK_CUR", "REIST_VFS_SEEK_END"):
            self.assertIn(token, header)
        for token in ("FILE_HANDLE_GENERATION_MAX", "session->retired = 1U",
                      "reist_vfs_resolve_path", "session->object_token",
                      "session->service_generation",
                      "X86OS_VFS_SHADOW_OBJECT_OPEN",
                      "X86OS_VFS_SHADOW_OBJECT_READ",
                      "X86OS_VFS_SHADOW_OBJECT_FSTAT",
                      "X86OS_VFS_SHADOW_OBJECT_CLOSE",
                      "X86OS_VFS_SHADOW_OBJECT_OPEN_RIGHTS",
                      "X86OS_VFS_SHADOW_OBJECT_DELEGATE",
                      "X86OS_VFS_SHADOW_OBJECT_ADOPT"):
            self.assertIn(token, source)
        for token in ("reist_vfs_file_read_bulk",
                      "X86OS_VFS_SHADOW_OBJECT_BULK_READ",
                      "X86OS_STORAGE_VFS_BULK_READ", "file_crc32"):
            self.assertIn(token, source)
        for forbidden in ("malloc(", "free(", "x86os_open(", "x86os_read(",
                          "x86os_close(", "x86os_readdir", "reist_vfs_read_at",
                          "reist_vfs_stat", "char path["):
            self.assertNotIn(forbidden, source)

    def test_rights_are_local_and_service_enforced_before_publication(self):
        source = read("userspace/storage/lib/vfs_file_client.c")
        service = read("userspace/programs/storage_service.c")
        header = read("userspace/storage/include/reist/vfs_file_client.h")
        for token in ("REIST_VFS_FILE_RIGHT_READ",
                      "REIST_VFS_FILE_RIGHT_SEEK",
                      "REIST_VFS_FILE_RIGHT_STAT",
                      "REIST_VFS_FILE_RIGHT_DELEGATE",
                      "reist_vfs_file_open_rights",
                      "reist_vfs_file_delegate", "reist_vfs_file_adopt"):
            self.assertIn(token, header)
        self.assertIn("session->rights", source)
        self.assertIn("rights & ~session->rights", source)
        self.assertIn("slot->rights & X86OS_VFS_OBJECT_RIGHT_READ", service)
        self.assertIn("slot->rights & X86OS_VFS_OBJECT_RIGHT_STAT", service)
        self.assertIn("source->rights & X86OS_VFS_OBJECT_RIGHT_DELEGATE",
                      service)
        self.assertIn("frame->rights & ~source->rights", service)

    def test_guest_uses_explicit_generation_scoped_child_adoption(self):
        guest = read("userspace/programs/guest_test.c")
        build = read("scripts/build_system_programs.py")
        for token in ("VFS_ADOPT", "x86os_process_identity_of(child",
                      "reist_vfs_file_delegate", "reist_vfs_file_adopt",
                      "VFS_EXPIRE", "index < 4U", "!= -24",
                      "x86os_sleep_ms(7000U)",
                      "TEST_STAGE STORAGE_VFS_DELEGATION_OK"):
            self.assertIn(token, guest)
        self.assertIn('"GTEST.PRG": (', build)
        self.assertIn("userspace/storage/lib/vfs_file_client.c", build)

    def test_storage_rescue_growth_keeps_the_aggregate_pool_fixed(self):
        process = read("kernel/proc/process.c")
        self.assertIn("RESCUE_PROGRAM_CACHE_CAPACITY (224U * 1024U)",
                      process)
        self.assertIn("RESCUE_PROGRAM_POOL_CAPACITY (448U * 1024U)",
                      process)

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
