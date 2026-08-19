import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "userspace/gui/include/reist/gui/value_controls.h"
SOURCE = ROOT / "userspace/gui/lib/value_controls.c"
HOST = ROOT / "test/test_gui_value_controls_host.c"


class GuiValueControlsSourceTests(unittest.TestCase):
    def test_contract_is_fixed_capacity_and_semantic(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")
        self.assertIn("REIST_GUI_TEXT_CAPACITY 64U", header)
        self.assertIn("REIST_GUI_LIST_CAPACITY 32U", header)
        for symbol in ("reist_gui_text_dispatch", "reist_gui_list_dispatch",
                       "reist_gui_range_dispatch", "REIST_GUI_RANGE_PROGRESS"):
            self.assertIn(symbol, header)
        self.assertNotRegex(source, r"\b(malloc|calloc|realloc|free)\s*\(")
        self.assertNotIn("x86os", source)
        self.assertNotIn("framebuffer", source)

    def test_host_behavior(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "gui-value-controls-test.exe"
            subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-Iuserspace/gui/include", str(SOURCE), str(HOST),
                 "-o", str(executable)], cwd=ROOT, check=True,
                capture_output=True, text=True,
            )
            subprocess.run([str(executable)], cwd=ROOT, check=True,
                           capture_output=True, text=True)


if __name__ == "__main__":
    unittest.main()
