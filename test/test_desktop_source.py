import os
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
    def test_sampled_keyboard_precedes_later_pointer_focus_changes(self):
        loop = self.source[self.source.index('int key = read_key();'):]
        self.assertLess(loop.index('desktop_ui_keyboard_event(&ui,'),
                        loop.index('for (; mouse_events < DESKTOP_MOUSE_BATCH_LIMIT;'))
        self.assertLess(loop.index('enqueue_surface_keyboard(&manager,'),
                        loop.index('for (; mouse_events < DESKTOP_MOUSE_BATCH_LIMIT;'))

    def test_menu_escape_then_start_host_behavior(self):
        # Real menu controller complements the dispatch-order source assertion;
        # the full compositor/client lifecycle is proved in the guest gate.
        compiler = shutil.which('gcc') or shutil.which('clang')
        command = [compiler] if compiler else [shutil.which('zig'), 'cc']
        self.assertIsNotNone(command[0])
        fixture = r'''
#include <assert.h>
#include <reist/gui/menu.h>
int main(void) {
    const reist_gui_menu_item_t items[] = {{"Exit", 1U, 0U, 0U, 0U}};
    const reist_gui_menu_t menus[] = {{"Start", items, 1U, 0U, 0U}};
    const reist_gui_menu_model_t model = { .version=1U,
        .struct_size=sizeof(model), .menus=menus, .menu_count=1U };
    const reist_gui_menu_layout_t layout = { .version=1U,
        .struct_size=sizeof(layout), .surface_width=1024U, .surface_height=768U,
        .bar={4,741,1016U,24U}, .font_width=8U, .font_height=16U,
        .title_padding_x=16U, .item_padding_x=8U, .item_padding_y=4U,
        .popup_direction=REIST_GUI_MENU_POPUP_ABOVE };
    reist_gui_menu_state_t state;
    reist_gui_menu_event_t key, pointer;
    reist_gui_menu_result_t result;
    reist_gui_menu_event_initialize(&key);
    key.type=REIST_GUI_MENU_EVENT_KEYBOARD;
    key.key=REIST_GUI_MENU_KEY_ESCAPE;
    reist_gui_menu_event_initialize(&pointer);
    pointer.type=REIST_GUI_MENU_EVENT_POINTER_BUTTON;
    pointer.x=40; pointer.y=753; pointer.button=REIST_GUI_MENU_BUTTON_LEFT;
    for (unsigned reversed=0; reversed<2; ++reversed) {
        reist_gui_menu_state_initialize(&state);
        reist_gui_menu_result_initialize(&result);
        if (!reversed) {
            assert(reist_gui_menu_dispatch(&model,&layout,&state,&key,&result)==0);
            assert(!result.consumed); /* Escape still belongs to the client. */
        }
        pointer.pressed=1;
        assert(reist_gui_menu_dispatch(&model,&layout,&state,&pointer,&result)==0);
        pointer.pressed=0;
        assert(reist_gui_menu_dispatch(&model,&layout,&state,&pointer,&result)==0);
        assert(state.open_menu==0U && state.capture_kind==REIST_GUI_MENU_CAPTURE_NONE);
        if (reversed) {
            assert(reist_gui_menu_dispatch(&model,&layout,&state,&key,&result)==0);
            assert(result.consumed && state.open_menu==REIST_GUI_MENU_NO_INDEX);
        }
    }
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='reist-menu-order-') as temp:
            source = Path(temp) / 'menu-order.c'
            source.write_text(fixture, encoding='utf-8')
            for optimization in ('-O0', '-O2'):
                executable = Path(temp) / ('menu-order' + optimization + '.exe')
                build = subprocess.run(command + ['-std=c11', optimization,
                    '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                    '-Iuserspace/gui/include', str(source),
                    'userspace/gui/lib/menu.c', '-o', str(executable)],
                    cwd=ROOT, capture_output=True, text=True, timeout=60)
                self.assertEqual(build.returncode, 0, build.stderr)
                run = subprocess.run([str(executable)], capture_output=True,
                                     text=True, timeout=5)
                self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_display_close_waits_for_both_surface_retirements(self):
        runner = (ROOT / 'scripts/run_qemu_display_settings.py').read_text()
        close = runner[runner.index('    def close_desktop('):runner.index('    def run(')]
        self.assertLess(close.index('DISPLAY_PROBE_APPLET_RETIRED'),
                        close.index('DISPLAY_PROBE_CONTROL_RETIRED'))
        self.assertLess(close.index('DISPLAY_PROBE_CONTROL_RETIRED'),
                        close.index('self.click(*self.start_point)'))
        self.assertIn('str(self.control_pid)', close)
        self.assertIn('if (display_probe_control_live && !control_live)', self.source)

    def test_terminal_generation_admission_precedes_desktop_activation(self):
        source = (ROOT / "userspace/gui/compositor/desktop.c").read_text()
        self.assertLess(source.index("if (desktop_terminal_acquire()"),
                        source.index("int activation_status = desktop_activate_configured()"))
        self.assertIn("return desktop_activate_with_fallback();", source)
        self.assertIn("REIST_TERMINAL_ACQUIRE_SERVICE", source)
        self.assertIn("REIST_TERMINAL_RELEASE", source)
        self.assertIn("now - start >= 1000U", source)
        self.assertIn("terminal_probe_idle && key == 7", source)

    @classmethod
    def setUpClass(cls):
        cls.source = DESKTOP.read_text(encoding="utf-8")

    def test_window_manager_model_host_behavior(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        command = [compiler] if compiler else [shutil.which("zig"), "cc"]
        self.assertIsNotNone(command[0], "C compiler required for drag-anchor proof")
        environment = os.environ.copy()
        environment["ZIG_GLOBAL_CACHE_DIR"] = str(ROOT / "build/zig-global-cache")
        environment["ZIG_LOCAL_CACHE_DIR"] = str(ROOT / "build/zig-cache")
        with tempfile.TemporaryDirectory(prefix="reist-desktop-wm-") as temp:
            executable = Path(temp) / "desktop-wm-test.exe"
            subprocess.run(
                 command + ["-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-I.", "test/test_desktop_wm_host.c",
                 "userspace/gui/compositor/desktop_wm.c", "-o", str(executable)],
                cwd=ROOT, env=environment, check=True, capture_output=True,
                text=True, timeout=60)
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
        desktop_build = programs[
            programs.index('"DESKTOP.PRG"'):
            programs.index('"GUIDEMO.PRG"')
        ]
        self.assertIn("vfs_file_client.c", desktop_build)
        self.assertIn('#include "desktop_surface.h"', self.source)
        self.assertIn("desktop_surface_initialize(&surfaces)", self.source)

    def test_launcher_requires_the_pixel_display_abi(self):
        self.assertIn("x86os_display_info(&display)", self.source)
        self.assertIn("X86OS_DISPLAY_ABI_VERSION", self.source)
        self.assertIn("x86os_fill_rect", self.source)
        self.assertIn("x86os_draw_text_pixels", self.source)
        self.assertRegex(self.source, r"display\.width\s*<\s*320U")
        self.assertRegex(self.source, r"display\.height\s*<\s*240U")

    def test_interactive_compositor_reports_ordered_bounded_lifecycle(self):
        surface = self.source.index(
            "desktop_surface_runtime_initialize(&surface_runtime)")
        self_test = self.source.index(
            "X86OS_REIST_REPORT_SELF_TEST", surface)
        progress = self.source.index(
            "desktop_lifecycle_publish_progress(", self_test)
        splash = self.source.index("desktop_splash_show(", progress)
        icons = self.source.index(
            "desktop_file_icon_cache_initialize(", splash)
        first_frame = self.source.index(
            "render_desktop_measured(", icons)
        ready = self.source.index(
            "X86OS_REIST_REPORT_SERVICE_READY", first_frame)
        loop = self.source.index("for (;;) {", ready)
        self.assertLess(surface, self_test)
        self.assertLess(self_test, progress)
        self.assertLess(progress, splash)
        self.assertLess(splash, icons)
        self.assertLess(icons, first_frame)
        self.assertLess(first_frame, ready)
        self.assertLess(ready, loop)
        pre_ready = self.source[progress:ready]
        self.assertGreaterEqual(
            pre_ready.count("desktop_lifecycle_publish_progress("), 6)
        startup = self.source[ready:loop]
        self.assertGreaterEqual(
            startup.count("desktop_lifecycle_publish_progress("), 2)
        self.assertIn(
            "uint32_t lifecycle_sequence = 1U",
            self.source[self_test:ready])
        self.assertIn("uint32_t *sequence", self.source)
        self.assertIn("uint64_t *heartbeat_ms", self.source)
        self.assertIn("lifecycle_now_ms - lifecycle_heartbeat_ms >= 500U",
                      self.source[loop:])
        exit_path = self.source[self.source.index("static uint32_t desktop_try_exit"):
                                self.source.index("static char filetypes_config")]
        self.assertLess(exit_path.index("X86OS_REIST_REPORT_DIAGNOSTIC"),
                        exit_path.index("desktop_display_deactivate()"))

    def test_root_explorer_replaces_static_launcher_windows(self):
        self.assertIn('#include "desktop_explorer.h"', self.source)
        self.assertIn('{"Computer", "/",', self.source)
        self.assertNotIn("#define APP_COUNT", self.source)
        self.assertNotIn("static const desktop_app_t", self.source)
        self.assertIn("desktop_explorer_open", self.source)
        self.assertIn("desktop_explorer_child_path", self.source)
        self.assertIn("desktop_icon_rect", self.source)
        self.assertIn("render_window", self.source)

    def test_explorer_scrollbar_uses_current_client_and_all_input_paths(self):
        explorer = (
            ROOT / "userspace" / "gui" / "compositor" /
            "desktop_explorer.c"
        ).read_text(encoding="utf-8")
        header = (
            ROOT / "userspace" / "gui" / "compositor" /
            "desktop_explorer.h"
        ).read_text(encoding="utf-8")
        self.assertIn("DESKTOP_EXPLORER_ENTRY_CAPACITY 128U", header)
        self.assertIn("DESKTOP_EXPLORER_SCROLLBAR_EXTENT 18U", header)
        self.assertIn("static void render_explorer_scrollbar", self.source)
        self.assertIn("desktop_explorer_layout(explorer_window, client)",
                      self.source)
        self.assertIn("desktop_explorer_pointer_motion(", self.source)
        self.assertIn("desktop_explorer_wheel(", self.source)
        self.assertIn("desktop_explorer_resize(", self.source)
        self.assertIn("mouse.wheel", self.source)
        self.assertIn("layout.maximum_first_row", explorer)
        self.assertIn("layout.scrollbar.x", self.source)
        render = self.source[
            self.source.index("static void render_explorer_scrollbar"):
            self.source.index("static void render_surface_paint_list")
        ]
        self.assertNotRegex(
            render, r"\b(x86os_open|x86os_read|reist_vfs_|malloc)\s*\("
        )

    def test_explorer_has_bounded_classic_same_window_navigation_chrome(self):
        explorer = (
            ROOT / "userspace" / "gui" / "compositor" /
            "desktop_explorer.c"
        ).read_text(encoding="utf-8")
        header = (
            ROOT / "userspace" / "gui" / "compositor" /
            "desktop_explorer.h"
        ).read_text(encoding="utf-8")
        self.assertIn("DESKTOP_EXPLORER_HISTORY_CAPACITY 16U", header)
        for api in ("desktop_explorer_navigate", "desktop_explorer_back",
                    "desktop_explorer_forward", "desktop_explorer_up",
                    "desktop_explorer_refresh"):
            self.assertIn(api, explorer)
            self.assertIn(api, self.source)
        activation = self.source[
            self.source.index("static uint32_t apply_desktop_activation"):
            self.source.index("static void apply_control_panel_activation")
        ]
        self.assertIn("desktop_explorer_navigate(", activation)
        directory_branch = activation[
            activation.index("X86OS_DIRECTORY"):
        ]
        self.assertNotIn("open_explorer_path(", directory_branch)
        for label in ("< Zurueck", '"Adresse:"', '"Aktual."'):
            self.assertIn(label, self.source)
        self.assertIn("DESKTOP_EXPLORER_NAVIGATION_FORWARD", self.source)
        self.assertIn("DESKTOP_EXPLORER_NAVIGATION_UP", self.source)
        self.assertIn("DESKTOP_EXPLORER_STATUS_HEIGHT", self.source)
        self.assertIn("key == '\\b' || key == 0x7F", self.source)
        chrome = self.source[
            self.source.index("static void render_explorer_chrome"):
            self.source.index("static void render_surface_paint_list")
        ]
        self.assertNotRegex(
            chrome, r"\b(x86os_open|x86os_read|reist_vfs_|malloc)\s*\("
        )

    def test_explorer_switches_between_icons_and_fixed_details_rows(self):
        for token in (
                "DESKTOP_EXPLORER_NAVIGATION_VIEW",
                "desktop_explorer_toggle_view(",
                "render_explorer_details_header",
                "desktop_explorer_detail_columns",
                '"Details"', '"Symbole"', '"Name"', '"Typ"',
                '"Groesse"', '"Geaendert UTC"'):
            self.assertIn(token, self.source)
        self.assertIn("desktop_explorer_format_size(", self.source)
        self.assertIn("desktop_explorer_format_modified_utc(", self.source)
        details = self.source[
            self.source.index("static void render_explorer_details_header"):
            self.source.index("static void render_explorer_scrollbar")
        ]
        self.assertNotRegex(
            details, r"\b(x86os_open|x86os_read|reist_vfs_|malloc)\s*\("
        )

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

    def test_taskbar_uses_the_public_menu_api_and_typed_window_actions(self):
        programs = (ROOT / "scripts" / "build_system_programs.py").read_text(
            encoding="utf-8"
        )
        self.assertIn('#include "reist/gui/menu.h"', self.source)
        self.assertIn("static const reist_gui_menu_model_t", self.source)
        self.assertIn("reist_gui_menu_dispatch", self.source)
        self.assertIn("render_taskbar", self.source)
        self.assertIn("render_menu_popup", self.source)
        self.assertIn("render_system_dialog", self.source)
        self.assertIn("DESKTOP_MENU_ACTION_HELP", self.source)
        self.assertIn("DESKTOP_MENU_ACTION_ABOUT", self.source)
        self.assertIn("DESKTOP_WM_EVENT_OPEN", self.source)
        self.assertIn("DESKTOP_WM_EVENT_CLOSE", self.source)
        self.assertIn("DESKTOP_WM_EVENT_SELECT", self.source)
        gui_programs = programs.split("GUI_PROGRAMS", 1)[1]
        for program in ("DESKTOP.PRG", "GUIDEMO.PRG", "NOTEPAD.PRG",
                        "SOUNDPLAYER.PRG", "IMAGEVIEWER.PRG", "BROWSER.PRG"):
            self.assertIn(f'"{program}"', gui_programs)
        self.assertIn("gui_library", programs)

    def test_taskbar_has_start_tasks_and_validated_minute_clock(self):
        self.assertIn("static uint32_t taskbar_height", self.source)
        self.assertIn('"Start"', self.source)
        self.assertIn('"Computer oeffnen"', self.source)
        self.assertIn('"Systemsteuerung"', self.source)
        self.assertIn('"Webbrowser"', self.source)
        self.assertIn("REIST_GUI_MENU_POPUP_ABOVE", self.source)
        self.assertIn("desktop_task_button_rect", self.source)
        self.assertIn("desktop_taskbar_window_at", self.source)
        self.assertIn("desktop_partition_offset", self.source)
        self.assertNotIn("(uint64_t)available *", self.source)
        self.assertIn("DESKTOP_TASKBAR_CAPTURE_BACKGROUND", self.source)
        self.assertIn("DESKTOP_WM_CAPACITY", self.source)
        self.assertIn("x86os_get_date()", self.source)
        self.assertIn("x86os_get_time()", self.source)
        self.assertIn("desktop_clock_refresh", self.source)
        self.assertIn("DESKTOP_CLOCK_POLL_MS 1000U", self.source)
        self.assertIn("desktop_clock_rect", self.source)
        self.assertNotIn(
            '"Menue: Klick/Pfeile/ENTER   Fenster: Ziehen/Groesse',
            self.source,
        )

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

    def test_trash_context_menu_and_restore_action_are_explicit(self):
        self.assertIn("trash_context_menu_model", self.source)
        self.assertIn("render_trash_context_popup", self.source)
        self.assertIn("X86OS_MOUSE_BUTTON_RIGHT", self.source)
        self.assertIn('"Papierkorb leeren"', self.source)
        self.assertIn('"Wiederherstellen"', self.source)
        self.assertIn("DESKTOP_DIALOG_EMPTY_TRASH", self.source)
        self.assertIn("REIST_GUI_DIALOG_RESPONSE_YES", self.source)
        self.assertIn("apply_trash_restore", self.source)
        self.assertIn("apply_trash_empty", self.source)

    def test_trash_documentation_probes_use_a_real_bounded_trash_object(self):
        self.assertIn("prepare_trash_documentation_probe", self.source)
        self.assertIn('"--trash-context-probe"', self.source)
        self.assertIn('"--trash-confirm-probe"', self.source)
        self.assertIn('"--trash-restore-probe"', self.source)
        self.assertIn('"/trash-demo.txt"', self.source)
        self.assertIn("desktop_trash_move(", self.source)
        self.assertIn("DESKTOP_TRASH_CONTEXT_READY", self.source)
        self.assertIn("DESKTOP_TRASH_CONFIRM_READY", self.source)
        self.assertIn("DESKTOP_TRASH_RESTORE_READY", self.source)
        self.assertIn("desktop_trash_restore(", self.source)
        self.assertIn(
            "DESKTOP_DIALOG_EMPTY_TRASH", self.source
        )

    def test_redraw_is_driven_by_fixed_dirty_regions_and_clips_primitives(self):
        header = WM_HEADER.read_text(encoding="utf-8")
        self.assertIn("#define DESKTOP_WM_DIRTY_CAPACITY 8U", header)
        self.assertIn("desktop_dirty_region_t", header)
        self.assertIn("static void render_dirty_regions", self.source)
        self.assertIn("fill_rect_clipped", self.source)
        self.assertIn("draw_text_clipped", self.source)

    def test_menu_state_changes_keep_exact_clipped_damage(self):
        collect = self.source[self.source.index("static void collect_menu_damage") :]
        collect = collect[: collect.index("\n}") + 2]
        self.assertIn("if (menu_result->full_redraw)", collect)
        self.assertIn("desktop_dirty_full(dirty)", collect)
        self.assertIn("index < menu_result->damage_count", collect)
        self.assertIn("desktop_dirty_add", collect)
        self.assertIn("desktop_rect_from_gui", collect)
        self.assertNotIn("damage_count != 0U", collect)

    def test_vmware_menu_feedback_uses_a_bounded_start_item_strip(self):
        activation = self.source[
            self.source.index("static int desktop_activate_with_fallback") :
            self.source.index("static int desktop_svga2d_activate_until_ready")
        ]
        self.assertIn("desktop_low_latency_menu_feedback = 0U", activation)
        self.assertIn("REIST_SVGA2D_CAP_RECT_COPY", activation)
        collect = self.source[self.source.index("static void collect_menu_damage") :]
        collect = collect[: collect.index("\n}") + 2]
        self.assertIn("desktop_start_menu_item_damage(display, damage)", collect)
        self.assertIn("ui->menu.open_menu == DESKTOP_MENU_START", collect)
        self.assertIn("DESKTOP_MENU_FAST_FEEDBACK_WIDTH", collect)
        render_item = self.source[
            self.source.index("static void render_menu_item_model") :
            self.source.index("static void render_menu_popup_model")
        ]
        self.assertIn("model == &desktop_menu_model", render_item)
        self.assertIn("fast_feedback", render_item)
        self.assertIn("(desktop_rect_t){item.x, item.y, feedback_width, item.height}", render_item)

    def test_opaque_start_menu_damage_skips_lower_scene_layers(self):
        classifier = self.source[
            self.source.index("static uint32_t menu_overlay_local_damage") :
            self.source.index("static void render_menu_overlay_damage")
        ]
        self.assertIn("dirty->full", classifier)
        self.assertIn("ui->menu.open_menu != DESKTOP_MENU_START", classifier)
        self.assertIn("ui->dialog.visible", classifier)
        self.assertIn("desktop_drag.phase != DESKTOP_DRAG_PHASE_IDLE", classifier)
        self.assertIn("start_menu_damage_bounds(display, &bounds)", classifier)
        self.assertIn("rect_contains(bounds, dirty->rects[index])", classifier)
        popup = self.source[
            self.source.index("static void render_menu_overlay_damage") :
            self.source.index("static void render_desktop_rect(")
        ]
        self.assertIn("index < dirty->count", popup)
        self.assertIn("visible_region_subtract_popup", popup)
        self.assertIn("desktop_taskbar_rect(display)", popup)
        self.assertIn("render_desktop_clip(", popup)
        self.assertIn("render_taskbar(&context", popup)
        self.assertIn("render_menu_popup(&context, ui)", popup)
        frame = self.source[
            self.source.index("static uint32_t render_desktop_frame") :
            self.source.index("static uint32_t render_desktop_cached_move_frame")
        ]
        self.assertIn("menu_overlay_local_damage(display, ui, dirty)", frame)
        self.assertIn("render_menu_overlay_damage(", frame)

    def test_pointer_motion_publishes_without_scheduler_throttle(self):
        self.assertIn(
            "#define DESKTOP_POINTER_CONTINUOUS_INPUT_INTERVAL_MS 16U",
            self.source,
        )
        self.assertNotIn("static uint32_t pointer_present_due", self.source)
        main = self.source[self.source.index("int main(") :]
        self.assertIn("pointer_present_pending", main)
        self.assertIn("pointer_pending_since_ms", main)
        self.assertIn("pointer_overlay_active", main)
        self.assertIn("desktop_pointer_present(", main)
        self.assertIn("pointer_latency_max_ms", self.source)
        self.assertIn("pointer_call_max_ms", self.source)
        self.assertIn("pointer_failures", self.source)
        no_damage = main[main.index("} else if (pointer_present_pending) {") :]
        self.assertLess(
            no_damage.index("desktop_pointer_present(pointer_x, pointer_y, 1U)"),
            no_damage.index("x86os_sleep_ms(DESKTOP_IDLE_POLL_MS)"),
        )
        self.assertIn(
            "pointer_pending_clock_valid = 0U;\n"
            "            }\n"
            "            (void)x86os_sleep_ms(DESKTOP_IDLE_POLL_MS);",
            no_damage,
        )
        self.assertIn("x86os_sleep_ms(DESKTOP_IDLE_POLL_MS)", main)
        self.assertNotIn("while (pointer_present", main)

    def test_hover_probe_measures_all_start_menu_rows(self):
        self.assertIn('text_equal(argv[1], "--hover-probe")', self.source)
        self.assertIn("DESKTOP_HOVER_PROBE_ITEMS", self.source)
        self.assertIn("DESKTOP_HOVER_METRICS", self.source)
        self.assertIn("DESKTOP_HOVER_OK", self.source)
        self.assertIn("DESKTOP_HOVER_MENU_READY", self.source)
        self.assertIn("!left_down && left_was_down", self.source)
        self.assertIn("hover_menu_ready_pending = 1U", self.source)
        self.assertIn("mouse_batch_max_ms", self.source)
        self.assertIn("mouse_batch_max_reports", self.source)
        self.assertIn("hover_probe_record_transition", self.source)
        self.assertIn("hover_probe_record_pointer_present", self.source)
        transition = self.source[
            self.source.index("static void hover_probe_record_transition") :
            self.source.index("static void print_render_metrics")
        ]
        self.assertNotIn("x86os_puts", transition)
        self.assertNotIn("x86os_putchar", transition)

    def test_ansi_arrow_keys_change_explorer_selection(self):
        self.assertIn("desktop_explorer_keyboard", self.source)
        for suffix in ("UP", "DOWN", "LEFT", "RIGHT"):
            self.assertIn(f"DESKTOP_KEY_{suffix}", self.source)
        for ansi in ("'A'", "'B'", "'C'", "'D'"):
            self.assertIn(ansi, self.source)

    def test_bare_escape_is_local_cancel_not_global_desktop_exit(self):
        decoder = self.source[self.source.index("static int read_key") :]
        decoder = decoder[: decoder.index("\n}") + 2]
        self.assertIn("if (prefix == 0) return DESKTOP_KEY_ESCAPE;", decoder)
        # Non-CSI lookahead is another key, not a reason to drop both keys.
        # Real O0/O2 Escape/Escape and Escape/text behavior is covered by
        # test_browser_surface_latency.py; local cancel remains the invariant.
        self.assertIn("if (prefix != '[') { pending = prefix; return DESKTOP_KEY_ESCAPE; }", decoder)
        self.assertIn("int value = pending ? pending : x86os_getchar_nonblocking();", decoder)
        self.assertIn("pending = 0;", decoder)
        self.assertIn("value < 0x40 || value > 0x7E", decoder)
        self.assertGreaterEqual(decoder.count("return DESKTOP_KEY_NONE;"), 3)
        self.assertEqual(decoder.count("return DESKTOP_KEY_ESCAPE;"), 2)
        main_loop = self.source[self.source.index("for (;;) {") :]
        self.assertNotIn(".key = DESKTOP_WM_KEY_ESCAPE", main_loop)
        self.assertIn("desktop_ui_keyboard_event", main_loop)
        self.assertIn("enqueue_surface_keyboard", main_loop)
        self.assertIn('"Desktop beenden", DESKTOP_MENU_ACTION_EXIT', self.source)
        self.assertIn(
            "menu_result->action == DESKTOP_MENU_ACTION_EXIT", self.source
        )

    def test_program_activation_spawns_and_waits_for_the_selected_child(self):
        launch = self.source[self.source.index("static int launch_program") :]
        launch = launch[: launch.index("\n}") + 2]
        self.assertIn("x86os_spawn(launch_program_path)", launch)
        self.assertIn("x86os_wait(pid, &status)", launch)
        self.assertNotIn("x86os_puts", launch)
        self.assertNotIn("x86os_clear", launch)

    def test_sound_player_uses_nonblocking_surface_launch(self):
        selector = self.source[
            self.source.index("static uint32_t program_uses_surface"):
            self.source.index("static int launch_program")
        ]
        self.assertIn('"/usr/gui/bin/soundplayer.prg"', selector)
        self.assertIn('"/usr/gui/bin/browser.prg"', selector)
        self.assertIn('"/usr/gui/bin/guidemo.prg"', selector)
        launch = self.source[self.source.index("static int launch_program"):]
        surface = launch[:launch.index(
            "/* Legacy full-screen clients remain synchronous")]
        self.assertIn("desktop_surface_runtime_bind", surface)
        bind_failure = surface[surface.index("if (bound != 0)"):]
        self.assertLess(bind_failure.index("x86os_kill(pid)"),
                        bind_failure.index("x86os_wait(pid, &status)"))
        self.assertIn("return 0;", surface)
        probe = self.source[
            self.source.index("static int launch_surface_probe_client"):
            self.source.index("static void clip_pointer")
        ]
        ownership = self.source[
            self.source.index("static uint32_t committed_surface_owned_by"):
            self.source.index("static int launch_surface_probe_client")
        ]
        self.assertIn("launched_owner", probe)
        self.assertIn("committed_surface_owned_by", probe)
        self.assertIn("surfaces->slots[index].committed", ownership)
        self.assertIn("surfaces->slots[index].paint_generation != 0U",
                      ownership)
        self.assertIn("DESKTOP_SURFACE_PROBE_READY_ATTEMPTS", probe)
        self.assertIn("DESKTOP_SURFACE_PROBE_READY_ATTEMPTS 3000U",
                      self.source)
        self.assertNotIn("active_surface_count", probe)
        self.assertIn('text_equal(argv[1], "--sound-probe")', self.source)
        self.assertIn('"DESKTOP_AUDIO_HEARTBEAT_OK\\n"', self.source)

    def test_system_sounds_are_configured_and_spawned_without_blocking_gui(self):
        config = (ROOT / "config/etc/reist/sounds.conf").read_text(
            encoding="utf-8")
        self.assertIn("schema=reist.sounds/1", config)
        for event in ("startup", "shutdown", "error",
                      "notification", "trash_drop", "trash_empty"):
            self.assertIn(f"event.{event}=", config)
        sound = self.source[
            self.source.index("static uint32_t desktop_system_sound_path_valid"):
            self.source.index("static int load_filetypes")
        ]
        self.assertIn("reist_config_parse(", sound)
        self.assertIn("DESKTOP_SYSTEM_SOUND_CHILD_CAPACITY 2U", self.source)
        self.assertIn('DESKTOP_SYSTEM_SOUND_PLAYER, "--quiet"', sound)
        self.assertIn("x86os_spawnv(DESKTOP_SYSTEM_SOUND_PLAYER", sound)
        self.assertIn("x86os_process_identity_of", sound)
        reap = sound[sound.index("static void desktop_system_sound_poll"):]
        self.assertLess(reap.index("x86os_process_identity_of"),
                        reap.index("x86os_wait(child->pid"))
        main = self.source[self.source.index("int main("):]
        for event in ("STARTUP", "SHUTDOWN", "ERROR",
                      "NOTIFICATION", "TRASH_DROP", "TRASH_EMPTY"):
            self.assertIn(f"DESKTOP_SYSTEM_SOUND_{event}", main)
        self.assertIn("saturating_increment(&ui->trash_drop_sequence)",
                      self.source)
        self.assertIn("saturating_increment(&ui->trash_empty_sequence)",
                      self.source)
        self.assertNotIn("DESKTOP_SYSTEM_SOUND_CLICK", self.source)
        self.assertNotIn("event.click", config)
        self.assertIn("system_sounds.enabled = 0U", main)
        self.assertNotIn("reist_audio_", self.source)

    def test_audio_surface_probe_uses_packaged_startup_sound(self):
        self.assertIn('"/USR/SHARE/SOUNDS/STARTUP.WAV"', self.source)
        self.assertNotIn('"/USR/SHARE/SOUNDS/440HZ.WAV"', self.source)
        self.assertIn(
            "int lifecycle_clock_status = "
            "x86os_monotonic_ms(&lifecycle_now_ms);",
            self.source,
        )
        self.assertIn("GUIDEMO_INTERACTION_OK", (
            ROOT / "scripts/run_qemu_runtime_desktop.py"
        ).read_text(encoding="utf-8"))

    def test_navigation_and_program_launch_do_not_compete_with_audio_clients(self):
        explorer_open = self.source[
            self.source.index("static uint32_t open_explorer_path"):
            self.source.index("static uint32_t close_all_explorer_windows")
        ]
        activation = self.source[
            self.source.index("static uint32_t apply_desktop_activation"):
            self.source.index("static uint32_t apply_desktop_ui_result")
        ]
        self.assertNotIn("notification_sequence", explorer_open)
        self.assertNotIn("notification_sequence", activation)
        dialog = self.source[
            self.source.index("static void desktop_ui_open_dialog"):
            self.source.index("static void desktop_ui_open_error")
        ]
        self.assertIn("notification_sequence", dialog)

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

    def test_first_render_precedes_the_desktop_ready_marker(self):
        main = self.source[self.source.index("int main(") :]
        first_render = main.index("render_desktop_measured(")
        service_ready = main.index(
            "X86OS_REIST_REPORT_SERVICE_READY", first_render)
        marker = main.index('x86os_puts("DESKTOP_OK\\n")')
        self.assertIn('x86os_puts("DESKTOP_STARTUP_MS value=")', main)
        self.assertIn('desktop_startup_phase_metric("font-io"', self.source)
        self.assertIn('desktop_startup_phase_metric("font-parse"', self.source)
        self.assertIn("desktop_editor_font_catalog_load(&display,",
                      self.source)
        self.assertIn("DESKTOP_EDITOR_FONT_CATALOG_READY families=5 sizes=8",
                      self.source)
        self.assertIn("if (!extension_needed) return 0", self.source)
        self.assertIn("if (desktop_font_attempted) return 0", self.source)
        self.assertIn("DESKTOP_FONT_LAZY_READY", self.source)
        self.assertIn("DESKTOP_FONT_LAZY_FALLBACK", self.source)
        self.assertLess(first_render, service_ready)
        self.assertLess(service_ready, marker)
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

    def test_live_resize_defers_client_reconfigure_and_bounds_damage(self):
        self.assertIn("surface_window_is_live_resizing", self.source)
        sync = self.source[
            self.source.index("static void sync_surface_windows") :
            self.source.index("static void print_render_metrics")
        ]
        self.assertIn("if (!live_resize &&", sync)
        manager = WM_SOURCE.read_text(encoding="utf-8")
        self.assertIn("collect_right_bottom_resize_damage", manager)
        self.assertIn("DESKTOP_WM_WINDOW_RETAINED_RESIZE", manager)
        self.assertIn("Right/bottom resizing leaves", manager)
        self.assertNotIn("resize_origin", manager)
        self.assertIn(
            "window->flags = DESKTOP_WM_WINDOW_RETAINED_RESIZE;",
            self.source,
        )
        self.assertIn("window->flags = 0U;", self.source)

    def test_retained_left_top_resize_uses_bounded_copy_cache(self):
        self.assertIn("DESKTOP_MOVE_CACHE_RESIZE", self.source)
        capture = self.source[
            self.source.index("static void desktop_move_cache_capture") :
            self.source.index("/* Relative USB reports are coalesced")
        ]
        self.assertIn("min_u32(source.width, destination.width)", capture)
        self.assertIn("min_u32(source.height, destination.height)", capture)
        self.assertIn("move->cleanup = source", capture)
        self.assertIn("move->redraw = destination", capture)
        cached = self.source[
            self.source.index("static uint32_t render_desktop_cached_move_frame") :
            self.source.index("static void record_render_metrics")
        ]
        self.assertGreaterEqual(
            cached.count("render_desktop_rect_difference("), 2
        )
        self.assertIn("move->cleanup, move->destination", cached)
        self.assertIn("move->redraw, move->destination", cached)
        self.assertIn("manager.capture_kind == DESKTOP_WM_CAPTURE_RESIZE", self.source)

    def test_live_resize_never_reads_past_acknowledged_surface(self):
        render_window = self.source[
            self.source.index("static void render_window") :
            self.source.index("static void render_taskbar")
        ]
        self.assertIn("desktop_rect_t committed_bounds", render_window)
        self.assertIn("client.width < surface->width", render_window)
        self.assertIn("client.height < surface->height", render_window)
        self.assertIn("buffer_clip.width, buffer_clip.height", render_window)

        cached = self.source[
            self.source.index("static uint32_t render_desktop_cached_move_frame") :
            self.source.index("static void record_render_metrics")
        ]
        self.assertIn("move->kind == DESKTOP_MOVE_CACHE_RESIZE", cached)
        self.assertIn("? -95", cached)

    def test_surface_program_is_async_and_owned_by_the_compositor(self):
        self.assertIn('"/usr/gui/bin/surfacedemo.prg"', self.source)
        self.assertIn('"/usr/gui/bin/notepad.prg"', self.source)
        self.assertIn('"/usr/gui/bin/imageviewer.prg"', self.source)
        self.assertIn('"/usr/gui/bin/browser.prg"', self.source)
        self.assertIn("launch_surface_probe_client", self.source)
        self.assertIn('"/USR/GUI/BIN/NOTEPAD.PRG", "/README.TXT",',
                      self.source)
        self.assertIn("program_uses_surface", self.source)
        self.assertIn("path_equal_ascii_case", self.source)
        surface_classifier = self.source[
            self.source.index("static uint32_t program_uses_surface") :
            self.source.index("static int launch_program")
        ]
        self.assertNotIn("text_equal(program", surface_classifier)
        self.assertEqual(surface_classifier.count("path_equal_ascii_case"), 8)
        self.assertIn('"/usr/gui/bin/display.prg"', surface_classifier)
        self.assertIn("desktop_surface_runtime_reserve", self.source)
        self.assertIn("desktop_surface_runtime_bind", self.source)
        self.assertIn("sync_surface_windows", self.source)
        self.assertIn("desktop_surface_runtime_send_close", self.source)

    def test_retained_surface_paint_is_clipped_to_current_client(self):
        paint_list = self.source[
            self.source.index("static void render_surface_paint_list") :
            self.source.index("static void render_window")
        ]
        render_window = self.source[
            self.source.index("static void render_window") :
            self.source.index("static void render_taskbar")
        ]
        self.assertIn(
            "intersect_rects(client, context->clip, &surface_clip)",
            render_window,
        )
        self.assertIn("surface_context.clip = surface_clip", render_window)
        self.assertIn("fill_rect_clipped(context, bounds", paint_list)
        self.assertIn("context, bounds.x, bounds.y", paint_list)
        base = "surface->committed_paint_count"
        dynamic = "surface->committed_dynamic_paint_count"
        overlay = "surface->committed_overlay_paint_count"
        hover = "surface->committed_hover_paint_count"
        self.assertIn(base, render_window)
        self.assertIn(dynamic, render_window)
        self.assertIn(overlay, render_window)
        self.assertIn(hover, render_window)
        self.assertLess(render_window.index(base), render_window.index(dynamic))
        self.assertLess(render_window.index(dynamic), render_window.index(overlay))
        self.assertLess(render_window.index(base), render_window.index(overlay))
        self.assertLess(render_window.index(overlay), render_window.index(hover))

    def test_surface_commit_maps_only_local_presentation_damage(self):
        sync = self.source[
            self.source.index("static void sync_surface_windows"):
            self.source.index("static void print_render_metrics")
        ]
        self.assertIn("desktop_surface_present_damage_take", sync)
        self.assertIn("client.x + local_damage.x", sync)
        self.assertIn("client.y + local_damage.y", sync)
        presentation = sync[
            sync.index("if (surface->paint_generation"):
            sync.index("surface->presented_generation = surface->paint_generation")
        ]
        self.assertIn("if (damage_status == DESKTOP_SURFACE_OK)", presentation)
        self.assertLess(
            presentation.index("desktop_surface_present_damage_take"),
            presentation.index("desktop_dirty_add(dirty, presentation_damage)"),
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
            self.source.index("static uint32_t render_desktop_measured") :
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
        self.assertIn('x86os_puts("DESKTOP_ACCELERATION")', metrics)
        self.assertIn('print_metric("accelerated_frames"', metrics)
        self.assertIn('print_metric("fallbacks"', metrics)
        self.assertIn('print_metric("observed_caps"', metrics)
        self.assertIn('print_metric("reconnects"', metrics)
        self.assertIn('print_metric("reconnect_attempts"', metrics)
        self.assertIn('" connect_status="', metrics)
        self.assertIn('" service_status="', metrics)
        self.assertIn('" transaction_status="', metrics)
        self.assertIn('" copy_status="', metrics)
        self.assertIn('" mark_status="', metrics)

    def test_launcher_is_freestanding_and_has_no_host_libc_dependency(self):
        self.assertNotRegex(self.source, r"#include\s*<(stdio|stdlib|string)\.h>")
        self.assertNotRegex(self.source, r"\b(printf|strlen|memcpy|malloc|free)\s*\(")

    def test_file_associations_are_bounded_and_pass_the_document_as_argv(self):
        self.assertIn('#include "desktop_filetypes.h"', self.source)
        load_filetypes = self.source[
            self.source.index("static int load_filetypes"):
            self.source.index("static int format_surface_argument")
        ]
        self.assertIn("read_file_bounded(", load_filetypes)
        self.assertNotIn("x86os_open(", load_filetypes)
        self.assertNotIn("x86os_read(", load_filetypes)
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

    def test_nonreplaceable_surface_input_overload_fences_the_client(self):
        self.assertIn("DESKTOP_SURFACE_ECAPACITY", self.source)
        self.assertIn(
            "DESKTOP_SURFACE_INPUT_FENCED status=-75", self.source
        )
        self.assertIn("desktop_surface_destroy(", self.source)

    def test_mouse_motion_is_coalesced_between_button_edges(self):
        self.assertIn("#define DESKTOP_MOUSE_BATCH_LIMIT 4U", self.source)
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

    def test_surface_input_gets_same_turn_bounded_paint_drain(self):
        loop = self.source[self.source.index("for (;;) {") :]
        self.assertIn("surface_input_queued", loop)
        post_input = loop.index("if (surface_input_queued)")
        final_sync = loop.index("sync_surface_windows(", post_input)
        bounded = loop[post_input:final_sync]
        self.assertIn("x86os_yield()", bounded)
        self.assertEqual(
            bounded.count("desktop_surface_runtime_poll("), 1
        )
        self.assertNotIn("while (", bounded)

    def test_surface_input_yields_only_when_client_cannot_run_in_parallel(self):
        loop = self.source[self.source.index("for (;;) {") :]
        post_input = loop[loop.index("if (surface_input_queued)") :]
        post_input = post_input[:post_input.index("sync_surface_windows(")]
        self.assertIn("if (online_cpu_count == 1U) (void)x86os_yield();",
                      post_input)
        self.assertIn("x86os_cpu_topology(&topology)", self.source)
        self.assertIn("topology.version == X86OS_CPU_TOPOLOGY_VERSION",
                      self.source)
        self.assertIn("uint32_t online_cpu_count = 1U;", self.source)

    def test_idle_input_poll_cadence_is_one_millisecond(self):
        loop = self.source[self.source.index("for (;;) {") :]
        self.assertIn("x86os_sleep_ms(DESKTOP_IDLE_POLL_MS)", loop)
        self.assertIn("#define DESKTOP_IDLE_POLL_MS 1U", self.source)
        self.assertNotIn("x86os_sleep_ms(5U)", loop)

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
        self.assertIn("render_desktop_rect_difference(", cached)
        self.assertIn("omitted_kind, move->window_index", cached)
        self.assertIn("context->omitted_kind != DESKTOP_MOVE_CACHE_DIALOG", self.source)
        self.assertIn("context->omitted_kind == DESKTOP_MOVE_CACHE_WINDOW", self.source)
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
        self.assertIn("reist_utf8_prefix(", text_draw)
        self.assertIn("maximum_scalars", text_draw)
        self.assertIn("text, prefix_bytes", text_draw)
        self.assertNotIn("text + first", text_draw)
        self.assertIn("x86os_draw_text_pixels_clipped(", text_draw)
        self.assertIn("context->clip.width, context->clip.height", text_draw)
        self.assertNotIn("x86os_draw_text_pixels(", text_draw)

        dirty_start = self.source.index("static void render_dirty_regions")
        dirty_redraw = self.source[
            dirty_start : self.source.index("static void render_desktop(", dirty_start)
        ]
        self.assertIn(".clip = dirty->rects[index]", dirty_redraw)
        self.assertNotIn("expanded_render_clip", self.source)

    def test_compositor_culls_opaque_regions_with_bounded_safe_fallback(self):
        start = self.source.index("#define DESKTOP_VISIBLE_REGION_CAPACITY")
        end = self.source.index("static void render_dirty_regions", start)
        culling = self.source[start:end]
        self.assertIn("desktop_visible_region_t", culling)
        self.assertIn("visible_region_subtract", culling)
        self.assertIn("window_visual_bounds", culling)
        self.assertIn("visible_region_subtract_system_ui", culling)
        self.assertIn("higher = position + 1U", culling)
        self.assertIn("render_desktop_background(&clipped", culling)
        self.assertIn("render_window(\n                &clipped", culling)
        self.assertIn("if (!background_culled)", culling)
        self.assertIn("if (!culled)", culling)
        self.assertNotIn("malloc", culling)

if __name__ == "__main__":
    unittest.main()
