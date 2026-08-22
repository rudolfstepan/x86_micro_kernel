import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "userspace/gui/compositor/desktop_drag.h"
SOURCE = ROOT / "userspace/gui/compositor/desktop_drag.c"


class DesktopDragSourceTests(unittest.TestCase):
    def test_drag_contract_is_fixed_generic_and_heap_free(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")
        for token in (
            "DESKTOP_DRAG_OBJECT_FILE", "DESKTOP_DRAG_OBJECT_TEXT",
            "DESKTOP_DRAG_OBJECT_IMAGE", "DESKTOP_DRAG_OBJECT_APPLICATION",
            "DESKTOP_DRAG_OPERATION_MOVE", "DESKTOP_DRAG_OPERATION_COPY",
            "DESKTOP_DRAG_OPERATION_LINK", "desktop_drag_object_t",
            "desktop_drag_target_t", "desktop_drag_drop",
        ):
            self.assertIn(token, header)
        self.assertIn("DESKTOP_DRAG_DATA_CAPACITY", header)
        self.assertIn("DESKTOP_DRAG_FEEDBACK_VALID", header)
        self.assertNotRegex(source, r"\b(malloc|calloc|realloc|free)\s*\(")

    def test_drag_host_behavior(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory(prefix="reist-desktop-drag-") as temp:
            executable = Path(temp) / "desktop-drag-test.exe"
            subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-I.", "test/test_desktop_drag_host.c",
                 "userspace/gui/compositor/desktop_drag.c",
                 "-o", str(executable)],
                cwd=ROOT, check=True, capture_output=True, text=True,
            )
            subprocess.run(
                [str(executable)], cwd=ROOT, check=True,
                capture_output=True, text=True, timeout=5,
            )


if __name__ == "__main__":
    unittest.main()
