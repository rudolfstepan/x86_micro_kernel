import shutil
import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "userspace/gui/apps/notepad/main.c"


class GuiNotepadSourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE.read_text(encoding="utf-8")

    def test_app_uses_only_public_gui_and_system_interfaces(self):
        for include in (
            '#include "x86os.h"',
            '#include "reist/gui/dialog.h"',
            '#include "reist/gui/file_dialog.h"',
            '#include "reist/gui/menu.h"',
            '#include "reist/gui/surface_client.h"',
            '#include "reist/gui/text_editor.h"',
        ):
            self.assertIn(include, self.source)
        self.assertNotIn("desktop_wm", self.source)
        self.assertNotRegex(
            self.source, r"\b(malloc|calloc|realloc|free)\s*\(")
        self.assertIn("this process owns a compositor Surface", self.source)

    def test_desktop_mode_uses_surface_paint_and_local_events(self):
        for contract in (
            "reist_gui_surface_endpoint_from_argv",
            "reist_gui_surface_client_create",
            "reist_gui_surface_client_ack_configure",
            "reist_gui_surface_client_set_title",
            "reist_gui_surface_client_paint_begin",
            "reist_gui_surface_client_paint_fill",
            "reist_gui_surface_client_paint_text",
            "reist_gui_surface_client_paint_commit",
            "REIST_GUI_SURFACE_INPUT_POINTER_MOTION",
            "REIST_GUI_SURFACE_INPUT_KEYBOARD",
        ):
            self.assertIn(contract, self.source)
        self.assertIn("if (!surface_mode && x86os_display_activate() == 0)",
                      self.source)
        self.assertIn("NOTEPAD_SURFACE_DOCUMENT_READY", self.source)
        self.assertIn("NOTEPAD_SURFACE_MENU_READY", self.source)
        self.assertIn("NOTEPAD_SURFACE_FILE_DIALOG_READY", self.source)
        self.assertIn("NOTEPAD_SURFACE_HOVER_READY", self.source)
        self.assertIn("NOTEPAD_PAINT_RETRY_LIMIT 3U", self.source)
        self.assertIn("paint_status_retryable", self.source)
        self.assertIn("status == -110 || status == -114", self.source)
        self.assertIn(
            "paint_surface != 0 ? color_face : color_desktop", self.source
        )

    def test_app_has_real_editing_persistence_and_dialog_flows(self):
        for contract in (
            "reist_gui_text_editor_dispatch",
            "reist_gui_text_editor_set_text",
            "reist_gui_text_editor_get_text",
            "reist_gui_text_editor_mark_saved",
            "x86os_fsync",
            "x86os_rename",
            "REIST_GUI_DIALOG_RESPONSE_SAVE",
            "REIST_GUI_DIALOG_RESPONSE_DISCARD",
            "REIST_GUI_DIALOG_RESPONSE_CANCEL",
            "NOTEPAD_ACTION_OPEN",
            "NOTEPAD_ACTION_SAVE_AS",
            "reist_gui_file_dialog_open",
            "reist_gui_file_dialog_dispatch",
            "reist_gui_menu_dispatch",
            "x86os_display_frame_begin",
            "x86os_display_frame_commit",
        ):
            self.assertIn(contract, self.source)
        self.assertIn("NOTEPAD_MOUSE_BATCH_LIMIT 32U", self.source)
        self.assertIn('"/untitled.txt"', self.source)

    def test_resize_is_recoverable_and_dialog_is_a_separate_surface(self):
        self.assertIn("accept_configure_bounded", self.source)
        self.assertIn('"notepad: Resize verzoegert: "', self.source)
        self.assertNotIn(
            "reist_gui_surface_client_accept_configure(\n"
            "                            &surface_client, &message) != 0) {\n"
            "                        application.exit_requested = 1U",
            self.source)
        self.assertIn("reist_gui_surface_client_create_dialog", self.source)
        self.assertIn("render_separate_dialog", self.source)
        self.assertIn("close_dialog_surface", self.source)
        self.assertIn("if (!dialog_surface_active) render_dialog", self.source)
        self.assertIn("NOTEPAD_SURFACE_DIALOG_READY", self.source)
        self.assertIn("NOTEPAD_SURFACE_RESIZE_OK", self.source)

    def test_source_is_valid_freestanding_c11(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        subprocess.run(
            [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
             "-fsyntax-only", "-Iuserspace/sdk/include",
             "-Iuserspace/gui/include", str(SOURCE)],
            cwd=ROOT, check=True, capture_output=True, text=True,
        )

    def test_builds_package_notepad_as_a_gui_program(self):
        programs = (ROOT / "scripts/build_system_programs.py").read_text(
            encoding="utf-8")
        windows = (ROOT / "scripts/build-windows.ps1").read_text(
            encoding="utf-8")
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        runtime = (ROOT / "scripts/run_qemu_system_layout.py").read_text(
            encoding="utf-8")
        self.assertIn('"NOTEPAD.PRG"', programs)
        self.assertIn('"NOTEPAD.PRG"', programs.split("GUI_PROGRAMS", 1)[1])
        self.assertEqual(windows.count("'usr/gui/bin/notepad.prg'"), 1)
        self.assertEqual(makefile.count("usr/gui/bin/notepad.prg="), 1)
        self.assertIn('(\"notepad --help\", \"Usage: notepad\")', runtime)


if __name__ == "__main__":
    unittest.main()
