import shutil
import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "userspace/gui/compositor/desktop_filetypes.c"
HEADER = ROOT / "userspace/gui/compositor/desktop_filetypes.h"


class DesktopFiletypesSourceTests(unittest.TestCase):
    def test_parser_is_fixed_capacity_and_has_no_process_authority(self):
        source = SOURCE.read_text(encoding="utf-8")
        header = HEADER.read_text(encoding="utf-8")
        self.assertIn("DESKTOP_FILETYPES_CAPACITY 16U", header)
        self.assertIn("DESKTOP_FILETYPES_CONFIG_CAPACITY 4096U", header)
        self.assertIn("desktop_filetypes_parse", source)
        self.assertIn("desktop_filetypes_lookup", source)
        self.assertNotRegex(source, r"\b(malloc|calloc|realloc|free)\s*\(")
        self.assertNotIn("x86os_spawn", source)
        self.assertNotIn("x86os_open", source)

    def test_host_contract(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        output = ROOT / "build/desktop-filetypes-test.exe"
        output.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(
            [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
             "-Iuserspace/gui/compositor",
             "test/test_desktop_filetypes_host.c",
             "userspace/gui/compositor/desktop_filetypes.c",
             "-o", str(output)],
            cwd=ROOT, check=True, capture_output=True, text=True,
        )
        subprocess.run([str(output)], cwd=ROOT, check=True)


if __name__ == "__main__":
    unittest.main()
