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

    def test_launcher_requires_the_pixel_display_abi(self):
        self.assertIn("x86os_display_info(&display)", self.source)
        self.assertIn("X86OS_DISPLAY_ABI_VERSION", self.source)
        self.assertIn("x86os_fill_rect", self.source)
        self.assertIn("x86os_draw_text_pixels", self.source)
        self.assertRegex(self.source, r"display\.width\s*<\s*320U")
        self.assertRegex(self.source, r"display\.height\s*<\s*240U")

    def test_four_required_apps_are_visible_and_launchable(self):
        self.assertIn("#define APP_COUNT 4U", self.source)
        for title, program in (
            ("Shell", "/bin/shell.prg"),
            ("Dateien", "/bin/ls.prg"),
            ("Editor", "/bin/edit.prg"),
            ("System", "/sbin/sysinfo.prg"),
        ):
            self.assertIn(f'"{title}"', self.source)
            self.assertIn(f'"{program}"', self.source)
        self.assertIn("desktop_icon_rect", self.source)
        self.assertIn("render_window", self.source)

    def test_window_manager_has_fixed_z_order_focus_and_capture(self):
        header = WM_HEADER.read_text(encoding="utf-8")
        model = WM_SOURCE.read_text(encoding="utf-8")
        self.assertIn("#define DESKTOP_WM_CAPACITY 4U", header)
        self.assertIn("windows[DESKTOP_WM_CAPACITY]", header)
        self.assertIn("z_order[DESKTOP_WM_CAPACITY]", header)
        self.assertIn("DESKTOP_WM_CAPTURE_MOVE", header)
        self.assertIn("DESKTOP_WM_CAPTURE_CLOSE", header)
        self.assertIn("desktop_wm_window_at", model)
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
        main = self.source[self.source.index("int main(void)") :]
        self.assertNotIn("desktop_wm_pointer_press(&manager", main)
        self.assertNotIn("desktop_wm_pointer_motion(&manager", main)
        self.assertNotIn("desktop_wm_pointer_release(&manager", main)

    def test_redraw_is_driven_by_fixed_dirty_regions_and_clips_primitives(self):
        header = WM_HEADER.read_text(encoding="utf-8")
        self.assertIn("#define DESKTOP_WM_DIRTY_CAPACITY 8U", header)
        self.assertIn("desktop_dirty_region_t", header)
        self.assertIn("static void render_dirty_regions", self.source)
        self.assertIn("fill_rect_clipped", self.source)
        self.assertIn("draw_text_clipped", self.source)

    def test_tab_and_ansi_arrow_keys_change_selection(self):
        self.assertIn("key == '\\t'", self.source)
        for suffix in ("UP", "DOWN", "LEFT", "RIGHT"):
            self.assertIn(f"DESKTOP_KEY_{suffix}", self.source)
        for ansi in ("'A'", "'B'", "'C'", "'D'"):
            self.assertIn(ansi, self.source)

    def test_only_a_bare_escape_launches_shell(self):
        decoder = self.source[self.source.index("static int read_key") :]
        decoder = decoder[: decoder.index("\n}") + 2]
        self.assertIn("if (prefix == 0) return DESKTOP_KEY_ESCAPE;", decoder)
        self.assertIn("if (prefix != '[') return DESKTOP_KEY_NONE;", decoder)
        self.assertIn("value < 0x40 || value > 0x7E", decoder)
        self.assertGreaterEqual(decoder.count("return DESKTOP_KEY_NONE;"), 3)
        self.assertEqual(decoder.count("return DESKTOP_KEY_ESCAPE;"), 1)

    def test_enter_spawns_and_waits_for_the_selected_child(self):
        launch = self.source[self.source.index("static void launch_app") :]
        launch = launch[: launch.index("\n}") + 2]
        self.assertIn("x86os_spawn(apps[index].program)", launch)
        self.assertIn("x86os_spawnv(apps[index].program, 2, arguments)", launch)
        self.assertIn("x86os_wait(pid, &status)", launch)
        self.assertIn('x86os_puts("DESKTOP_LAUNCH:")', launch)
        self.assertLess(launch.index("x86os_clear();"), launch.index("x86os_spawn"))

    def test_wait_failure_terminates_and_reaps_child_before_input_returns(self):
        launch = self.source[self.source.index("static void launch_app") :]
        wait_check = launch.index("if (wait_result != pid)")
        kill = launch.index("x86os_kill(pid)", wait_check)
        reap = launch.index("x86os_wait(pid, &status)", kill)
        pause = launch.index("(void)x86os_getchar();", reap)
        self.assertLess(wait_check, kill)
        self.assertLess(kill, reap)
        self.assertLess(reap, pause)

    def test_editor_receives_a_real_document_argument(self):
        self.assertRegex(
            self.source,
            r'\{"Editor",\s*"Textdateien bearbeiten",\s*"/bin/edit\.prg",'
            r'\s*"desktop\.txt"',
        )

    def test_child_input_is_drained_before_the_desktop_is_redrawn(self):
        launch = self.source[self.source.index("static void launch_app") :]
        launch = launch[: launch.index("\n}") + 2]
        self.assertLess(launch.index("x86os_wait"), launch.index("drain_input();"))
        self.assertLess(
            launch.index('x86os_puts("\\nTaste zum Desktop...")'),
            launch.index("(void)x86os_getchar();"),
        )
        self.assertLess(launch.index("(void)x86os_getchar();"), launch.index("drain_input();"))
        self.assertNotIn("render_desktop", launch)
        drain = self.source[self.source.index("static void drain_input") :]
        self.assertIn("x86os_getchar_nonblocking()", drain)

    def test_spawn_failure_reports_its_status_before_waiting(self):
        launch = self.source[self.source.index("static void launch_app") :]
        self.assertIn('x86os_puts("Start fehlgeschlagen (Status ")', launch)
        self.assertIn("print_integer(pid);", launch)

    def test_first_render_emits_the_desktop_ready_marker(self):
        main = self.source[self.source.index("int main(void)") :]
        first_render = main.index("render_desktop_frame(&display, &manager,")
        marker = main.index('x86os_puts("DESKTOP_OK\\n")')
        self.assertLess(marker, first_render)
        startup = main[:main.index("for (;;)")]
        self.assertEqual(
            startup.count("render_desktop_frame(&display, &manager,"), 1
        )

    def test_scene_redraw_uses_a_bounded_frame_transaction(self):
        redraw = self.source[
            self.source.index("static void render_desktop_frame") :
        ]
        redraw = redraw[: redraw.index("\n}") + 2]
        self.assertIn("x86os_display_frame_begin", redraw)
        self.assertIn("x86os_display_frame_commit", redraw)
        self.assertIn("x86os_display_frame_cancel", redraw)
        self.assertLess(redraw.index("x86os_display_frame_begin"),
                        redraw.index("render_desktop("))
        self.assertLess(redraw.index("render_desktop("),
                        redraw.index("x86os_display_frame_commit"))

    def test_launcher_is_freestanding_and_has_no_host_libc_dependency(self):
        self.assertNotRegex(self.source, r"#include\s*<(stdio|stdlib|string)\.h>")
        self.assertNotRegex(self.source, r"\b(printf|strlen|memcpy|malloc|free)\s*\(")

    def test_usb_mouse_moves_a_clipped_visible_pointer(self):
        self.assertIn("x86os_mouse_event(&mouse)", self.source)
        self.assertIn("static void move_pointer", self.source)
        self.assertIn("(int64_t)*pointer_x + delta_x", self.source)
        self.assertIn("(int64_t)*pointer_y + delta_y", self.source)
        self.assertIn("mouse.delta_x, mouse.delta_y", self.source)
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


if __name__ == "__main__":
    unittest.main()
