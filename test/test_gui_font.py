"""PSF2 Ring-3 font parser, asset and integration contracts."""

from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
GCC = shutil.which("gcc")


class GuiFontTests(unittest.TestCase):
    def test_generated_font_is_reproducible_and_standard_psf2(self) -> None:
        subprocess.run(
            ["python", "scripts/generate_psf2_font.py", "--check"],
            cwd=ROOT, check=True, capture_output=True)
        data = (ROOT / "assets/fonts/reist-vga.psf").read_bytes()
        header = struct.unpack_from("<8I", data)
        self.assertEqual(header, (0x864AB572, 0, 32, 1, 257, 16, 16, 8))
        self.assertLess(len(data), 16384)

    @unittest.skipUnless(GCC, "gcc is required for PSF2 host behavior")
    def test_parser_lookup_raster_and_fail_closed_behavior(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / "gui_font_host.exe"
            subprocess.run(
                [GCC, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 f"-I{ROOT}", f"-I{ROOT / 'userspace/gui/include'}",
                 "test/test_gui_font_host.c", "userspace/gui/lib/font.c",
                 "-o", str(executable)], cwd=ROOT, check=True,
                capture_output=True)
            subprocess.run(
                [str(executable), "assets/fonts/reist-vga.psf"],
                cwd=ROOT, check=True, capture_output=True)

    def test_font_library_is_ring3_fixed_storage(self) -> None:
        source = (ROOT / "userspace/gui/lib/font.c").read_text(encoding="utf-8")
        header = (ROOT / "userspace/gui/include/reist/gui/font.h").read_text(
            encoding="utf-8")
        self.assertIn("REIST_GUI_FONT_PSF2_MAGIC", header)
        self.assertIn("reist_gui_font_mapping_t *mappings", header)
        self.assertIn("PSF2_SEQUENCE", source)
        for forbidden in ("malloc(", "calloc(", "realloc(", "fopen(",
                          "x86os_", "framebuffer_"):
            self.assertNotIn(forbidden, source)

    def test_font_is_in_both_full_image_layouts(self) -> None:
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        windows = (ROOT / "scripts/build-windows.ps1").read_text(
            encoding="utf-8")
        spec = "usr/share/fonts/reist-vga.psf"
        self.assertIn(spec, makefile)
        self.assertIn(spec, windows)

    def test_desktop_loads_and_overlays_extension_glyphs(self) -> None:
        desktop = (ROOT / "userspace/gui/compositor/desktop.c").read_text(
            encoding="utf-8")
        self.assertIn("reist_gui_font_open_psf2", desktop)
        self.assertIn("reist_gui_font_raster_xrgb_region", desktop)
        self.assertIn(
            "glyph_index >= DESKTOP_FONT_EXTENSION_FIRST_GLYPH", desktop)
        self.assertIn("intersect_rects", desktop)
        self.assertIn("DESKTOP_FONT_FALLBACK_OK", desktop)


if __name__ == "__main__":
    unittest.main()
