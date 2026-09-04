import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / "userspace/gui/apps/browser/main.c"
PARSER = ROOT / "userspace/gui/lib/html_document.c"
HEADER = ROOT / "userspace/gui/include/reist/gui/html_document.h"


class GuiBrowserSourceTests(unittest.TestCase):
    def test_parser_is_fixed_capacity_and_passes_host_cases(self):
        header = HEADER.read_text(encoding="utf-8")
        source = PARSER.read_text(encoding="utf-8")
        for contract in (
            "REIST_HTML_INPUT_CAPACITY 65536U",
            "REIST_HTML_ELEMENT_CAPACITY 512U",
            "REIST_HTML_LINK_CAPACITY 128U",
            "REIST_HTML_NESTING_CAPACITY 32U",
            "raw_end_tag_at",
            "REIST_HTML_ENCODING",
        ):
            self.assertIn(contract, header + source)
        self.assertNotRegex(source, r"\b(malloc|calloc|realloc|free)\s*\(")
        compiler = shutil.which("gcc") or shutil.which("clang")
        compiler_command = [compiler] if compiler is not None else []
        if not compiler_command:
            zig = Path(r"C:\tools\zig-x86_64-windows-0.16.0\zig.exe")
            if zig.is_file():
                compiler_command = [str(zig), "cc"]
        if not compiler_command:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / "html-document-host.exe"
            environment = os.environ.copy()
            environment["ZIG_LOCAL_CACHE_DIR"] = str(
                Path(temporary) / "zig-local")
            environment["ZIG_GLOBAL_CACHE_DIR"] = str(
                Path(temporary) / "zig-global")
            subprocess.run([*compiler_command,
                "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-Iuserspace/gui/include",
                "userspace/gui/lib/html_document.c",
                "test/test_html_document_host.c", "-o", str(executable),
            ], cwd=ROOT, check=True, env=environment)
            subprocess.run([str(executable)], cwd=ROOT, check=True,
                           env=environment)

    def test_browser_is_a_bounded_surface_client(self):
        source = APP.read_text(encoding="utf-8")
        for contract in (
            "reist_gui_surface_endpoint_from_argv",
            "reist_gui_surface_client_create",
            "reist_gui_surface_client_paint_font_text",
            "REIST_GUI_SURFACE_INPUT_KEYBOARD",
            "BROWSER_DOCUMENT_LIMIT 65536U",
            "BROWSER_LAYOUT_LINE_CAPACITY",
            "BROWSER_LINK_HIT_CAPACITY",
            "reist_html_document_parse",
        ):
            self.assertIn(contract, source)
        self.assertNotRegex(source, r"\b(malloc|calloc|realloc|free)\s*\(")

    def test_transport_and_vfs_authority_are_separated(self):
        source = APP.read_text(encoding="utf-8")
        self.assertIn('"/usr/bin/curl.prg"', source)
        self.assertIn('"--max-bytes"', source)
        self.assertIn('"65536"', source)
        self.assertIn("x86os_spawnv", source)
        self.assertIn("x86os_wait", source)
        self.assertIn("reist_vfs_file_open_rights", source)
        self.assertIn(
            "REIST_VFS_FILE_RIGHT_READ | REIST_VFS_FILE_RIGHT_STAT", source)
        self.assertIn("reist_vfs_file_close", source)
        for forbidden in ("x86os_socket(", "reist_tls_", "mbedtls_",
                          "wolfSSL_"):
            self.assertNotIn(forbidden, source)

    def test_javascript_extension_is_out_of_process(self):
        queue = (ROOT / "automation/reist-s03b.toml").read_text(
            encoding="utf-8")
        self.assertIn("later versioned IPC adapter", queue)
        self.assertIn("separate quota-bound generation-scoped Ring-3", queue)

    def test_browser_is_packaged_and_owns_html_associations(self):
        associations = (ROOT / "config/etc/reist/filetypes.conf").read_text(
            encoding="utf-8")
        self.assertIn(".html=/usr/gui/bin/browser.prg", associations)
        self.assertIn(".htm=/usr/gui/bin/browser.prg", associations)
        for path in (ROOT / "Makefile", ROOT / "scripts/build-windows.ps1"):
            self.assertIn("usr/gui/bin/browser.prg",
                          path.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
