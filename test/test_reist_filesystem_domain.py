import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class ReistFilesystemDomainTests(unittest.TestCase):
    def test_domain_is_idle_aware_read_only_and_non_restartable(self):
        source = (ROOT / "kernel/init/filesystem_safety.c").read_text(encoding="utf-8")
        self.assertIn('supervisor_register("filesystem-write"', source)
        self.assertIn(".restart_budget = 0", source)
        self.assertIn("supervisor_report_idle(filesystem_supervisor_handle)", source)
        self.assertIn("filesystem_read_only = true", source)

    def test_all_public_vfs_mutations_are_supervised(self):
        source = (ROOT / "fs/vfs/vfs.c").read_text(encoding="utf-8")
        for name in ("vfs_unmount", "vfs_write", "vfs_mkdir", "vfs_rmdir",
                     "vfs_create", "vfs_delete"):
            start = source.index(f"int {name}(")
            end = source.index("\n}", start)
            body = source[start:end]
            self.assertIn("vfs_mutation_begin()", body, name)
            self.assertIn("vfs_mutation_finish(armed, result)", body, name)
            self.assertIn("VFS_ERR_READ_ONLY", body, name)

    def test_io_failure_latches_read_only_mode(self):
        source = (ROOT / "fs/vfs/vfs.c").read_text(encoding="utf-8")
        start = source.index("static int vfs_mutation_finish")
        end = source.index("\n}", start)
        body = source[start:end]
        self.assertIn("result == VFS_ERR_IO", body)
        self.assertIn("filesystem_fence_mutations()", body)

    def test_fatal_path_registers_both_persistence_fences(self):
        source = (ROOT / "kernel/init/kernel.c").read_text(encoding="utf-8")
        self.assertIn("output_fence_register(filesystem_fence_mutations)", source)
        self.assertIn("output_fence_register(storage_fence_writes)", source)
        self.assertLess(source.index("output_fence_init()"),
                        source.index("output_fence_register(filesystem_fence_mutations)"))
        self.assertLess(source.index("storage_safety_init("),
                        source.index("auto_mount_all_drives()"))


if __name__ == "__main__":
    unittest.main()
