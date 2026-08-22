import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "userspace/gui/compositor/desktop_explorer.h"
SOURCE = ROOT / "userspace/gui/compositor/desktop_explorer.c"
DESKTOP = ROOT / "userspace/gui/compositor/desktop.c"
HOST = ROOT / "test/test_desktop_explorer_host.c"


class DesktopExplorerSourceTests(unittest.TestCase):
    def test_explorer_is_fixed_capacity_and_vfs_backed(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")
        desktop = DESKTOP.read_text(encoding="utf-8")
        self.assertIn("DESKTOP_EXPLORER_WINDOW_CAPACITY 8U", header)
        self.assertIn("DESKTOP_EXPLORER_ENTRY_CAPACITY 32U", header)
        self.assertIn("DESKTOP_EXPLORER_DOUBLE_CLICK_MS 500U", header)
        self.assertIn("x86os_readdir_batch", source)
        self.assertIn("x86os_stat", source)
        self.assertIn("if (explorer->staging_truncated) break", source)
        self.assertIn("DESKTOP_EXPLORER_DIRECTORY_PROBE_BATCHES 2U", header)
        self.assertIn("entry_is_dot_name", source)
        self.assertIn("desktop_explorer_icon_kind", source)
        self.assertIn("snapshot_generation", header)
        self.assertIn("desktop_explorer_drag_object", header)
        self.assertIn("desktop_explorer_drag_validate", header)
        self.assertIn("/usr/share/icons/program.ico", desktop)
        self.assertIn("desktop_file_icon_cache_initialize();", desktop)
        render = desktop.split("static void render_explorer_entry", 1)[1]
        render = render.split("static void render_window", 1)[0]
        self.assertIn("draw_cached_file_icon", render)
        self.assertNotRegex(
            render, r"\b(x86os_open|x86os_read|reist_image_decode|malloc)\s*\("
        )
        self.assertNotRegex(source, r"\b(malloc|calloc|realloc|free)\s*\(")

    def test_host_behavior(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "desktop-explorer-test.exe"
            subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-Iuserspace/sdk/include", "-Iuserspace/gui/compositor",
                 str(SOURCE),
                 "userspace/gui/compositor/desktop_drag.c",
                 str(HOST), "-o", str(executable)],
                cwd=ROOT, check=True, capture_output=True, text=True,
            )
            subprocess.run([str(executable)], cwd=ROOT, check=True,
                           capture_output=True, text=True)


if __name__ == "__main__":
    unittest.main()
