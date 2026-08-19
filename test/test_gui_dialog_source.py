import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "userspace/gui/include/reist/gui/dialog.h"
TYPES = ROOT / "userspace/gui/include/reist/gui/types.h"
SOURCE = ROOT / "userspace/gui/lib/dialog.c"
EXAMPLE = ROOT / "userspace/gui/examples/dialog_controller.c"


class GuiDialogSourceTests(unittest.TestCase):
    def test_public_dialog_api_is_versioned_bounded_and_asynchronous(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")
        types = TYPES.read_text(encoding="utf-8")
        self.assertIn("REIST_GUI_DIALOG_API_VERSION 1U", header)
        self.assertIn("REIST_GUI_DIALOG_MAX_BUTTONS 4U", header)
        self.assertIn("REIST_GUI_DIALOG_DAMAGE_CAPACITY 4U", header)
        for token in (
            "REIST_GUI_DIALOG_MODELESS",
            "REIST_GUI_DIALOG_WINDOW_MODAL",
            "REIST_GUI_DIALOG_APPLICATION_MODAL",
            "default_response",
            "cancel_response",
            "owner_generation",
            "REIST_GUI_DIALOG_CAPTURE_MOVE",
            "reist_gui_dialog_open",
            "reist_gui_dialog_dispatch",
            "reist_gui_dialog_complete",
            "reist_gui_dialog_response",
        ):
            self.assertIn(token, header)
        self.assertIn("never creates a nested event loop", header)
        self.assertIn("@param[in,out] state", header)
        self.assertIn("extern \"C\"", header)
        self.assertIn("half-open", types)
        self.assertNotIn("x86os", header.lower())
        self.assertNotRegex(source, r"\b(malloc|calloc|realloc|free)\s*\(")

    def test_documented_example_uses_only_installed_dialog_header(self):
        example = EXAMPLE.read_text(encoding="utf-8")
        self.assertIn("#include <reist/gui/dialog.h>", example)
        self.assertIn("reist_gui_dialog_open", example)
        self.assertIn("reist_gui_dialog_dispatch", example)
        self.assertIn("reist_gui_dialog_response", example)
        self.assertNotIn("desktop_wm", example)
        self.assertNotIn("x86os", example.lower())

    def test_dialog_controller_host_behavior(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory(prefix="reist-gui-dialog-") as temp:
            executable = Path(temp) / "gui-dialog-test.exe"
            subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-Iuserspace/gui/include", "test/test_gui_dialog_host.c",
                 "userspace/gui/lib/dialog.c", "-o", str(executable)],
                cwd=ROOT, check=True, capture_output=True, text=True,
            )
            subprocess.run(
                [str(executable)], cwd=ROOT, check=True,
                capture_output=True, text=True, timeout=5,
            )


if __name__ == "__main__":
    unittest.main()
