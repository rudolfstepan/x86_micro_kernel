import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


class ReistVfsNamespaceTests(unittest.TestCase):
    def test_client_host_contract(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = pathlib.Path(directory) / "vfs_namespace.exe"
            subprocess.run([
                "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                f"-I{ROOT}", f"-I{ROOT / 'userspace/sdk/include'}",
                str(ROOT / "test/test_reist_vfs_namespace_host.c"),
                str(ROOT / "userspace/storage/lib/vfs_namespace_client.c"),
                str(ROOT / "userspace/storage/lib/vfs_path.c"),
                "-o", str(executable),
            ], check=True, cwd=ROOT)
            subprocess.run([str(executable)], check=True, cwd=ROOT)

    def test_append_only_namespace_abi_is_fixed(self):
        sdk = read("userspace/sdk/include/x86os.h")
        pool = read("include/kernel/storage_request_pool.h")
        kernel = read("kernel/init/storage_request_pool.c")
        syscall = read("kernel/syscall/syscall_table.c")
        for token in (
            "X86OS_STORAGE_VFS_NAMESPACE 34U",
            "X86OS_VFS_SHADOW_FS_UNLINK 20U",
            "X86OS_VFS_SHADOW_FS_RENAME 21U",
            "x86os_vfs_namespace_frame_t",
        ):
            self.assertIn(token, sdk)
        self.assertIn("STORAGE_REQUEST_VFS_NAMESPACE = 34", pool)
        self.assertIn("STORAGE_REQUEST_VFS_NAMESPACE", kernel)
        self.assertIn("STORAGE_REQUEST_VFS_NAMESPACE", syscall)
        self.assertIn(
            "sizeof(x86os_vfs_namespace_frame_t) ==",
            read("userspace/storage/lib/vfs_namespace_client.c"),
        )

    def test_service_owns_ext2_namespace_mutation(self):
        service = read("userspace/programs/storage_service.c")
        ext2 = read("userspace/storage/lib/vfs_shadow_ext2.c")
        build = read("scripts/build_system_programs.py")
        for token in (
            "vfs_namespace_mutate",
            "vfs_namespace_path_valid",
            "vfs_namespace_reserved_zero",
            "reist_vfs_shadow_ext2_unlink_symlink",
            "reist_vfs_shadow_ext2_rename",
            "X86OS_STORAGE_VFS_NAMESPACE",
        ):
            self.assertIn(token, service)
        for token in (
            "ext2_plan_directory_remove",
            "ext2_plan_directory_rename",
            "ext2_release_allocation",
            "ext2_transaction_journal",
            "ext2_transaction_verify",
            "EXT2_JOURNAL_STATE_COMMITTED",
        ):
            self.assertIn(token, ext2)
        self.assertNotRegex(ext2, r"\b(malloc|calloc|realloc|free)\s*\(")
        self.assertIn('name == "STORAGE.PRG"', build)
        self.assertIn('"-fno-inline-functions"', build)
        self.assertIn('dependency_files.append(Path(__file__).resolve())',
                      build)

    def test_tools_fallback_only_for_explicit_unsupported_result(self):
        for path, legacy in (
            ("userspace/programs/del.c", "x86os_unlink"),
            ("userspace/programs/rm.c", "x86os_unlink"),
            ("userspace/programs/rename.c", "x86os_rename"),
        ):
            source = read(path)
            self.assertIn("status == -95", source)
            self.assertIn(legacy, source)
            self.assertIn("reist_vfs_", source)

    def test_runtime_expects_successful_final_component_mutations(self):
        runner = read("scripts/run_qemu_ext2_symlink.py")
        self.assertIn("renamed-link", runner)
        self.assertIn("readlink", runner)
        self.assertIn("del", runner)
        self.assertIn("svcctl restart 5", runner)
        self.assertIn("moved.txt", runner)
        self.assertIn("regular EXT2 inode changed", runner)


if __name__ == "__main__":
    unittest.main()
