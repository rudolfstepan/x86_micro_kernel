import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DESKTOP = ROOT / "userspace" / "programs" / "desktop.c"


class DesktopSourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = DESKTOP.read_text(encoding="utf-8")

    def test_desktop_is_part_of_the_system_program_image(self):
        programs = (ROOT / "scripts" / "build_system_programs.py").read_text(
            encoding="utf-8"
        )
        self.assertIn('"DESKTOP.PRG": ROOT / "userspace/programs/desktop.c"', programs)

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
        self.assertIn("index % 2U", self.source)
        self.assertIn("index / 2U", self.source)

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
        self.assertLess(launch.index("x86os_wait"), launch.index("drain_input();"))
        self.assertLess(
            launch.index('x86os_puts("\\nTaste zum Desktop...")'),
            launch.index("(void)x86os_getchar();"),
        )
        self.assertLess(launch.index("(void)x86os_getchar();"), launch.index("drain_input();"))
        self.assertLess(launch.index("drain_input();"), launch.index("render_desktop"))
        drain = self.source[self.source.index("static void drain_input") :]
        self.assertIn("x86os_getchar_nonblocking()", drain)

    def test_spawn_failure_reports_its_status_before_waiting(self):
        launch = self.source[self.source.index("static void launch_app") :]
        self.assertIn('x86os_puts("Start fehlgeschlagen (Status ")', launch)
        self.assertIn("print_integer(pid);", launch)

    def test_first_render_emits_the_desktop_ready_marker(self):
        main = self.source[self.source.index("int main(void)") :]
        first_render = main.index("render_desktop(&display, selected)")
        marker = main.index('x86os_puts("DESKTOP_OK\\n")')
        self.assertLess(first_render, marker)

    def test_launcher_is_freestanding_and_has_no_host_libc_dependency(self):
        self.assertNotRegex(self.source, r"#include\s*<(stdio|stdlib|string)\.h>")
        self.assertNotRegex(self.source, r"\b(printf|strlen|memcpy|malloc|free)\s*\(")


if __name__ == "__main__":
    unittest.main()
