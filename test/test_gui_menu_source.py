import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "userspace/gui/include/reist/gui/menu.h"
SOURCE = ROOT / "userspace/gui/lib/menu.c"
EXAMPLE = ROOT / "userspace/gui/examples/menu_controller.c"


class GuiMenuSourceTests(unittest.TestCase):
    def test_public_menu_model_is_versioned_fixed_and_surface_local(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")
        self.assertIn("REIST_GUI_MENU_API_VERSION 1U", header)
        self.assertIn("REIST_GUI_MENU_MAX_MENUS 8U", header)
        self.assertIn("REIST_GUI_MENU_MAX_ITEMS 16U", header)
        self.assertIn("REIST_GUI_MENU_DAMAGE_CAPACITY 4U", header)
        self.assertIn("surface_width", header)
        self.assertIn("reist_gui_menu_dispatch", header)
        self.assertIn("REIST_GUI_MENU_CAPTURE_ITEM", header)
        self.assertIn("REIST_GUI_MENU_EINVAL", header)
        self.assertIn("REIST_GUI_MENU_LAYOUT_V1_SIZE", header)
        self.assertIn("REIST_GUI_MENU_POPUP_ABOVE", header)
        self.assertIn("popup_direction", header)
        self.assertIn("layout_popup_direction", source)
        self.assertIn("extern \"C\"", header)
        self.assertIn("@param[in,out] state", header)
        self.assertIn("@return REIST_GUI_MENU_OK", header)
        self.assertIn("caller-owned surface coordinate system", header)
        self.assertNotIn("x86os", header.lower())
        self.assertNotRegex(source, r"\b(malloc|calloc|realloc|free)\s*\(")

    def test_documented_example_uses_only_the_installed_public_header(self):
        example = EXAMPLE.read_text(encoding="utf-8")
        self.assertIn("#include <reist/gui/menu.h>", example)
        self.assertIn("reist_gui_menu_validate", example)
        self.assertIn("reist_gui_menu_dispatch", example)
        self.assertNotIn("desktop_wm", example)
        self.assertNotIn("x86os", example.lower())

    def test_hot_and_pressed_items_have_exact_fixed_damage(self):
        source = SOURCE.read_text(encoding="utf-8")
        hot = source[source.index("static void set_hot_item") :]
        hot = hot[: hot.index("\n}") + 2]
        self.assertIn("add_item_damage", hot)
        self.assertNotIn("popup_rect_unchecked", hot)
        self.assertGreaterEqual(hot.count("add_item_damage"), 2)
        press = source[source.index("static void dispatch_press") :]
        press = press[: source.index("static void dispatch_release") -
                      source.index("static void dispatch_press")]
        self.assertIn("add_item_damage", press)
        self.assertNotIn("add_damage(layout, result, popup)", press)
        item_damage = source[source.index("static void add_item_damage") :]
        item_damage = item_damage[: item_damage.index("\n}") + 2]
        self.assertIn("append_damage(layout, result, rect)", item_damage)
        self.assertNotIn("add_damage(layout, result, rect)", item_damage)

    def test_menu_controller_host_behavior(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory(prefix="reist-gui-menu-") as temp:
            executable = Path(temp) / "gui-menu-test.exe"
            subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-Iuserspace/gui/include", "test/test_gui_menu_host.c",
                 "userspace/gui/lib/menu.c", "-o", str(executable)],
                cwd=ROOT, check=True, capture_output=True, text=True,
            )
            subprocess.run(
                [str(executable)], cwd=ROOT, check=True,
                capture_output=True, text=True, timeout=5,
            )


if __name__ == "__main__":
    unittest.main()
