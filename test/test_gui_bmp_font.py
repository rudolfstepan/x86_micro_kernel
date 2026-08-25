"""Broad, bounded GNU Unifont all-plane fallback contracts."""

from __future__ import annotations

import gzip
import hashlib
from pathlib import Path
import struct
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets/fonts/source/unifont_all-16.0.04.hex.gz"
FONT = ROOT / "assets/fonts/reist-unicode.psf"
SOURCE_SHA256 = "20e8b505f602488697979eefc69857f7f6106bceab702f5ac559f4f84e0e7494"
SAMPLES = (0x0041, 0x0416, 0x05D0, 0x0627, 0x0915, 0x4E2D, 0xAC00,
           0x10348, 0x1D11E, 0x1F600, 0x1F680, 0x20000)


def source_glyphs() -> list[tuple[int, bytes]]:
    result = []
    for line in gzip.decompress(SOURCE.read_bytes()).decode("ascii").splitlines():
        scalar, bitmap = line.split(":", 1)
        result.append((int(scalar, 16), bytes.fromhex(bitmap)))
    return result


class GuiUnicodeFontTests(unittest.TestCase):
    def test_source_identity_license_and_deterministic_generation(self) -> None:
        self.assertEqual(hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
                         SOURCE_SHA256)
        license_text = (ROOT / "assets/fonts/source/OFL-1.1.txt").read_text(
            encoding="utf-8")
        self.assertIn("SIL OPEN FONT LICENSE Version 1.1", license_text)
        subprocess.run(
            ["python", "scripts/generate_unifont_psf2.py", "--check"],
            cwd=ROOT, check=True, capture_output=True)

    def test_psf2_has_exact_bounded_source_coverage(self) -> None:
        glyphs = source_glyphs()
        data = FONT.read_bytes()
        header = struct.unpack_from("<8I", data)
        self.assertEqual(header, (0x864AB572, 0, 32, 1,
                                  126086, 16, 16, 8))
        self.assertEqual(len(glyphs), 126086)
        self.assertEqual(len({scalar for scalar, _ in glyphs}), 126086)
        self.assertEqual(sum(scalar <= 0xFFFF for scalar, _ in glyphs),
                         60518)
        self.assertEqual(sum(scalar > 0xFFFF for scalar, _ in glyphs),
                         65568)
        self.assertTrue(all(not 0xD800 <= scalar <= 0xDFFF
                            for scalar, _ in glyphs))
        self.assertLessEqual(max(scalar for scalar, _ in glyphs), 0x10FFFF)
        self.assertLess(len(data), 3 * 1024 * 1024)
        table = data[32 + 126086 * 16:]
        expected = b"".join(
            chr(scalar).encode("utf-8") + b"\xFF" for scalar, _ in glyphs)
        self.assertEqual(table, expected)

    def test_representative_scripts_have_real_rasters(self) -> None:
        glyphs = source_glyphs()
        indices = {scalar: index for index, (scalar, _) in enumerate(glyphs)}
        data = FONT.read_bytes()
        for scalar in SAMPLES:
            raster = data[32 + indices[scalar] * 16:
                          32 + (indices[scalar] + 1) * 16]
            self.assertNotEqual(raster, bytes(16), f"U+{scalar:04X}")
        wide = dict(glyphs)[0x4E2D]
        expected = bytes(
            sum((0x80 >> x) for x in range(8)
                if int.from_bytes(wide[row:row + 2], "big") &
                (0xC000 >> (x * 2)))
            for row in range(0, 32, 2))
        cjk_index = indices[0x4E2D]
        self.assertEqual(data[32 + cjk_index * 16:
                              32 + (cjk_index + 1) * 16], expected)

    def test_desktop_uses_fixed_all_plane_fallback_after_cp437(self) -> None:
        desktop = (ROOT / "userspace/gui/compositor/desktop.c").read_text(
            encoding="utf-8")
        self.assertIn("DESKTOP_FONT_FILE_CAPACITY (3U * 1024U * 1024U)",
                      desktop)
        self.assertIn("DESKTOP_FILE_READ_CHUNK 24576U", desktop)
        self.assertIn("DESKTOP_FILE_READ_PAUSE_MS 1U", desktop)
        bounded_read = desktop[desktop.index("static int read_file_bounded"):
                               desktop.index("static int desktop_font_load")]
        self.assertIn("remaining < DESKTOP_FILE_READ_CHUNK", bounded_read)
        self.assertIn("x86os_read(descriptor, bytes + used, request)",
                      bounded_read)
        self.assertIn("x86os_sleep_ms(DESKTOP_FILE_READ_PAUSE_MS)",
                      bounded_read)
        self.assertNotIn("x86os_yield()", bounded_read)
        self.assertNotIn("x86os_read(descriptor, bytes + used, capacity - used)",
                         bounded_read)
        self.assertIn("DESKTOP_FONT_MAPPING_CAPACITY 262144U", desktop)
        self.assertIn("/usr/share/fonts/reist-unicode.psf", desktop)
        self.assertIn("!reist_unicode_vga_has_glyph(scalar)", desktop)
        self.assertIn("DESKTOP_SVGA2D_PROBE_READY_DEADLINE_MS 2000U", desktop)
        self.assertIn("desktop_svga2d_activate_until_ready", desktop)
        self.assertLess(desktop.index("desktop_font_load(&display)"),
                        desktop.index("probe_activate_status ="))
        self.assertIn("font_overlay_status != 8", desktop)
        self.assertIn("0x10FFFDU", desktop)
        self.assertIn("DESKTOP_FONT_BMP_OK", desktop)
        self.assertIn("DESKTOP_FONT_SUPPLEMENTARY_OK", desktop)

    def test_full_images_include_assets_and_floppy_excludes_them(self) -> None:
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        windows = (ROOT / "scripts/build-windows.ps1").read_text(
            encoding="utf-8")
        for path in ("usr/share/fonts/reist-unicode.psf",
                     "usr/share/fonts/unifont_all-16.0.04.hex.gz",
                     "usr/share/fonts/ofl-1.1.txt",
                     "usr/share/fonts/readme.txt"):
            self.assertIn(path, makefile)
            self.assertIn(path, windows)
        filter_section = makefile[makefile.index("FLOPPY_IMAGE_FILES :="):]
        self.assertIn("usr/share/fonts/reist-unicode.psf=%",
                      filter_section)
        self.assertNotIn("$floppyDataArguments += @(\n        '--data-file', "
                         "\"usr/share/fonts/reist-unicode.psf", windows)

    def test_visual_unicode_sample_is_real_utf8_and_packaged(self) -> None:
        sample = (ROOT / "assets/fonts/unicode.txt").read_text(encoding="utf-8")
        for text in ("Ä Ö Ü", "Α Β Γ", "А Б В", "א ב ג", "ا ب ت",
                     "अ आ इ", "中文字符", "日本語", "한국어",
                     "😀 🚀 𐍈 𝄞 𠀀"):
            self.assertIn(text, sample)
        self.assertIn("usr/share/fonts/unicode.txt=assets/fonts/unicode.txt",
                      (ROOT / "Makefile").read_text(encoding="utf-8"))
        self.assertIn("usr/share/fonts/unicode.txt=$(Join-Path $RepoRoot "
                      "'assets\\fonts\\unicode.txt')",
                      (ROOT / "scripts/build-windows.ps1").read_text(
                          encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
