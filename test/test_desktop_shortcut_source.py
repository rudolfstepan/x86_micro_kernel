import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "userspace/gui/compositor/desktop_shortcut.h"
SOURCE = ROOT / "userspace/gui/compositor/desktop_shortcut.c"
DESKTOP = ROOT / "userspace/gui/compositor/desktop.c"
HOST = ROOT / "test/test_desktop_shortcut_host.c"
ICON = ROOT / "assets/icons/shortcut.ico"


class DesktopShortcutSourceTests(unittest.TestCase):
    def test_sibling_contract_is_bounded_typed_and_fail_closed(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")
        combined = header + source
        for token in (
            'DESKTOP_SHORTCUT_SCHEMA "reist.shortcut/1"',
            'DESKTOP_SHORTCUT_DIRECTORY "/desktop"',
            "DESKTOP_SHORTCUT_COLLISION_LIMIT 32U",
            "DESKTOP_SHORTCUT_OPERATION_TIMEOUT_MS 10000U",
            "DESKTOP_SHORTCUT_REQUEST_TIMEOUT_MS 1000U",
            "directory_path",
            "directory_identity",
            "target_identity",
            "desktop_shortcut_prepare_directory",
            "desktop_shortcut_create",
            "desktop_shortcut_resolve",
            "directory_authority_equal",
            "canonical",
            "reist_vfs_lstat",
            "reist_vfs_file_open_flags",
            "X86OS_O_NOFOLLOW",
            "REIST_VFS_FILE_RIGHT_READ | REIST_VFS_FILE_RIGHT_STAT",
            "reist_vfs_file_fstat",
            "reist_vfs_file_read_bulk",
            "reist_vfs_file_close",
            "x86os_create",
            "x86os_write",
            "x86os_fsync",
            "x86os_close",
            "x86os_rename",
            '"schema=" DESKTOP_SHORTCUT_SCHEMA',
            '"\\nkind="',
            '"\\ntarget="',
        ):
            self.assertIn(token, combined)
        for forbidden in (
            "desktop_shortcut_state_t",
            "desktop_shortcut_refresh(",
            "desktop_shortcut_remove(",
            "reist_vfs_readdir_at",
        ):
            self.assertNotIn(forbidden, combined)
        self.assertNotRegex(source, r"\b(malloc|calloc|realloc|free)\s*\(")
        self.assertNotRegex(source, r"\b(system|popen|x86os_spawnv?)\s*\(")
        self.assertNotIn("getenv(", source)
        create = source.split("int desktop_shortcut_create(", 1)[1]
        self.assertLess(create.index("request->directory_path"),
                        create.index("x86os_create(temp_path)"))
        self.assertLess(create.index("x86os_fsync(descriptor)"),
                        create.index("x86os_rename(temp_path, final_path)"))
        self.assertLess(create.index("x86os_close(descriptor)"),
                        create.index("x86os_rename(temp_path, final_path)"))

    def test_desktop_uses_directory_snapshot_and_sibling_creation(self):
        desktop = DESKTOP.read_text(encoding="utf-8")
        explorer_h = (
            ROOT / "userspace/gui/compositor/desktop_explorer.h"
        ).read_text(encoding="utf-8")
        for token in (
            '#include "desktop_shortcut.h"',
            '"Verknuepfung erstellen"',
            "desktop_explorer_drag_validate",
            "desktop_shortcut_create(",
            "desktop_shortcut_resolve(",
            "DESKTOP_SHORTCUT_TARGET_PROGRAM",
            "desktop_filetypes_lookup",
            "launch_program(",
            '"/usr/share/icons/shortcut.ico"',
            "X86OS_MOUSE_BUTTON_RIGHT",
            "DESKTOP_SHORTCUT_CREATED",
            "DESKTOP_SHORTCUT_ACTIVATED",
        ):
            self.assertIn(token, desktop)
        for token in (
            "desktop_directory",
            "DESKTOP_EXPLORER_DESKTOP_SOURCE_ID",
            "desktop_explorer_desktop_refresh",
            "desktop_explorer_desktop_drag_object",
        ):
            self.assertIn(token, explorer_h)
        self.assertNotIn("desktop_shortcut_state_t", desktop)
        self.assertNotIn("Verknuepfung auf Desktop erstellen", desktop)
        render = desktop.split("static void render_icon", 1)[1]
        render = render.split("static void render_drag_feedback", 1)[0]
        self.assertNotRegex(
            render,
            r"\b(reist_vfs_|x86os_(open|read|stat)|reist_image_decode|malloc)\s*\(",
        )

    def test_source_icon_move_module_and_runtime_are_packaged(self):
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
            'ROOT / "userspace/gui/compositor/desktop_shortcut.c"', programs
        )
        self.assertIn(
            'ROOT / "userspace/gui/compositor/desktop_file_move.c"', programs
        )
        self.assertIn(
            "usr/share/icons/shortcut.ico=assets/icons/shortcut.ico", makefile
        )
        self.assertIn("'shortcut'", windows)
        self.assertIn("runtime-desktop-shortcuts", runtime)
        self.assertIn("--shortcut-probe", runner)
        for token in (
            '"usb-mouse,bus=reistxhci.0"',
            "DESKTOP_SHORTCUT_SIBLING_OK",
            "DESKTOP_SHORTCUT_DESKTOP_UNCHANGED",
            "DESKTOP_FILE_MOVE_OK",
            "DESKTOP_SHORTCUT_STORAGE_RELOAD_OK",
            "DESKTOP_SHORTCUT_ACTIVATED",
            '"CLOSE", "client"',
            "DESKTOP_SHORTCUT_CLIENT_CLOSED",
            'print("runtime-desktop-shortcuts: PASS")',
        ):
            self.assertIn(token, runner)
        self.assertTrue(ICON.is_file())
        payload = ICON.read_bytes()
        self.assertGreaterEqual(len(payload), 70)
        reserved, image_type, count = struct.unpack_from("<HHH", payload, 0)
        self.assertEqual((reserved, image_type, count), (0, 1, 1))
        width, height, colors, reserved_entry = struct.unpack_from(
            "<BBBB", payload, 6
        )
        self.assertEqual((width, height, colors, reserved_entry), (32, 32, 0, 0))
        size, offset = struct.unpack_from("<II", payload, 14)
        self.assertEqual(offset, 22)
        self.assertEqual(size, len(payload) - offset)

    def test_host_behavior(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory(prefix="reist-desktop-shortcut-") as temp:
            executable = Path(temp) / "desktop-shortcut-test.exe"
            subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I.",
                    "-Iuserspace/sdk/include",
                    "-Iuserspace/storage/include",
                    "-Iuserspace/gui/compositor",
                    str(HOST),
                    str(SOURCE),
                    "-o",
                    str(executable),
                ],
                cwd=ROOT,
                check=True,
                capture_output=True,
                text=True,
            )
            subprocess.run(
                [str(executable)], cwd=ROOT, check=True,
                capture_output=True, text=True, timeout=10,
            )


if __name__ == "__main__":
    unittest.main()
