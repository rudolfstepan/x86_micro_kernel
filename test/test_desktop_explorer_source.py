import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "userspace/gui/compositor/desktop_explorer.h"
SOURCE = ROOT / "userspace/gui/compositor/desktop_explorer.c"
HOST = ROOT / "test/test_desktop_explorer_host.c"


class DesktopExplorerSourceTests(unittest.TestCase):
    def test_explorer_is_fixed_capacity_and_vfs_backed(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")
        self.assertIn("DESKTOP_EXPLORER_WINDOW_CAPACITY 8U", header)
        self.assertIn("DESKTOP_EXPLORER_ENTRY_CAPACITY 32U", header)
        self.assertIn("DESKTOP_EXPLORER_DOUBLE_CLICK_MS 500U", header)
        self.assertIn("x86os_readdir_batch", source)
        self.assertIn("x86os_stat", source)
        self.assertIn("if (explorer->staging_truncated) break", source)
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
                 str(SOURCE), str(HOST), "-o", str(executable)],
                cwd=ROOT, check=True, capture_output=True, text=True,
            )
            subprocess.run([str(executable)], cwd=ROOT, check=True,
                           capture_output=True, text=True)


if __name__ == "__main__":
    unittest.main()
