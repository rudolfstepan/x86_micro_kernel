import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class VfsMaintenanceContracts(unittest.TestCase):
    def read(self, path):
        return (ROOT / path).read_text(encoding="utf-8")

    def test_acquire_checks_open_nodes_before_blocking_opens(self):
        header = self.read("fs/vfs/vfs.h")
        source = self.read("fs/vfs/vfs.c")
        self.assertIn("uint32_t open_nodes", header)
        self.assertIn("maintenance_blocked", header)
        acquire = source[source.index("static int vfs_maintenance_acquire_locked"):
                         source.index("int vfs_maintenance_acquire(")]
        self.assertIn("open_nodes != 0U", acquire)
        self.assertIn("maintenance_blocked = true", acquire)
        open_body = source[source.index("static int vfs_open_locked"):
                            source.index("static int vfs_close_locked")]
        self.assertIn("if (fs->maintenance_blocked)", open_body)

    def test_release_is_idempotent(self):
        source = self.read("fs/vfs/vfs.c")
        release = source[source.index("static int vfs_maintenance_release_locked"):
                          source.index("int vfs_maintenance_acquire(")]
        self.assertIn("if (!mount->fs->maintenance_blocked) return VFS_OK", release)


if __name__ == "__main__":
    unittest.main()
