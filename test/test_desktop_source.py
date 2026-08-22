import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DESKTOP = ROOT / "userspace" / "gui" / "compositor" / "desktop.c"
WM_SOURCE = ROOT / "userspace" / "gui" / "compositor" / "desktop_wm.c"
WM_HEADER = ROOT / "userspace" / "gui" / "compositor" / "desktop_wm.h"


class DesktopSourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = DESKTOP.read_text(encoding="utf-8")

    def test_window_manager_model_host_behavior(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory(prefix="reist-desktop-wm-") as temp:
            executable = Path(temp) / "desktop-wm-test.exe"
            subprocess.run(
                 [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-I.", "test/test_desktop_wm_host.c",
                 "userspace/gui/compositor/desktop_wm.c", "-o", str(executable)],
                cwd=ROOT, check=True, capture_output=True, text=True)
            subprocess.run([str(executable)], cwd=ROOT, check=True,
                           capture_output=True, text=True, timeout=5)

    def test_desktop_is_part_of_the_system_program_image(self):
        programs = (ROOT / "scripts" / "build_system_programs.py").read_text(
            encoding="utf-8"
        )
        self.assertIn('"DESKTOP.PRG": (', programs)
        self.assertIn('ROOT / "userspace/gui/compositor/desktop.c"', programs)
        self.assertIn('ROOT / "userspace/gui/compositor/desktop_wm.c"', programs)
        self.assertIn(
            'ROOT / "userspace/gui/compositor/desktop_explorer.c"', programs
        )
        self.assertIn(
            'ROOT / "userspace/gui/compositor/desktop_surface.c"', programs
        )
        self.assertIn('#include "desktop_surface.h"', self.source)
        self.assertIn("desktop_surface_initialize(&surfaces)", self.source)

    def test_launcher_requires_the_pixel_display_abi(self):
        self.assertIn("x86os_display_info(&display)", self.source)
        self.assertIn("X86OS_DISPLAY_ABI_VERSION", self.source)
        self.assertIn("x86os_fill_rect", self.source)
        self.assertIn("x86os_draw_text_pixels", self.source)
        self.assertRegex(self.source, r"display\.width\s*<\s*320U")
        self.assertRegex(self.source, r"display\.height\s*<\s*240U")

    def test_root_explorer_replaces_static_launcher_windows(self):
        self.assertIn('#include "desktop_explorer.h"', self.source)
        self.assertIn('{"Computer", "/",', self.source)
        self.assertNotIn("#define APP_COUNT", self.source)
        self.assertNotIn("static const desktop_app_t", self.source)
        self.assertIn("desktop_explorer_open", self.source)
        self.assertIn("desktop_explorer_child_path", self.source)
        self.assertIn("desktop_icon_rect", self.source)
        self.assertIn("render_window", self.source)

    def test_icon_focus_is_compact_and_does_not_fill_the_hit_cell(self):
        render = self.source[self.source.index("static void render_icon") :]
        render = render[: render.index("\n}") + 2]
        self.assertIn("desktop_rect_t focus", render)
        self.assertIn("draw_bevel(context, focus", render)
        self.assertNotIn("draw_bevel(context, rect", render)
        self.assertIn("large cell remains the predictable", render)
        self.assertIn("3U + symbol.height + 3U", render)
        self.assertNotIn("rect.height - display->font_height - 3U", render)

    def test_desktop_and_explorer_icon_content_is_centered(self):
        self.assertIn("static int32_t centered_text_x", self.source)
        desktop_icon = self.source[
            self.source.index("static void render_icon") :
            self.source.index("static void render_resize_grip")
        ]
        explorer_icon = self.source[
            self.source.index("static void render_explorer_entry") :
            self.source.index("static void render_window")
        ]
        self.assertIn("(rect.width - icon_size) / 2U", desktop_icon)
        self.assertIn("centered_text_x(", desktop_icon)
        self.assertIn("(cell.width - symbol_width) / 2U", explorer_icon)
        self.assertIn("centered_text_x(display, cell", explorer_icon)

    def test_window_manager_has_fixed_z_order_focus_and_capture(self):
        header = WM_HEADER.read_text(encoding="utf-8")
        model = WM_SOURCE.read_text(encoding="utf-8")
        self.assertIn("#define DESKTOP_WM_CAPACITY 8U", header)
        self.assertIn("windows[DESKTOP_WM_CAPACITY]", header)
        self.assertIn("z_order[DESKTOP_WM_CAPACITY]", header)
        self.assertIn("DESKTOP_WM_CAPTURE_MOVE", header)
        self.assertIn("DESKTOP_WM_CAPTURE_CLOSE", header)
        self.assertIn("DESKTOP_WM_CAPTURE_RESIZE", header)
        for edge in ("LEFT", "RIGHT", "TOP", "BOTTOM"):
            self.assertIn(f"DESKTOP_WM_RESIZE_{edge}", header)
        self.assertIn("minimum_width", header)
        self.assertIn("minimum_height", header)
        self.assertIn("desktop_wm_window_at", model)
        self.assertIn("desktop_wm_resize_edges_at", model)
        self.assertIn("static uint32_t resize_window", model)
        self.assertIn("desktop_wm_pointer_press", model)
        self.assertIn("desktop_wm_pointer_motion", model)
        self.assertIn("desktop_wm_pointer_release", model)
        self.assertNotRegex(model, r"\b(malloc|free)\s*\(")

    def test_input_uses_typed_dispatch_with_separate_focus(self):
        header = WM_HEADER.read_text(encoding="utf-8")
        self.assertIn("desktop_wm_event_t", header)
        self.assertIn("keyboard_focus", header)
        self.assertIn("pointer_focus", header)
        self.assertIn("desktop_wm_dispatch", self.source)
        main = self.source[self.source.index("int main(") :]
        self.assertNotIn("desktop_wm_pointer_press(&manager", main)
        self.assertNotIn("desktop_wm_pointer_motion(&manager", main)
        self.assertNotIn("desktop_wm_pointer_release(&manager", main)

    def test_system_bar_uses_the_public_menu_api_and_typed_window_actions(self):
        programs = (ROOT / "scripts" / "build_system_programs.py").read_text(
            encoding="utf-8"
        )
        self.assertIn('#include "reist/gui/menu.h"', self.source)
        self.assertIn("static const reist_gui_menu_model_t", self.source)
        self.assertIn("reist_gui_menu_dispatch", self.source)
        self.assertIn("render_menu_bar", self.source)
        self.assertIn("render_menu_popup", self.source)
        self.assertIn("render_system_dialog", self.source)
        self.assertIn("DESKTOP_MENU_ACTION_HELP", self.source)
        self.assertIn("DESKTOP_MENU_ACTION_ABOUT", self.source)
        self.assertIn("DESKTOP_WM_EVENT_OPEN", self.source)
        self.assertIn("DESKTOP_WM_EVENT_CLOSE", self.source)
        gui_programs = programs.split("GUI_PROGRAMS", 1)[1]
        for program in ("DESKTOP.PRG", "GUIDEMO.PRG", "NOTEPAD.PRG",
                        "SOUNDPLAYER.PRG", "IMAGEVIEWER.PRG"):
            self.assertIn(f'"{program}"', gui_programs)
        self.assertIn("gui_library", programs)

    def test_active_menu_or_dialog_receives_input_before_the_window_manager(self):
        motion = self.source[
            self.source.index("static uint32_t dispatch_pointer_motion") :
            self.source.index("static uint32_t dispatch_pointer_button")
        ]
        button = self.source[
            self.source.index("static uint32_t dispatch_pointer_button") :
            self.source.index("static void render_probe_error")
        ]
        self.assertLess(
            motion.index("desktop_ui_pointer_event("),
            motion.index("dispatch_desktop_event("),
        )
        self.assertIn("if (!ui_motion_consumed)", motion)
        self.assertIn("if (!ui_press_consumed)", button)
        self.assertIn("if (!ui_release_consumed)", button)
        self.assertIn("if (!ui_key.consumed)", self.source)

    def test_desktop_dialogs_use_the_public_async_controller(self):
        self.assertIn('#include "reist/gui/dialog.h"', self.source)
        self.assertIn("REIST_GUI_DIALOG_MODELESS", self.source)
        self.assertIn("REIST_GUI_DIALOG_APPLICATION_MODAL", self.source)
        self.assertIn("reist_gui_dialog_open", self.source)
        self.assertIn("reist_gui_dialog_dispatch", self.source)
        self.assertIn("reist_gui_dialog_response", self.source)
        self.assertGreaterEqual(self.source.count("reist_gui_dialog_validate"), 2)
        self.assertIn("REIST_GUI_DIALOG_CAPTURE_MOVE", self.source)
        self.assertNotIn("DESKTOP_DIALOG_CAPTURE_", self.source)

    def test_redraw_is_driven_by_fixed_dirty_regions_and_clips_primitives(self):
        header = WM_HEADER.read_text(encoding="utf-8")
        self.assertIn("#define DESKTOP_WM_DIRTY_CAPACITY 8U", header)
        self.assertIn("desktop_dirty_region_t", header)
        self.assertIn("static void render_dirty_regions", self.source)
        self.assertIn("fill_rect_clipped", self.source)
        self.assertIn("draw_text_clipped", self.source)

    def test_menu_state_changes_use_an_atomic_frame_without_partial_glyphs(self):
        collect = self.source[self.source.index("static void collect_menu_damage") :]
        collect = collect[: collect.index("\n}") + 2]
        self.assertIn("menu_result->damage_count != 0U", collect)
        self.assertIn("desktop_dirty_full(dirty)", collect)
        self.assertIn("explicit glyph clip rectangle", collect)

    def test_ansi_arrow_keys_change_explorer_selection(self):
        self.assertIn("desktop_explorer_keyboard", self.source)
        for suffix in ("UP", "DOWN", "LEFT", "RIGHT"):
            self.assertIn(f"DESKTOP_KEY_{suffix}", self.source)
        for ansi in ("'A'", "'B'", "'C'", "'D'"):
            self.assertIn(ansi, self.source)

    def test_only_a_bare_escape_leaves_the_desktop(self):
        decoder = self.source[self.source.index("static int read_key") :]
        decoder = decoder[: decoder.index("\n}") + 2]
        self.assertIn("if (prefix == 0) return DESKTOP_KEY_ESCAPE;", decoder)
        self.assertIn("if (prefix != '[') return DESKTOP_KEY_NONE;", decoder)
        self.assertIn("value < 0x40 || value > 0x7E", decoder)
        self.assertGreaterEqual(decoder.count("return DESKTOP_KEY_NONE;"), 3)
        self.assertEqual(decoder.count("return DESKTOP_KEY_ESCAPE;"), 1)

    def test_program_activation_spawns_and_waits_for_the_selected_child(self):
        launch = self.source[self.source.index("static int launch_program") :]
        launch = launch[: launch.index("\n}") + 2]
        self.assertIn("x86os_spawn(launch_program_path)", launch)
        self.assertIn("x86os_wait(pid, &status)", launch)
        self.assertNotIn("x86os_puts", launch)
        self.assertNotIn("x86os_clear", launch)

    def test_wait_failure_terminates_and_reaps_child_before_input_returns(self):
        launch = self.source[self.source.index("static int launch_program") :]
        wait_check = launch.index("if (wait_result != pid)")
        kill = launch.index("x86os_kill(pid)", wait_check)
        reap = launch.index("x86os_wait(pid, &status)", kill)
        self.assertLess(wait_check, kill)
        self.assertLess(kill, reap)

    def test_double_click_opens_folders_or_programs(self):
        self.assertIn("result->activated", self.source)
        self.assertIn("X86OS_DIRECTORY", self.source)
        self.assertIn("has_program_extension", self.source)
        self.assertIn("open_explorer_path", self.source)
        self.assertIn(
            "launch_program(surface_runtime, program, document)", self.source)

    def test_child_returns_directly_to_graphical_desktop(self):
        launch = self.source[self.source.index("static int launch_program") :]
        launch = launch[: launch.index("\n}") + 2]
        self.assertNotIn("render_desktop", launch)
        self.assertNotIn("Taste zum Desktop", launch)
        self.assertNotIn("x86os_getchar", launch)
        activation = self.source[
            self.source.index("static uint32_t apply_desktop_activation") :
            self.source.index("static uint32_t apply_desktop_ui_result")
        ]
        self.assertIn("desktop_dirty_full(dirty)", activation)

    def test_spawn_failure_returns_status_for_the_modal_error(self):
        launch = self.source[self.source.index("static int launch_program") :]
        self.assertIn("return pid;", launch)
        self.assertNotIn("print_integer", launch)

    def test_explorer_errors_use_an_application_modal_dialog(self):
        self.assertIn("DESKTOP_DIALOG_ERROR", self.source)
        self.assertIn("static void desktop_ui_open_error", self.source)
        self.assertIn('"Keine Dateizuordnung vorhanden."', self.source)
        self.assertIn('"Programmargumente konnten nicht uebergeben werden."',
                      self.source)
        self.assertIn('"Kein freier Scheduler-Task verfuegbar."', self.source)
        self.assertIn('"Keine freie IPC-Ressource verfuegbar."', self.source)
        self.assertIn('"Surface-Endpunkt ist beim Besitzer ungueltig (-9)."',
                      self.source)
        self.assertIn('"Surface-Delegation wurde verweigert (-13)."',
                      self.source)
        self.assertIn('"Programmdatei nicht gefunden oder ungueltig."',
                      self.source)
        self.assertIn('"Ordner kann nicht geoeffnet werden."', self.source)
        self.assertIn('"Programm konnte nicht gestartet werden."', self.source)
        activation = self.source[
            self.source.index("static uint32_t apply_desktop_activation") :
            self.source.index("static uint32_t apply_desktop_ui_result")
        ]
        self.assertIn("desktop_ui_open_error(", activation)
        self.assertNotIn('x86os_puts("desktop: Keine Dateizuordnung', activation)

    def test_first_render_emits_the_desktop_ready_marker(self):
        main = self.source[self.source.index("int main(") :]
        first_render = main.index("render_desktop_measured(")
        marker = main.index('x86os_puts("DESKTOP_OK\\n")')
        self.assertLess(marker, first_render)
        startup = main[:main.index("for (;;)")]
        self.assertEqual(startup.count("render_desktop_measured("), 1)

    def test_scene_redraw_uses_a_bounded_frame_transaction(self):
        redraw = self.source[
            self.source.index("static uint32_t render_desktop_frame") :
        ]
        redraw = redraw[: redraw.index("\n}") + 2]
        self.assertIn("x86os_display_frame_begin", redraw)
        self.assertIn("x86os_display_frame_commit", redraw)
        self.assertIn("x86os_display_frame_cancel", redraw)
        self.assertLess(redraw.index("x86os_display_frame_begin"),
                        redraw.index("render_desktop("))
        self.assertLess(redraw.index("render_desktop("),
                        redraw.index("x86os_display_frame_commit"))

    def test_resize_has_a_visible_grip_and_uses_dirty_redraw(self):
        grip = self.source[self.source.index("static void render_resize_grip") :]
        grip = grip[: grip.index("\n}") + 2]
        self.assertIn("fill_rect_clipped", grip)
        self.assertIn("render_resize_grip(context, window)", self.source)
        self.assertIn("DESKTOP_WM_CAPTURE_RESIZE", self.source)
        self.assertIn("resize_render = 1U", self.source)
        self.assertIn(
            "&display, &manager, &explorer, &surfaces, &ui, &dirty,",
            self.source,
        )

    def test_surface_program_is_async_and_owned_by_the_compositor(self):
        self.assertIn('"/usr/gui/bin/surfacedemo.prg"', self.source)
        self.assertIn('"/usr/gui/bin/notepad.prg"', self.source)
        self.assertIn('"/usr/gui/bin/imageviewer.prg"', self.source)
        self.assertIn("launch_surface_probe_client", self.source)
        self.assertIn('"/USR/GUI/BIN/NOTEPAD.PRG", "/README.TXT", 1U',
                      self.source)
        self.assertIn("program_uses_surface", self.source)
        self.assertIn("path_equal_ascii_case", self.source)
        surface_classifier = self.source[
            self.source.index("static uint32_t program_uses_surface") :
            self.source.index("static int launch_program")
        ]
        self.assertNotIn("text_equal(program", surface_classifier)
        self.assertEqual(surface_classifier.count("path_equal_ascii_case"), 4)
        self.assertIn("desktop_surface_runtime_reserve", self.source)
        self.assertIn("desktop_surface_runtime_bind", self.source)
        self.assertIn("sync_surface_windows", self.source)
        self.assertIn("desktop_surface_runtime_send_close", self.source)

    def test_retained_surface_paint_is_clipped_to_current_client(self):
        render_window = self.source[
            self.source.index("static void render_window") :
            self.source.index("static void render_menu_bar")
        ]
        self.assertIn(
            "intersect_rects(client, context->clip, &surface_clip)",
            render_window,
        )
        self.assertIn("surface_context.clip = surface_clip", render_window)
        self.assertIn(
            "fill_rect_clipped(\n                    &surface_context",
            render_window,
        )
        self.assertIn(
            "&surface_context, bounds.x, bounds.y", render_window
        )

    def test_render_probe_is_fixed_bounded_and_reports_versioned_metrics(self):
        self.assertIn("#define DESKTOP_METRICS_VERSION 1U", self.source)
        self.assertIn("#define DESKTOP_RENDER_PROBE_STEPS 8U", self.source)
        self.assertIn("#define DESKTOP_ARGUMENT_LIMIT 32U", self.source)
        self.assertIn('text_equal(argv[1], "--render-probe")', self.source)
        probe = self.source[self.source.index("static void run_render_probe") :]
        probe = probe[: probe.index("\n}\n\nint main") + 2]
        self.assertIn("DESKTOP_WM_CAPTURE_MOVE", probe)
        self.assertIn("DESKTOP_WM_CAPTURE_RESIZE", probe)
        self.assertEqual(probe.count("< DESKTOP_RENDER_PROBE_STEPS"), 2)
        self.assertIn("render_desktop_measured", probe)
        self.assertIn("desktop_move_capture_geometry", probe)
        self.assertIn("desktop_move_cache_capture", probe)
        self.assertIn("move_cache.valid ? &move_cache : 0", probe)
        measured = self.source[
            self.source.index("static void render_desktop_measured") :
            self.source.index("static int read_escape_byte")
        ]
        self.assertGreaterEqual(measured.count("x86os_monotonic_ms"), 2)
        self.assertNotIn("x86os_puts", measured)
        metrics = self.source[
            self.source.index("static void print_render_metrics") :
            self.source.index("static uint32_t desktop_try_exit")
        ]
        for field in (
            "version", "full_frames", "dirty_frames", "drag_frames",
            "resize_frames", "fallback_frames", "damage_regions",
            "clock_errors", "probe_errors",
        ):
            self.assertIn(f'print_metric("{field}"', metrics)

    def test_launcher_is_freestanding_and_has_no_host_libc_dependency(self):
        self.assertNotRegex(self.source, r"#include\s*<(stdio|stdlib|string)\.h>")
        self.assertNotRegex(self.source, r"\b(printf|strlen|memcpy|malloc|free)\s*\(")

    def test_file_associations_are_bounded_and_pass_the_document_as_argv(self):
        self.assertIn('#include "desktop_filetypes.h"', self.source)
        self.assertIn('x86os_open("/etc/reist/filetypes.conf")', self.source)
        self.assertIn("DESKTOP_FILETYPES_CONFIG_CAPACITY", self.source)
        self.assertIn("desktop_filetypes_parse", self.source)
        self.assertIn("desktop_filetypes_lookup", self.source)
        self.assertIn("static char launch_program_path", self.source)
        self.assertIn("static char launch_document_path", self.source)
        self.assertIn("static const char *launch_arguments[3]", self.source)
        self.assertIn("copy_launch_text", self.source)
        self.assertIn(
            "x86os_spawnv(launch_program_path, 2, launch_arguments)",
            self.source,
        )
        self.assertIn("argument_count = 3", self.source)
        self.assertIn('"Keine Dateizuordnung vorhanden."', self.source)

    def test_usb_mouse_moves_a_clipped_visible_pointer(self):
        self.assertIn("x86os_mouse_event(&mouse)", self.source)
        self.assertIn("static void move_pointer", self.source)
        self.assertIn("(int64_t)*pointer_x + delta_x", self.source)
        self.assertIn("(int64_t)*pointer_y + delta_y", self.source)
        self.assertIn(
            "accumulate_mouse_delta(&pending_delta_x, mouse.delta_x)",
            self.source,
        )
        self.assertIn(
            "accumulate_mouse_delta(&pending_delta_y, mouse.delta_y)",
            self.source,
        )
        self.assertIn(
            "x86os_pointer_update(pointer_x, pointer_y, 1U)", self.source
        )
        self.assertNotIn("draw_mouse_pointer", self.source)
        self.assertIn("X86OS_MOUSE_BUTTON_LEFT", self.source)
        self.assertIn("DESKTOP_WM_EVENT_POINTER_MOTION", self.source)
        self.assertIn("DESKTOP_WM_EVENT_POINTER_BUTTON", self.source)
        self.assertIn("dispatch_desktop_event", self.source)
        self.assertIn("desktop_icon_at_position", self.source)

    def test_mouse_button_release_requires_a_falling_edge(self):
        self.assertIn("if (left_down && !left_was_down)", self.source)
        self.assertIn("else if (!left_down && left_was_down)", self.source)
        self.assertNotIn("else if (left_was_down)", self.source)

    def test_mouse_motion_is_coalesced_between_button_edges(self):
        self.assertIn("#define DESKTOP_MOUSE_BATCH_LIMIT 32U", self.source)
        self.assertIn("static void accumulate_mouse_delta", self.source)
        self.assertIn("static uint32_t dispatch_pointer_motion", self.source)
        self.assertIn("static uint32_t dispatch_pointer_button", self.source)
        main = self.source[self.source.index("int main(") :]
        self.assertIn("pending_delta_x", main)
        self.assertIn("pending_delta_y", main)
        self.assertIn("mouse_events < DESKTOP_MOUSE_BATCH_LIMIT", main)
        self.assertLess(
            main.index("dispatch_pointer_motion("),
            main.index("dispatch_pointer_button("),
        )
        self.assertIn(
            "ui->dialog.capture_kind == REIST_GUI_DIALOG_CAPTURE_MOVE",
            self.source,
        )

    def test_full_content_drag_uses_atomic_cached_shadow_blit(self):
        self.assertIn("desktop_move_cache_t", self.source)
        self.assertIn("desktop_move_cache_capture", self.source)
        self.assertIn("render_desktop_cached_move_frame", self.source)
        cached = self.source[
            self.source.index("static uint32_t render_desktop_cached_move_frame") :
            self.source.index("static void record_render_metrics")
        ]
        self.assertIn("x86os_display_frame_begin", cached)
        self.assertIn("x86os_display_frame_stage_blit", cached)
        self.assertIn("x86os_display_frame_commit", cached)
        self.assertIn("render_dirty_regions(", cached)
        self.assertIn("move->kind, move->window_index", cached)
        self.assertIn("context->omitted_kind != DESKTOP_MOVE_CACHE_DIALOG", self.source)
        self.assertIn("context->omitted_kind != DESKTOP_MOVE_CACHE_WINDOW", self.source)
        self.assertIn("ui->dialog.visible ||", self.source)
        self.assertIn("manager->z_order[DESKTOP_WM_CAPACITY - 1U]", self.source)
        self.assertNotIn("desktop_wm_t visual_manager = *manager", cached)
        self.assertNotIn("render_drag_outline", self.source)

    def test_dirty_redraw_preserves_glyphs_crossing_damage_edges(self):
        text_draw = self.source[
            self.source.index("static void draw_text_clipped") :
            self.source.index("static uint32_t menu_height")
        ]
        self.assertIn(
            "text_top >= clip_bottom || text_bottom <= clip_top", text_draw
        )
        self.assertIn(
            "glyph_left < clip_right && glyph_right > clip_left", text_draw
        )
        self.assertIn(
            "glyph_left >= clip_right || glyph_right <= clip_left", text_draw
        )
        self.assertNotIn(
            "glyph_left >= clip_left && glyph_right <= clip_right", text_draw
        )
        self.assertIn("x86os_draw_text_pixels_clipped(", text_draw)
        self.assertIn("context->clip.width, context->clip.height", text_draw)
        self.assertNotIn("x86os_draw_text_pixels(", text_draw)

        dirty_start = self.source.index("static void render_dirty_regions")
        dirty_redraw = self.source[
            dirty_start : self.source.index("static void render_desktop(", dirty_start)
        ]
        self.assertIn(".clip = dirty->rects[index]", dirty_redraw)
        self.assertNotIn("expanded_render_clip", self.source)

if __name__ == "__main__":
    unittest.main()
