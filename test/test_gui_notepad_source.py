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
            '#include "reist/gui/value_controls.h"',
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

    def test_surface_mode_paints_client_content_without_window_decorations(self):
        start = self.source.index("static void render_base_scene(")
        end = self.source.index("static void render_dynamic_scene(", start)
        base_scene = self.source[start:end]
        self.assertIn("render_editor_chrome(display, state)", base_scene)
        self.assertNotIn('"REIST Editor"', base_scene)
        self.assertNotIn("color_active", base_scene)
        self.assertIn(
            'reist_gui_surface_client_set_title(\n'
            '                &surface_client, "REIST Editor")',
            self.source,
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

    def test_editor_renders_utf8_on_scalar_boundaries(self):
        self.assertIn("reist_utf8_prefix", self.source)
        self.assertIn("reist_utf8_decode_one", self.source)
        self.assertIn("utf8_slice", self.source)
        self.assertIn("scalar_amount * display->font_width", self.source)
        self.assertNotIn("line + state->editor.first_column", self.source)

    def test_editor_has_bounded_horizontal_and_vertical_scrollbars(self):
        for contract in (
            "vertical_scroll_model",
            "horizontal_scroll_model",
            "reist_gui_text_editor_get_viewport",
            "reist_gui_text_editor_scroll_to",
            "reist_gui_range_configure",
            "reist_gui_range_set",
            "scrollbar_geometry",
            "NOTEPAD_SCROLLBAR_MIN_THUMB",
            "NOTEPAD_SCROLL_VERTICAL",
            "NOTEPAD_SCROLL_HORIZONTAL",
            "scroll_drag_offset",
        ):
            self.assertIn(contract, self.source)
        self.assertIn("maximum + page", self.source)
        self.assertIn("model->page_step", self.source)
        self.assertIn("synchronize_scrollbars(state)", self.source)

    def test_scroll_drag_uses_cached_viewport_and_coalesced_redraw(self):
        start = self.source.index("static uint32_t apply_scroll_value(")
        end = self.source.index("static uint32_t dispatch_one_scrollbar", start)
        apply_scroll = self.source[start:end]
        self.assertIn("state->viewport.first_line = state->editor.first_line",
                      apply_scroll)
        self.assertIn("state->viewport.first_column =",
                      apply_scroll)
        self.assertNotIn("synchronize_scrollbars(state)", apply_scroll)
        self.assertNotIn("reist_gui_text_editor_get_viewport", apply_scroll)

        start = self.source.index("static uint32_t dispatch_editor_pointer(")
        end = self.source.index("static uint32_t scroll_coordinate", start)
        pointer = self.source[start:end]
        self.assertIn("if (result.full_redraw &&", pointer)
        self.assertIn("synchronize_scrollbars(state)", pointer)

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

    def test_surface_menu_hover_repaints_only_the_retained_overlay(self):
        apply_start = self.source.index("static void apply_menu_result(")
        apply_end = self.source.index(
            "static uint32_t dispatch_editor_pointer", apply_start)
        apply_menu = self.source[apply_start:apply_end]
        self.assertIn("request_overlay_redraw(state)", apply_menu)
        self.assertNotIn("state->redraw = 1U", apply_menu)

        overlay_start = self.source.index("static void render_overlay(")
        overlay_end = self.source.index("static int write_all", overlay_start)
        overlay = self.source[overlay_start:overlay_end]
        self.assertIn("REIST_GUI_SURFACE_PAINT_LAYER_OVERLAY", overlay)
        self.assertIn("render_overlay_scene(display, state)", overlay)
        self.assertNotIn("render_base_scene(display, state)", overlay)
        self.assertIn(
            "application.redraw || application.dynamic_redraw", self.source)

    def test_scroll_and_hover_use_single_cpu_efficient_layers(self):
        self.assertIn("request_dynamic_redraw(state)", self.source)
        self.assertIn("REIST_GUI_SURFACE_PAINT_LAYER_DYNAMIC", self.source)
        self.assertIn("REIST_GUI_SURFACE_PAINT_LAYER_HOVER", self.source)
        self.assertIn("render_dynamic(&display, &application)", self.source)
        self.assertIn("render_hover(&display, &application)", self.source)
        hover = self.source[
            self.source.index("static void render_menu_hover("):
            self.source.index("static void render_scrollbar(")]
        self.assertIn("state->menu.hot_item", hover)
        self.assertNotIn("for (uint32_t index = 0U;", hover)
        self.assertIn('"notepad: Surface-Overlay verzoegert: "', self.source)

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
