"""Bounded UTF-8 raster, CP437 mapping and integration contracts."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
GCC = shutil.which("gcc")


class UnicodeTextTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.utf = (ROOT / "include/reist/utf.h").read_text(encoding="utf-8")
        cls.font = (ROOT / "include/reist/unicode_vga_font.h").read_text(
            encoding="utf-8")
        cls.framebuffer = (ROOT / "drivers/video/framebuffer.c").read_text(
            encoding="utf-8")
        cls.syscalls = (ROOT / "kernel/syscall/syscall_table.c").read_text(
            encoding="utf-8")
        cls.desktop = (ROOT /
            "userspace/gui/compositor/desktop.c").read_text(encoding="utf-8")
        cls.guest = (ROOT / "userspace/programs/guest_test.c").read_text(
            encoding="utf-8")

    def test_generated_cp437_mapping_is_reproducible(self) -> None:
        subprocess.run(
            ["python", "scripts/generate_cp437_unicode.py", "--check"],
            cwd=ROOT, check=True, capture_output=True)
        self.assertIn("REIST_UNICODE_VGA_MAPPING_COUNT", self.font)
        self.assertIn("REIST_UNICODE_VGA_MISSING_GLYPH 0xFEU", self.font)
        self.assertIn("reist_unicode_vga_has_glyph", self.font)

    @unittest.skipUnless(GCC, "gcc is required for Unicode host behavior")
    def test_scalar_scan_prefix_and_glyph_behavior(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / "unicode_text_host.exe"
            subprocess.run(
                [GCC, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 f"-I{ROOT}", "test/test_unicode_text_host.c",
                 "-o", str(executable)],
                cwd=ROOT, check=True, capture_output=True)
            subprocess.run([str(executable)], cwd=ROOT, check=True,
                           capture_output=True)

    def test_kernel_preflights_before_frame_and_pixels(self) -> None:
        self.assertIn("reist_utf8_scan(text, request.text_length",
                      self.syscalls)
        for signature in ("static int syscall_display_draw_text(",
                          "static int syscall_display_draw_text_clipped("):
            start = self.syscalls.index(signature)
            end = self.syscalls.index("\n}\n", start)
            body = self.syscalls[start:end]
            self.assertLess(body.index("reist_utf8_scan("),
                            body.index("framebuffer_frame_draw_enter("))
        self.assertIn("reist_unicode_vga_glyph(scalar)", self.framebuffer)
        self.assertIn("(int64_t)scalar_count * FONT_WIDTH", self.framebuffer)

    def test_desktop_uses_scalar_prefix_without_byte_slicing(self) -> None:
        self.assertIn("reist_utf8_prefix(text, length, maximum_scalars",
                      self.desktop)
        self.assertIn("unicode_text_measure", self.desktop)
        self.assertNotIn("text + first, end - first", self.desktop)

    def test_desktop_probe_proves_valid_bytes_and_malformed_rejection(self) -> None:
        self.assertIn("TEST_STAGE UNICODE_RASTER_OK", self.guest)
        self.assertIn("--unicode-probe", self.guest)
        self.assertIn("DESKTOP_UNICODE_OK", self.desktop)
        self.assertIn("malformed_status != -22", self.desktop)
        self.assertIn("sizeof(valid) - 1U", self.desktop)


if __name__ == "__main__":
    unittest.main()
