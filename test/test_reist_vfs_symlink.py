import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


class ReistVfsSymlinkTests(unittest.TestCase):
    def test_host_resolution_creation_and_interruption_behavior(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = pathlib.Path(directory) / "vfs_symlink.exe"
            subprocess.run([
                "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                f"-I{ROOT}", f"-I{ROOT / 'userspace/sdk/include'}",
                str(ROOT / "test/test_reist_vfs_symlink_host.c"),
                str(ROOT / "userspace/storage/lib/vfs_shadow_ext2.c"),
                str(ROOT / "userspace/storage/lib/vfs_symlink_client.c"),
                str(ROOT / "userspace/storage/lib/vfs_path.c"),
                "-o", str(executable),
            ], check=True, cwd=ROOT)
            subprocess.run([str(executable)], check=True, cwd=ROOT)

    def test_public_abi_is_append_only_and_posix_shaped(self):
        public = read("userspace/sdk/include/x86os.h")
        pool = read("include/kernel/storage_request_pool.h")
        pool_source = read("kernel/init/storage_request_pool.c")
        syscalls = read("kernel/syscall/syscall_table.c")
        client = read("userspace/storage/lib/vfs_symlink_client.c")
        for token in (
                "X86OS_STORAGE_VFS_SHADOW_STAT 31U",
                "X86OS_STORAGE_VFS_BULK_READ 32U",
                "X86OS_STORAGE_VFS_SYMLINK 33U",
                "X86OS_VFS_SHADOW_FS_LSTAT 16U",
                "X86OS_VFS_SHADOW_FS_READLINK 17U",
                "X86OS_VFS_SHADOW_FS_SYMLINK 18U",
                "X86OS_VFS_SHADOW_OBJECT_OPEN_FLAGS 19U",
                "X86OS_O_NOFOLLOW",
                "x86os_vfs_symlink_frame_t"):
            self.assertIn(token, public)
        self.assertIn("STORAGE_REQUEST_VFS_SYMLINK = 33", pool)
        self.assertIn("request->resource != 0U || request->offset != 0U",
                      pool_source)
        self.assertIn("process->domain_profile.kind != "
                      "PROCESS_DOMAIN_COMPATIBILITY", syscalls)
        self.assertIn("MAX_RECOVERY_RETRIES 1U", read(
            "userspace/storage/include/reist/vfs_symlink_client.h"))
        self.assertIn("reist_vfs_resolve_path", client)
        self.assertIn("x86os_storage_cancel(handle)", client)
        self.assertNotIn("malloc(", client)
        self.assertNotIn("free(", client)

    def test_ext2_owner_has_fixed_walk_and_transaction_bounds(self):
        source = read("userspace/storage/lib/vfs_shadow_ext2.c")
        header = read("userspace/storage/include/reist/vfs_shadow_ext2.h")
        for token in (
                "MAX_LINK_DEPTH 8U",
                "MAX_WALK_COMPONENTS 64U",
                "MAX_TRANSACTION_READS 384U",
                "MAX_TRANSACTION_WRITES 64U",
                "MAX_TRANSACTION_FLUSHES 8U",
                "JOURNAL_SECTORS 26U",
                "MAX_JOURNAL_ENTRIES 24U",
                "MAX_ALLOCATION_GROUPS 32U"):
            self.assertIn(token, header)
        for token in (
                "EXT2_S_IFLNK", "EXT2_FAST_SYMLINK_CAPACITY",
                "EXT2_JOURNAL_STATE_ACTIVE",
                "EXT2_JOURNAL_STATE_COMMITTED",
                "ext2_journal_restore", "ext2_transaction_verify",
                "ext2_plan_directory_relocate", "sector_first_offset",
                "ext2_plan_directory_relocate_cross_sector",
                "REIST_VFS_SHADOW_EXT2_NOFOLLOW_FINAL",
                "components >= REIST_VFS_SHADOW_EXT2_MAX_COMPONENTS",
                "publications > 2U", "current_crc != header->old_crc",
                "return -40", "return -95"):
            self.assertIn(token, source)
        self.assertNotIn("malloc(", source)
        self.assertNotIn("free(", source)
        self.assertNotIn("x86os_syscall", source)

    def test_service_and_tools_use_only_ring3_storage_contract(self):
        service = read("userspace/programs/storage_service.c")
        build = read("scripts/build_system_programs.py")
        makefile = read("Makefile")
        ln = read("userspace/programs/ln.c")
        readlink = read("userspace/programs/readlink.c")
        for token in (
                "X86OS_STORAGE_VFS_SYMLINK",
                "X86OS_VFS_SHADOW_FS_LSTAT",
                "X86OS_VFS_SHADOW_FS_READLINK",
                "X86OS_VFS_SHADOW_FS_SYMLINK",
                "X86OS_VFS_SHADOW_OBJECT_OPEN_FLAGS"):
            self.assertIn(token, service)
        self.assertIn('"LN.PRG"', build)
        self.assertIn('"READLINK.PRG"', build)
        self.assertIn("bin/ln.prg", makefile)
        self.assertIn("bin/readlink.prg", makefile)
        self.assertIn("reist_vfs_symlink", ln)
        self.assertIn("reist_vfs_readlink", readlink)
        for source in (ln, readlink):
            self.assertNotIn("x86os_symlink", source)
            self.assertNotIn("x86os_readlink", source)


if __name__ == "__main__":
    unittest.main()
