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
        self.assertIn("chkdsk --fat12", source)
        self.assertIn("CHKDSK: FAT12 BPB and both FAT mirrors are clean",
                      source)
        self.assertIn('parser.add_argument("--chkdsk-only"', source)
        self.assertIn('"CHKDSK FAT12 PASS" if chkdsk_only', source)
        self.assertIn("default=150.0", source)

    def test_runner_proves_resident_admin_after_root_backend_loss(self):
        source = (ROOT / "scripts/run_qemu_admin_maintenance.py").read_text(
            encoding="utf-8"
        )
        self.assertIn('"blockdev-set-active"', source)
        self.assertIn('"active": False', source)
        self.assertIn("REIST_RESCUE CACHE_EXEC /DEVCTL.PRG", source)
        self.assertIn('"active": True', source)
        self.assertIn("reference image changed", source)

    def test_wrapper_accepts_only_explicit_runner_pass(self):
        source = (ROOT / "scripts/test-reist-runtime.ps1").read_text(
            encoding="utf-8")
        body = source[source.index("function Invoke-AdminMaintenance"):
                      source.index("function Invoke-ComponentControl")]
        self.assertIn("'FAIL'", body)
        self.assertIn("'ADMIN MAINTENANCE PASS'", body)
        self.assertIn("'CHKDSK FAT12 PASS'", body)
        self.assertIn("Invoke-AdminMaintenance -ChkdskOnly", source)


if __name__ == "__main__":
    unittest.main()
