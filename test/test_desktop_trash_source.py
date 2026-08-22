import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "userspace/gui/compositor/desktop_trash.h"
SOURCE = ROOT / "userspace/gui/compositor/desktop_trash.c"
DESKTOP = ROOT / "userspace/gui/compositor/desktop.c"


class DesktopTrashSourceTests(unittest.TestCase):
    def test_trash_contract_is_recoverable_bounded_and_fail_closed(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")
        desktop = DESKTOP.read_text(encoding="utf-8")
        for token in (
            "DESKTOP_TRASH_FILES_PATH", "DESKTOP_TRASH_INFO_PATH",
            "DESKTOP_TRASH_STORAGE_PREFIX", "catalog_path",
            "DESKTOP_TRASH_COLLISION_LIMIT", "desktop_trash_move",
            "desktop_trash_restore", "desktop_trash_empty",
            "DESKTOP_TRASH_DELETE_DEPTH_LIMIT",
            "DESKTOP_TRASH_DELETE_ENTRY_LIMIT",
            "desktop_trash_refresh", "desktop_trash_source_allowed",
            "x86os_create", "x86os_rename", "x86os_fsync", "DeletionDate=",
            "x86os_open", "x86os_read", "x86os_rmdir",
            "[Trash Info]", "Version=2", "StoragePath=",
        ):
            self.assertIn(token, header + source)
        self.assertNotIn("x86os_unlink(request->source_path", source)
        self.assertIn("build_storage_path", source)
        self.assertNotRegex(source, r"\b(malloc|calloc|realloc|free)\s*\(")
        self.assertIn('"Papierkorb"', desktop)
        self.assertIn("DESKTOP_DRAG_FEEDBACK_VALID", desktop)
        self.assertIn("render_drag_feedback", desktop)
        self.assertIn('"Wiederherstellen"', desktop)
        self.assertIn('"Papierkorb leeren"', desktop)
        self.assertIn("X86OS_MOUSE_BUTTON_RIGHT", desktop)
        self.assertIn("DESKTOP_DIALOG_EMPTY_TRASH", desktop)
        self.assertIn("REIST_GUI_DIALOG_ROLE_DESTRUCTIVE", desktop)

    def test_trash_sources_and_icons_are_packaged_in_both_images(self):
        programs = (ROOT / "scripts/build_system_programs.py").read_text(
            encoding="utf-8"
        )
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        windows = (ROOT / "scripts/build-windows.ps1").read_text(
            encoding="utf-8"
        )
        self.assertIn('ROOT / "userspace/gui/compositor/desktop_drag.c"',
                      programs)
        self.assertIn('ROOT / "userspace/gui/compositor/desktop_trash.c"',
                      programs)
        for name in ("trash-empty", "trash-full"):
            self.assertIn(f"usr/share/icons/{name}.ico", makefile)
            self.assertIn(f"'{name}'", windows)

    def test_trash_host_behavior(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory(prefix="reist-desktop-trash-") as temp:
            executable = Path(temp) / "desktop-trash-test.exe"
            subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-I.", "-Iuserspace/sdk/include",
                 "test/test_desktop_trash_host.c",
                 "userspace/gui/compositor/desktop_trash.c",
                 "-o", str(executable)],
                cwd=ROOT, check=True, capture_output=True, text=True,
            )
            subprocess.run(
                [str(executable)], cwd=ROOT, check=True,
                capture_output=True, text=True, timeout=5,
            )


if __name__ == "__main__":
    unittest.main()
