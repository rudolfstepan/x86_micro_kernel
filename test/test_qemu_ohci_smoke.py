"""Regression contract for the OHCI QEMU smoke harness."""
import importlib.util
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def load_module():
    path = ROOT / "scripts/run_qemu_ohci_smoke.py"
    spec = importlib.util.spec_from_file_location("run_qemu_ohci_smoke", path)
    module = importlib.util.module_from_spec(spec)
    assert spec and spec.loader
    spec.loader.exec_module(module)
    return module


class OhciSmokeHarnessTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.module = load_module()

    def test_command_attaches_ohci_controller_keyboard_and_mouse(self):
        command = self.module.qemu_command(
            Path("qemu-system-i386"), Path("reist-os.img"), "256")
        self.assertIn("pci-ohci,id=ohci", command)
        self.assertIn("usb-kbd,bus=ohci.0", command)
        self.assertIn("usb-mouse,bus=ohci.0", command)
        self.assertIn("-serial", command)
        self.assertIn("stdio", command)

    def test_ready_and_failure_markers_are_defined(self):
        self.assertEqual(self.module.READY_MARKER, "OHCI keyboard ready")
        self.assertEqual(self.module.MOUSE_READY_MARKER, "OHCI mouse ready")
        self.assertIn("OHCI enumeration failed", self.module.FAILURE_MARKERS)
        self.assertIn("OHCI reset/start failed", self.module.FAILURE_MARKERS)


if __name__ == "__main__":
    unittest.main()
