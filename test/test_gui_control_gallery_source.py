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
        self.assertIn('#include "reist/gui/control.h"', self.source)
        self.assertIn('#include "reist/gui/container.h"', self.source)
        self.assertIn('#include "reist/gui/tabs.h"', self.source)
        self.assertIn('#include "reist/gui/value_controls.h"', self.source)
        self.assertIn('#include "reist/gui/surface_client.h"', self.source)
        self.assertNotIn("desktop_wm", self.source)
        self.assertNotRegex(
            self.source, r"\b(malloc|calloc|realloc|free)\s*\(")
        self.assertIn("exactly one Surface endpoint", self.source)

    def test_gallery_demonstrates_real_dialogs_and_basic_controls(self):
        self.assertIn("REIST_GUI_DIALOG_MODELESS", self.source)
        self.assertIn("REIST_GUI_DIALOG_APPLICATION_MODAL", self.source)
        self.assertIn("reist_gui_dialog_open", self.source)
        self.assertIn("reist_gui_dialog_dispatch", self.source)
        self.assertIn("reist_gui_menu_dispatch", self.source)
        self.assertIn("reist_gui_control_configure", self.source)
        self.assertIn("reist_gui_control_dispatch", self.source)
        self.assertIn("REIST_GUI_CONTROL_ROLE_PUSH_BUTTON", self.source)
        self.assertIn("REIST_GUI_CONTROL_ROLE_CHECKBOX", self.source)
        self.assertIn("REIST_GUI_CONTROL_ROLE_RADIO_BUTTON", self.source)
        self.assertIn("Keine geplante Komponente wird als fertig", self.source)

    def test_gallery_lifecycle_and_input_work_are_bounded(self):
        self.assertIn("reist_gui_surface_endpoint_from_argv", self.source)
        self.assertIn("reist_gui_surface_client_create", self.source)
        self.assertIn("reist_gui_surface_client_paint_begin", self.source)
        self.assertIn("reist_gui_surface_client_paint_commit", self.source)
        self.assertIn("reist_gui_surface_client_receive", self.source)
        self.assertIn("reist_gui_surface_client_destroy", self.source)
        self.assertNotIn("x86os_display_activate", self.source)
        self.assertNotIn("x86os_display_deactivate", self.source)
        self.assertNotIn("x86os_mouse_event", self.source)
        self.assertNotIn("x86os_getchar_nonblocking", self.source)
        self.assertNotIn("x86os_pointer_update", self.source)
        self.assertIn('text_equal(argv[1], "--help")', self.source)
        self.assertIn("GALLERY_SURFACE_EVENT_BATCH_LIMIT 32U", self.source)
        self.assertIn("GALLERY_SURFACE_CREATE_ATTEMPTS 250U", self.source)
        self.assertIn("x86os_sleep_ms(5U)", self.source)

    def test_centered_text_is_clipped_to_the_surface_before_paint(self):
        self.assertIn(
            "uint32_t available_width = display->width - (uint32_t)x;",
            self.source,
        )
        self.assertIn(
            "if (maximum_width > available_width) maximum_width = "
            "available_width;",
            self.source,
        )
        self.assertIn("GUIDEMO_PAINT_FAIL status=", self.source)

    def test_hover_is_a_small_retained_layer_and_paint_retries_are_bounded(self):
        self.assertIn("REIST_GUI_SURFACE_PAINT_LAYER_HOVER", self.source)
        self.assertIn("static void render_hover(", self.source)
        self.assertIn("state->hover_redraw = 1U", self.source)
        self.assertIn("GALLERY_PAINT_RETRY_LIMIT 3U", self.source)
        self.assertIn("startup_paint_failures", self.source)
        self.assertIn("paint_status_retryable", self.source)
        self.assertIn("GUIDEMO_MENU_INTERACTION_OK", self.source)

    def test_gallery_uses_server_window_decorations_and_real_pointer_input(self):
        render_start = self.source.index("static void render_gallery(")
        render_end = self.source.index("static void render_dialog(",
                                       render_start)
        renderer = self.source[render_start:render_end]
        self.assertIn("gallery_client_area", self.source)
        self.assertNotIn("gallery_frame", self.source)
        self.assertNotIn('"REIST GUI Control Gallery"', renderer)
        self.assertIn("GUIDEMO_INTERACTION_OK", self.source)
        self.assertIn('text_equal(argv[2], "--interaction-probe")',
                      self.source)

    def test_basic_control_layout_has_one_label_and_outline_only_focus(self):
        self.assertNotIn("Implementierte Basis-Controls", self.source)
        self.assertIn("static void outline(", self.source)
        self.assertIn("marker.width + 8U + focus_text_width + 4U", self.source)
        self.assertNotIn("bevel(focus, color_dark", self.source)

    def test_gallery_groups_labelled_controls_on_real_tab_pages(self):
        for label in ("Basis", "Eingabe", "Auswahl", "Werte",
                      "Einzeiliges Textfeld", "Liste: Theme-Auswahl",
                      "Slider: Lautstaerke", "SpinBox: Anzahl",
                      "Fortschrittsanzeige"):
            self.assertIn(label, self.source)
        self.assertIn("reist_gui_tabs_dispatch", self.source)
        self.assertIn("reist_gui_tree_validate", self.source)
        self.assertIn("reist_gui_text_dispatch", self.source)
        self.assertIn("reist_gui_list_dispatch", self.source)
        self.assertIn("reist_gui_range_dispatch", self.source)
        self.assertIn("GALLERY_NODE_INPUT_GROUP", self.source)
        self.assertIn("GALLERY_NODE_VALUES_GROUP", self.source)

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
        gui_programs = programs.split("GUI_PROGRAMS", 1)[1]
        for program in ("DESKTOP.PRG", "GUIDEMO.PRG", "NOTEPAD.PRG",
                        "SOUNDPLAYER.PRG", "IMAGEVIEWER.PRG"):
            self.assertIn(f'"{program}"', gui_programs)
        self.assertEqual(windows.count("'usr/gui/bin/guidemo.prg'"), 1)
        self.assertEqual(makefile.count("usr/gui/bin/guidemo.prg="), 1)
        self.assertIn('"/usr/gui/bin"', shell)
        self.assertIn(
            '(\"guidemo --help\", '
            '\"Usage: guidemo --reist-surface=<handle>\")', runtime_layout)

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
