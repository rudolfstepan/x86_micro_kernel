import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "userspace/gui/compositor/desktop_file_move.h"
SOURCE = ROOT / "userspace/gui/compositor/desktop_file_move.c"
HOST = ROOT / "test/test_desktop_file_move_host.c"


class DesktopFileMoveSourceTests(unittest.TestCase):
    def test_move_contract_is_bounded_verified_and_ring3(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")
        combined = header + source
        for token in (
            "DESKTOP_FILE_MOVE_CHUNK_CAPACITY 1024U",
            "DESKTOP_FILE_MOVE_TEMP_ATTEMPTS 32U",
            "DESKTOP_FILE_MOVE_MAX_BYTES",
            "DESKTOP_FILE_MOVE_TIMEOUT_MS 30000U",
            "DESKTOP_FILE_MOVE_EPARTIAL",
            "source_directory_identity",
            "destination_directory_identity",
            "destination_published",
            "source_removed",
            "duplicate_retained",
            "desktop_file_move_source_allowed",
            "desktop_file_move_destination_allowed",
            "desktop_file_move_execute",
            "directory_authority_equal",
            "reist_vfs_lstat",
            "reist_vfs_file_open_flags",
            "X86OS_O_NOFOLLOW",
            "reist_vfs_file_fstat",
            "reist_vfs_file_read_bulk",
            "x86os_create",
            "x86os_write",
            "x86os_fsync",
            "readback_temp",
            "x86os_rename",
            "x86os_unlink(request->source_path)",
        ):
            self.assertIn(token, combined)
        self.assertNotRegex(source, r"\b(malloc|calloc|realloc|free)\s*\(")
        self.assertNotIn("x86os_open(", source)
        execute = source.split("int desktop_file_move_execute(", 1)[1]
        self.assertLess(execute.index("path_absent(destination"),
                        execute.index("x86os_create(temporary)"))
        self.assertLess(execute.index("x86os_fsync(descriptor)"),
                        execute.index("readback_temp("))
        self.assertLess(execute.index("readback_temp("),
                        execute.index("x86os_rename(temporary, destination)"))
        self.assertLess(execute.index("x86os_rename(temporary, destination)"),
                        execute.index("x86os_unlink(request->source_path)"))

    def test_private_move_temps_are_hidden_from_explorer(self):
        explorer = (
            ROOT / "userspace/gui/compositor/desktop_explorer.c"
        ).read_text(encoding="utf-8")
        self.assertIn("entry_is_move_temporary_name", explorer)
        self.assertIn("entry_is_hidden_name", explorer)

    def test_host_behavior(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory(prefix="reist-desktop-move-") as temp:
            executable = Path(temp) / "desktop-file-move-test.exe"
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
