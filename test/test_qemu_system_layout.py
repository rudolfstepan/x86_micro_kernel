import ast
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class QemuSystemLayoutContracts(unittest.TestCase):
    def test_runner_is_bounded_and_checks_every_canonical_directory(self):
        source = (ROOT / "scripts/run_qemu_system_layout.py").read_text(
            encoding="utf-8"
        )
        ast.parse(source)
        self.assertIn("time.monotonic() + timeout", source)
        self.assertIn('nic="e1000"', source)
        for path in (
            "/bin", "/sbin", "/usr/bin", "/usr/gui/bin", "/libexec/reist"
        ):
            self.assertIn(path, source)
        self.assertIn('"/svcctl.prg status 5"', source)
        self.assertIn("STORAGE SERVICE_BIND_FAILED code=-13", source)
        self.assertIn('"/": "slash"', source)
        self.assertIn('".": "dot"', source)


if __name__ == "__main__":
    unittest.main()
