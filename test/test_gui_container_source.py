import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "userspace/gui/include/reist/gui/container.h"
SOURCE = ROOT / "userspace/gui/lib/container.c"
HOST = ROOT / "test/test_gui_container_host.c"


class GuiContainerSourceTests(unittest.TestCase):
    def test_tree_contract_supports_nested_containers(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")
        self.assertIn("REIST_GUI_TREE_CAPACITY 32U", header)
        self.assertIn("REIST_GUI_NODE_CONTAINER", header)
        self.assertIn("parent_id", header)
        self.assertIn("reist_gui_tree_geometry", header)
        self.assertIn("reist_gui_tree_first_child", header)
        self.assertIn("reist_gui_tree_route", header)
        self.assertIn("capture", header.lower())
        self.assertIn("bubbling", header.lower())
        self.assertNotRegex(source, r"\b(malloc|calloc|realloc|free)\s*\(")
        self.assertNotIn("x86os", source)

    def test_host_behavior(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "gui-container-test.exe"
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
