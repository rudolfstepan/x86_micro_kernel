import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class GuiControlPanelSourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = (ROOT / "userspace/gui/apps/control_panel/main.c").read_text(
            encoding="utf-8")
        cls.desktop = (ROOT / "userspace/gui/compositor/desktop.c").read_text(
            encoding="utf-8")

    def test_control_panel_is_a_separate_surface_client(self):
        self.assertIn('#include "reist/gui/surface_client.h"', self.source)
        self.assertIn('#include "reist/vfs_file_client.h"', self.source)
        self.assertIn("reist_gui_surface_endpoint_from_argv", self.source)
        self.assertIn("REIST_GUI_SURFACE_ROLE_TOPLEVEL", self.source)
        self.assertIn("reist_gui_surface_client_set_title", self.source)
        self.assertIn("Systemsteuerung", self.source)
        self.assertNotIn("x86os_display_activate", self.source)
        self.assertNotRegex(self.source, r"\b(malloc|calloc|realloc|free)\s*\(")

    def test_four_applets_support_pointer_and_keyboard(self):
        for label in ("Tastatur", "Maus", "System", "Desktop"):
            self.assertIn(label, self.source)
        self.assertIn("CONTROL_PANEL_APPLET_COUNT 4U", self.source)
        self.assertIn("handle_pointer", self.source)
        self.assertIn("handle_keyboard", self.source)
        self.assertIn("CONTROL_PANEL_KEY_LEFT", self.source)
        self.assertIn("CONTROL_PANEL_KEY_RIGHT", self.source)
        self.assertIn("CONTROL_PANEL_KEY_ENTER", self.source)

    def test_idle_surface_poll_does_not_close_the_window(self):
        self.assertIn(
            "reist_gui_surface_client_receive(&client, &message, 0U)",
            self.source,
        )
        self.assertIn("if (status == -11)", self.source)
        self.assertIn("x86os_sleep_ms(5U)", self.source)
        self.assertNotIn(
            "reist_gui_surface_client_receive(&client, &message, 50U)",
            self.source,
        )

    def test_mutation_crosses_the_config_service_process_boundary(self):
        self.assertIn('"/sbin/config.prg"', self.source)
        self.assertIn("x86os_spawnv", self.source)
        self.assertIn("x86os_wait", self.source)
        self.assertIn("Nur-Lese-Modus", self.source)

    def test_configuration_reads_use_minimal_ring3_objects(self):
        start = self.source.index("static int read_config(")
        end = self.source.index("static void load_applet", start)
        read_config = self.source[start:end]
        self.assertIn("reist_vfs_file_open_rights(", read_config)
        self.assertIn(
            "REIST_VFS_FILE_RIGHT_READ | REIST_VFS_FILE_RIGHT_STAT",
            read_config)
        self.assertIn("reist_vfs_file_fstat(handle, &info)", read_config)
        self.assertIn("reist_vfs_file_read(", read_config)
        self.assertIn("reist_vfs_file_close(handle)", read_config)
        self.assertIn("info.size > sizeof(config_buffer)", read_config)
        for legacy in ("x86os_open(", "x86os_read(", "x86os_close("):
            self.assertNotIn(legacy, read_config)
        self.assertLess(read_config.rindex("reist_vfs_file_close(handle)"),
                        read_config.index("reist_config_parse("))

    def test_desktop_has_a_distinct_control_panel_icon_and_surface_launch(self):
        self.assertIn('{"Systemsteuerung", "/usr/gui/bin/control.prg"',
                      self.desktop)
        self.assertIn('path_equal_ascii_case(program, "/usr/gui/bin/control.prg")',
                      self.desktop)
        self.assertIn("control_panel_activate", self.desktop)
        self.assertIn("#define DESKTOP_ICON_WIDTH 176U", self.desktop)

    def test_programs_are_packaged_in_both_image_builds(self):
        programs = (ROOT / "scripts/build_system_programs.py").read_text(
            encoding="utf-8")
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        windows = (ROOT / "scripts/build-windows.ps1").read_text(
            encoding="utf-8")
        for program in ("CONTROL.PRG", "CONFIG.PRG"):
            self.assertIn(f'"{program}"', programs)
        self.assertIn("vfs_file_client.c", programs)
        self.assertIn("usr/gui/bin/control.prg=", makefile)
        self.assertIn("sbin/config.prg=", makefile)
        self.assertIn("'usr/gui/bin/control.prg'", windows)
        self.assertIn("'sbin/config.prg'", windows)


if __name__ == "__main__":
    unittest.main()
