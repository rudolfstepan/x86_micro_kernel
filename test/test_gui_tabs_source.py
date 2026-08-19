import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "userspace/gui/include/reist/gui/tabs.h"
SOURCE = ROOT / "userspace/gui/lib/tabs.c"
HOST = ROOT / "test/test_gui_tabs_host.c"


class GuiTabsSourceTests(unittest.TestCase):
    def test_public_contract_is_bounded_and_renderer_independent(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")
        self.assertIn("REIST_GUI_TABS_CAPACITY 8U", header)
        self.assertIn("REIST_GUI_TABS_DAMAGE_CAPACITY 4U", header)
        self.assertIn("reist_gui_tabs_dispatch", header)
        self.assertIn("REIST_GUI_TABS_KEY_HOME", header)
        self.assertNotRegex(source, r"\b(malloc|calloc|realloc|free)\s*\(")
        self.assertNotIn("x86os", source)
        self.assertNotIn("framebuffer", source)

    def test_host_behavior(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "gui-tabs-test.exe"
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
