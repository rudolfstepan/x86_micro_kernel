import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "userspace/gui/include/reist/gui/file_dialog.h"
SOURCE = ROOT / "userspace/gui/lib/file_dialog.c"


class GuiFileDialogSourceTests(unittest.TestCase):
    def test_api_is_versioned_bounded_async_and_vfs_independent(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")
        self.assertIn("REIST_GUI_FILE_DIALOG_API_VERSION 1U", header)
        self.assertIn("REIST_GUI_FILE_DIALOG_PATH_CAPACITY 256U", header)
        self.assertIn("REIST_GUI_FILE_DIALOG_OPEN", header)
        self.assertIn("REIST_GUI_FILE_DIALOG_SAVE", header)
        self.assertIn("reist_gui_file_dialog_open", header)
        self.assertIn("reist_gui_file_dialog_dispatch", header)
        self.assertNotIn("x86os", header.lower())
        self.assertNotRegex(source, r"\b(malloc|calloc|realloc|free)\s*\(")

    def test_sdk_build_includes_file_dialog(self):
        sdk = (ROOT / "scripts/build_user_sdk.py").read_text(encoding="utf-8")
        self.assertIn('"file_dialog.c"', sdk)

    def test_host_behavior(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory(prefix="reist-file-dialog-") as temp:
            executable = Path(temp) / "file-dialog-test.exe"
            subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-Iuserspace/gui/include", "test/test_gui_file_dialog_host.c",
                 "userspace/gui/lib/file_dialog.c", "-o", str(executable)],
                cwd=ROOT, check=True, capture_output=True, text=True,
            )
            subprocess.run([str(executable)], cwd=ROOT, check=True,
                           capture_output=True, text=True, timeout=5)


if __name__ == "__main__":
    unittest.main()
