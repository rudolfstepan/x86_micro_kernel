import ast
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class QemuComponentControlContracts(unittest.TestCase):
    def test_runner_is_syntactically_valid_and_bounded(self):
        source = (ROOT / "scripts/run_qemu_component_control.py").read_text(
            encoding="utf-8"
        )
        ast.parse(source)
        self.assertIn("time.monotonic() + timeout", source)
        self.assertIn('nic="e1000"', source)
        self.assertIn("COMPONENT PROTECTED", source)
        self.assertIn("COMPONENT DEPENDENCY_BLOCKED", source)
        self.assertIn("COMPONENT DOWN_OK", source)
        self.assertIn("COMPONENT UP_OK", source)
        self.assertIn("COMPONENT RESTART_OK", source)
        self.assertIn('key = "spc"', source)

    def test_runner_checks_service_health_and_storage_diagnostic(self):
        source = (ROOT / "scripts/run_qemu_component_control.py").read_text(
            encoding="utf-8"
        )
        self.assertIn("storage-service state=READY", source)
        self.assertIn("STORAGE SERVICE_BIND_FAILED code=-13", source)
        self.assertIn("smoke.SHELL_PROMPT", source)


if __name__ == "__main__":
    unittest.main()
