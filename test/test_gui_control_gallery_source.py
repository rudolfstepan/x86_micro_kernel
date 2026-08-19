import shutil
import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "userspace/gui/apps/control_gallery/main.c"


class GuiControlGallerySourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE.read_text(encoding="utf-8")

    def test_gallery_uses_only_public_gui_and_system_interfaces(self):
        self.assertIn('#include "x86os.h"', self.source)
        self.assertIn('#include "reist/gui/menu.h"', self.source)
        self.assertIn('#include "reist/gui/dialog.h"', self.source)
        self.assertNotIn("desktop_wm", self.source)
        self.assertNotRegex(
            self.source, r"\b(malloc|calloc|realloc|free)\s*\(")
        self.assertIn("temporary full-screen graphical client", self.source)

    def test_gallery_demonstrates_real_modalities_and_marks_planned_controls(self):
        self.assertIn("REIST_GUI_DIALOG_MODELESS", self.source)
        self.assertIn("REIST_GUI_DIALOG_APPLICATION_MODAL", self.source)
        self.assertIn("reist_gui_dialog_open", self.source)
        self.assertIn("reist_gui_dialog_dispatch", self.source)
        self.assertIn("reist_gui_menu_dispatch", self.source)
        self.assertIn("[x] Modal/modeless Dialog + Response", self.source)
        self.assertIn("[ ] Checkbox, Radio, Textfeld, TextArea", self.source)
        self.assertIn("Keine geplante Komponente wird als fertig", self.source)

    def test_gallery_lifecycle_and_input_work_are_bounded(self):
        self.assertIn("x86os_display_activate", self.source)
        self.assertIn("x86os_display_deactivate", self.source)
        self.assertIn("x86os_display_frame_begin", self.source)
        self.assertIn("x86os_display_frame_commit", self.source)
        self.assertIn('text_equal(argv[1], "--help")', self.source)
        self.assertIn('x86os_puts("Usage: guidemo\\n")', self.source)
        self.assertIn("mouse_count < 32U", self.source)
        self.assertIn("consumed < 4U", self.source)
        self.assertIn("x86os_sleep_ms(5U)", self.source)

    def test_gallery_is_packaged_as_userspace_shell_command(self):
        programs = (ROOT / "scripts/build_system_programs.py").read_text(
            encoding="utf-8")
        windows = (ROOT / "scripts/build-windows.ps1").read_text(
            encoding="utf-8")
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        shell = (ROOT / "userspace/bin/shell.c").read_text(encoding="utf-8")
        runtime_layout = (
            ROOT / "scripts/run_qemu_system_layout.py"
        ).read_text(encoding="utf-8")
        self.assertIn('"GUIDEMO.PRG"', programs)
        self.assertIn('GUI_PROGRAMS = {"DESKTOP.PRG", "GUIDEMO.PRG"}', programs)
        self.assertEqual(windows.count("'usr/gui/bin/guidemo.prg'"), 1)
        self.assertEqual(makefile.count("usr/gui/bin/guidemo.prg="), 1)
        self.assertIn('"/usr/gui/bin"', shell)
        self.assertIn('(\"guidemo --help\", \"Usage: guidemo\")', runtime_layout)

    def test_gallery_source_is_valid_freestanding_c11(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        subprocess.run(
            [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
             "-fsyntax-only", "-Iuserspace/sdk/include",
             "-Iuserspace/gui/include", str(SOURCE)],
            cwd=ROOT, check=True, capture_output=True, text=True,
        )


if __name__ == "__main__":
    unittest.main()
