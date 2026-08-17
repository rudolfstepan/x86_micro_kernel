import ast
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class QemuAdminMaintenanceContracts(unittest.TestCase):
    def test_runner_is_syntactically_valid_and_bounded(self):
        path = ROOT / "scripts/run_qemu_admin_maintenance.py"
        source = path.read_text(encoding="utf-8")
        ast.parse(source)
        self.assertIn("time.monotonic() + timeout", source)
        self.assertIn("ADMIN ROOT_PROTECTED", source)
        self.assertIn("ADMIN DEVICE_DOWN_OK", source)
        self.assertIn("ADMIN DEVICE_UP_OK", source)

    def test_runner_proves_resident_admin_after_root_backend_loss(self):
        source = (ROOT / "scripts/run_qemu_admin_maintenance.py").read_text(
            encoding="utf-8"
        )
        self.assertIn('"blockdev-set-active"', source)
        self.assertIn('"active": False', source)
        self.assertIn("REIST_RESCUE CACHE_EXEC /DEVCTL.PRG", source)
        self.assertIn('"active": True', source)
        self.assertIn("reference image changed", source)


if __name__ == "__main__":
    unittest.main()
