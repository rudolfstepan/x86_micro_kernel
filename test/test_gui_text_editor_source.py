import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "userspace/gui/include/reist/gui/text_editor.h"
SOURCE = ROOT / "userspace/gui/lib/text_editor.c"
HOST = ROOT / "test/test_gui_text_editor_host.c"


class GuiTextEditorSourceTests(unittest.TestCase):
    def test_contract_is_bounded_renderer_independent_and_documented(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")
        self.assertIn("REIST_GUI_TEXT_EDITOR_MAX_LINES 200U", header)
        self.assertIn("REIST_GUI_TEXT_EDITOR_LINE_CAPACITY 256U", header)
        self.assertIn("reist_gui_text_editor_set_text", header)
        self.assertIn("reist_gui_text_editor_get_text", header)
        self.assertIn("reist_gui_text_editor_mark_saved", header)
        self.assertIn("reist_gui_text_editor_get_viewport", header)
        self.assertIn("reist_gui_text_editor_scroll_to", header)
        self.assertIn("reist_gui_text_editor_dispatch", header)
        self.assertIn("validated RFC 3629 UTF-8", header)
        self.assertIn("columns count Unicode scalars", header)
        self.assertIn("reist_utf8_decode_one", source)
        self.assertIn("line_byte_offset", source)
        self.assertIn("maximum_line_columns", source)
        self.assertNotRegex(source, r"\b(malloc|calloc|realloc|free)\s*\(")
        self.assertNotIn("x86os", source)
        self.assertNotIn("framebuffer", source)

    def test_sdk_build_includes_the_component(self):
        sdk = (ROOT / "scripts/build_user_sdk.py").read_text(encoding="utf-8")
        self.assertIn('"text_editor.c"', sdk)

    def test_host_behavior(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "gui-text-editor-test.exe"
            subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-Iuserspace/gui/include", "-Iinclude",
                 str(SOURCE), str(HOST),
                 "-o", str(executable)], cwd=ROOT, check=True,
                capture_output=True, text=True,
            )
            subprocess.run([str(executable)], cwd=ROOT, check=True,
                           capture_output=True, text=True)


if __name__ == "__main__":
    unittest.main()
