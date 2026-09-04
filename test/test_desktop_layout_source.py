import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "userspace/gui/compositor/desktop_layout.h"
SOURCE = ROOT / "userspace/gui/compositor/desktop_layout.c"
DESKTOP = ROOT / "userspace/gui/compositor/desktop.c"
HOST = ROOT / "test/test_desktop_layout_host.c"


class DesktopLayoutSourceTests(unittest.TestCase):
    def test_layout_contract_is_fixed_versioned_and_heap_free(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")
        combined = header + source
        for token in (
            'DESKTOP_LAYOUT_SCHEMA "reist.desktop-layout/1"',
            'DESKTOP_LAYOUT_PATH "/etc/reist/desktop-layout.conf"',
            "DESKTOP_LAYOUT_ENTRY_CAPACITY",
            "DESKTOP_LAYOUT_FILE_CAPACITY",
            "DESKTOP_LAYOUT_SEARCH_CAPACITY",
            "desktop_layout_document_t",
            "desktop_layout_view_t",
            "desktop_layout_parse",
            "desktop_layout_serialize",
            "desktop_layout_resolve",
            "desktop_layout_drop",
            "desktop_layout_move_document",
        ):
            self.assertIn(token, combined)
        self.assertNotRegex(source, r"\b(malloc|calloc|realloc|free)\s*\(")
        self.assertNotRegex(source, r"\b(reist_vfs_|x86os_)\w*\s*\(")

    def test_desktop_integrates_one_layout_view_and_durable_publish(self):
        desktop = DESKTOP.read_text(encoding="utf-8")
        for token in (
            '#include "desktop_layout.h"',
            "DESKTOP_DRAG_OPERATION_LAYOUT",
            "desktop_layout_rebuild",
            "desktop_layout_drop_target",
            "desktop_layout_store",
            "x86os_fsync(descriptor)",
            "x86os_rename(DESKTOP_LAYOUT_TEMP_PATH, DESKTOP_LAYOUT_PATH)",
            "&desktop_layout_document, candidate",
            '"DESKTOP_ICON_LAYOUT_DROP_OK',
            '"DESKTOP_ICON_LAYOUT_RELOAD_OK',
        ):
            self.assertIn(token, desktop)
        render = desktop.split("static void render_icon", 1)[1]
        render = render.split("static void render_drag_feedback", 1)[0]
        self.assertNotRegex(
            render,
            r"\b(reist_vfs_|x86os_(open|read|stat|write|fsync|rename))\w*\s*\(",
        )
        store = desktop.split("static int desktop_layout_store", 1)[1]
        store = store.split("static ", 1)[0]
        self.assertLess(store.index("x86os_fsync(descriptor)"),
                        store.index("x86os_rename("))
        self.assertLess(store.index("x86os_close(descriptor)"),
                        store.index("x86os_rename("))
        self.assertLess(store.index("x86os_rename("),
                        store.index("&desktop_layout_document, candidate"))

    def test_production_and_runtime_wiring_are_present(self):
        programs = (ROOT / "scripts/build_system_programs.py").read_text(
            encoding="utf-8"
        )
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        windows = (ROOT / "scripts/build-windows.ps1").read_text(
            encoding="utf-8"
        )
        runtime = (ROOT / "scripts/test-reist-runtime.ps1").read_text(
            encoding="utf-8"
        )
        runner = (ROOT / "scripts/run_qemu_runtime_desktop.py").read_text(
            encoding="utf-8"
        )
        self.assertIn(
            'ROOT / "userspace/gui/compositor/desktop_layout.c"', programs
        )
        self.assertIn(
            "etc/reist/desktop-layout.conf=config/etc/reist/desktop-layout.conf",
            makefile,
        )
        self.assertIn("'desktop-layout.conf'", windows)
        self.assertIn("runtime-desktop-icon-layout", runtime)
        self.assertIn("--icon-layout-probe", runner)
        for marker in (
            "DESKTOP_ICON_LAYOUT_READY",
            "DESKTOP_ICON_LAYOUT_DROP_OK",
            "DESKTOP_ICON_LAYOUT_RELOAD_OK",
            "DESKTOP_ICON_LAYOUT_RESIZE_OK",
            "DESKTOP_ICON_LAYOUT_ACTIVATED",
            'print("runtime-desktop-icon-layout: PASS")',
        ):
            self.assertIn(marker, runner)

    def test_host_behavior(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory(prefix="reist-desktop-layout-") as temp:
            executable = Path(temp) / "desktop-layout-test.exe"
            subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-I.", str(HOST), str(SOURCE), "-o", str(executable)],
                cwd=ROOT, check=True, capture_output=True, text=True,
            )
            subprocess.run(
                [str(executable)], cwd=ROOT, check=True,
                capture_output=True, text=True, timeout=5,
            )


if __name__ == "__main__":
    unittest.main()
