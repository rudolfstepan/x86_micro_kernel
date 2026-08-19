import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "userspace/gui/include/reist/gui/control.h"
SOURCE = ROOT / "userspace/gui/lib/control.c"
EXAMPLE = ROOT / "userspace/gui/examples/basic_controls.c"


class GuiControlSourceTests(unittest.TestCase):
    def test_public_api_is_versioned_bounded_and_renderer_independent(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")
        self.assertIn("REIST_GUI_CONTROL_API_VERSION 1U", header)
        self.assertIn("REIST_GUI_CONTROL_CAPACITY 16U", header)
        self.assertIn("REIST_GUI_CONTROL_DAMAGE_CAPACITY 8U", header)
        for token in (
            "REIST_GUI_CONTROL_ROLE_LABEL",
            "REIST_GUI_CONTROL_ROLE_PUSH_BUTTON",
            "REIST_GUI_CONTROL_ROLE_CHECKBOX",
            "REIST_GUI_CONTROL_ROLE_RADIO_BUTTON",
            "REIST_GUI_CONTROL_MIXED",
            "REIST_GUI_CONTROL_FOCUS_KEYBOARD",
            "reist_gui_control_configure",
            "reist_gui_control_dispatch",
            "reist_gui_control_set_check",
        ):
            self.assertIn(token, header)
        self.assertIn("implicit grab", header)
        self.assertIn("caller-owned surface", header)
        self.assertIn('extern "C"', header)
        self.assertNotIn("x86os", header.lower())
        self.assertNotRegex(source, r"\b(malloc|calloc|realloc|free)\s*\(")

    def test_installed_header_example_uses_only_public_control_api(self):
        example = EXAMPLE.read_text(encoding="utf-8")
        self.assertIn("#include <reist/gui/control.h>", example)
        self.assertIn("reist_gui_control_configure", example)
        self.assertIn("reist_gui_control_dispatch", example)
        self.assertNotIn("desktop_wm", example)
        self.assertNotIn("x86os", example.lower())

    def test_control_state_machine_host_behavior(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory(prefix="reist-gui-control-") as temp:
            executable = Path(temp) / "gui-control-test.exe"
            subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-Iuserspace/gui/include", "test/test_gui_control_host.c",
                 "userspace/gui/lib/control.c", "-o", str(executable)],
                cwd=ROOT, check=True, capture_output=True, text=True,
            )
            subprocess.run(
                [str(executable)], cwd=ROOT, check=True,
                capture_output=True, text=True, timeout=5,
            )


if __name__ == "__main__":
    unittest.main()
