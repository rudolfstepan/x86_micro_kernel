import os
import hashlib
import struct
import zlib
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / "userspace/gui/apps/browser/main.c"
PARSER = ROOT / "userspace/gui/lib/html_document.c"
HEADER = ROOT / "userspace/gui/include/reist/gui/html_document.h"


def run_host(sources, arguments=(), flags=()):
    compiler = shutil.which("gcc") or shutil.which("clang")
    command = [compiler] if compiler else [r"C:\tools\zig-x86_64-windows-0.16.0\zig.exe", "cc"]
    environment = os.environ.copy()
    environment["ZIG_LOCAL_CACHE_DIR"] = str(ROOT / "build/codex-agent/browser-host/zig-local")
    environment["ZIG_GLOBAL_CACHE_DIR"] = str(ROOT / "build/codex-agent/browser-host/zig-global")
    with tempfile.TemporaryDirectory() as directory:
        executable = Path(directory) / "browser-host.exe"
        subprocess.run([*command, "-std=c11", "-O1", "-UNDEBUG", "-Wall", "-Wextra", "-Werror",
                        "-Iuserspace/gui/include", "-Iuserspace/image/include",
                        "-Iuserspace/gui/apps/browser", *flags, *sources, "-o", str(executable)],
                       cwd=ROOT, env=environment, check=True, timeout=90)
        subprocess.run([str(executable), *map(str, arguments)], cwd=ROOT,
                       env=environment, check=True, timeout=40)


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
                "-std=c11", "-O1", "-UNDEBUG", "-Wall", "-Wextra", "-Werror",
                "-Iuserspace/gui/include",
                "userspace/gui/lib/html_document.c",
                "test/test_html_document_host.c", "-o", str(executable),
            ], cwd=ROOT, check=True, env=environment, timeout=90)
            subprocess.run([str(executable)], cwd=ROOT, check=True,
                           env=environment, timeout=40)

    def test_browser_is_a_bounded_surface_client(self):
        source = APP.read_text(encoding="utf-8")
        for contract in (
            "reist_gui_surface_endpoint_from_argv",
            "reist_gui_surface_client_create",
            "reist_gui_surface_client_paint_font_text",
            "REIST_GUI_SURFACE_INPUT_KEYBOARD",
            "BROWSER_DOCUMENT_LIMIT 65536U",
            "browser_model.h",
            "BROWSER_LINK_HIT_CAPACITY",
            "reist_html_document_parse",
            "reist_html_navigation_normalize",
            "address_replace_pending",
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

    def test_model_host_behavior(self):
        run_host(["userspace/gui/apps/browser/browser_model.c",
                  "userspace/gui/lib/html_document.c", "userspace/gui/lib/value_controls.c",
                  "test/test_browser_model_host.c"])

    def test_image_formats_corruption_and_capacity(self):
        def chunk(kind, data):
            return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
        png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 1, 8, 6, 0, 0, 0)) +
               chunk(b"IDAT", zlib.compress(b"\0\xff\0\0\xff\0\xff\0\x80")) + chunk(b"IEND", b""))
        # Minimal standards-based grayscale JPEG: DC=0 and EOB, neutral sample 128.
        def marker(kind, payload):
            return bytes([255, kind]) + struct.pack(">H", len(payload) + 2) + payload
        huffman = bytes([1] + [0] * 15 + [0])
        jpeg = (b"\xff\xd8" + marker(0xDB, b"\0" + bytes([1] * 64)) +
                marker(0xC0, bytes([8, 0, 1, 0, 1, 1, 1, 0x11, 0])) +
                marker(0xC4, b"\0" + huffman + b"\x10" + huffman) +
                marker(0xDA, bytes([1, 1, 0, 0, 63, 0])) + b"\x3f\xff\xd9")
        bmp = (b"BM" + struct.pack("<IHHI", 58, 0, 0, 54) +
               struct.pack("<IiiHHIIiiII", 40, 1, 1, 1, 24, 0, 4, 0, 0, 0, 0) + b"\xff\0\0\0")
        with tempfile.TemporaryDirectory() as directory:
            paths = []
            for name, data in (("red.png", png), ("gray.jpg", jpeg), ("blue.bmp", bmp)):
                path = Path(directory) / name
                path.write_bytes(data)
                paths.append(path)
            paths.append(ROOT / "assets/images/demo-colors.gif")
            run_host(["userspace/gui/apps/browser/browser_images.c", "userspace/image/lib/image.c", "userspace/image/lib/image_ico.c",
                      "test/test_browser_images_host.c"], paths)

    def test_decoder_is_pinned_and_browser_only(self):
        vendor = ROOT / "third_party/stb_image.h"
        digest = hashlib.sha256(vendor.read_bytes().replace(b"\r\n", b"\n")).hexdigest()
        self.assertEqual(digest, (ROOT / "third_party/stb_image.sha256").read_text().split()[0])
        adapter = (APP.parent / "browser_images.c").read_text()
        for contract in ("BROWSER_DECODE_ARENA_BYTES", "STBI_ONLY_PNG", "STBI_ONLY_JPEG",
                         "STBI_NO_STDIO", "STBI_MAX_DIMENSIONS", "png_envelope"):
            self.assertIn(contract, adapter)

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
