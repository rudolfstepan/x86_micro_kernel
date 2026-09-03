"""PSF2 Ring-3 font parser, asset and integration contracts."""

from pathlib import Path
import hashlib
import shutil
import struct
import subprocess
import tempfile
import tomllib
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

    def test_catalog_pins_five_sources_licenses_and_psf2_outputs(self) -> None:
        catalog = tomllib.loads(
            (ROOT / "assets/fonts/catalog.toml").read_text(encoding="utf-8"))
        self.assertEqual(catalog["schema"], "reist.font-catalog/1")
        self.assertEqual(catalog["pixel_heights"],
                         [10, 12, 14, 16, 18, 20, 24, 28])
        families = catalog["families"]
        self.assertEqual([family["id"] for family in families],
                         [1, 2, 3, 4, 5])
        self.assertEqual([family["name"] for family in families], [
            "GNU Unifont", "JetBrains Mono", "Source Code Pro", "Iosevka",
            "Fira Code"])
        raster_fingerprints = set()
        for family in families:
            for field in ("version", "copyright", "license", "source_url"):
                self.assertTrue(family[field])
            for path_field, hash_field in (("source_path", "source_sha256"),
                                           ("license_path", "license_sha256")):
                path = ROOT / family[path_field]
                self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(),
                                 family[hash_field])
            runtime = ROOT / "assets/fonts" / Path(
                family["runtime_path"]).name
            data = runtime.read_bytes()
            self.assertEqual(hashlib.sha256(data).hexdigest(),
                             family["runtime_sha256"])
            header = struct.unpack_from("<8I", data)
            self.assertEqual(header[0], 0x864AB572)
            self.assertEqual(header[2], 32)
            self.assertEqual(header[6:8],
                             (family["base_height"], family["base_width"]))
            raster_fingerprints.add(self._glyph_fingerprint(data, ord("A")))
        self.assertEqual(len(raster_fingerprints), 5)

    @staticmethod
    def _glyph_fingerprint(data: bytes, scalar: int) -> str:
        _, _, header_size, flags, count, char_size, _, _ = struct.unpack_from(
            "<8I", data)
        if not flags & 1:
            raise AssertionError("PSF2 Unicode table is required")
        cursor = header_size + count * char_size
        encoded = chr(scalar).encode("utf-8")
        for glyph in range(count):
            end = data.index(b"\xff", cursor)
            for sequence in data[cursor:end].split(b"\xfe"):
                if sequence == encoded:
                    start = header_size + glyph * char_size
                    return hashlib.sha256(data[start:start + char_size]).hexdigest()
            cursor = end + 1
        raise AssertionError(f"missing U+{scalar:04X}")

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

    @unittest.skipUnless(GCC, "gcc is required for catalog host behavior")
    def test_catalog_metrics_are_fixed_and_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / "gui_font_catalog_host.exe"
            subprocess.run(
                [GCC, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 f"-I{ROOT}", f"-I{ROOT / 'userspace/gui/include'}",
                 "test/test_gui_font_catalog_host.c",
                 "userspace/gui/lib/font_catalog.c", "-o", str(executable)],
                cwd=ROOT, check=True, capture_output=True)
            subprocess.run([str(executable)], cwd=ROOT, check=True,
                           capture_output=True)

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
        for spec in (
                "usr/share/fonts/reist-vga.psf",
                "usr/share/fonts/reist-unicode.psf",
                "usr/share/fonts/reist-jetbrains-mono.psf",
                "usr/share/fonts/reist-source-code-pro.psf",
                "usr/share/fonts/reist-iosevka.psf",
                "usr/share/fonts/reist-fira-code.psf",
                "usr/share/fonts/catalog.toml",
                "usr/share/fonts/jetbrains-mono-ofl.txt",
                "usr/share/fonts/source-code-pro-ofl.txt",
                "usr/share/fonts/iosevka-ofl.txt",
                "usr/share/fonts/fira-code-ofl.txt"):
            self.assertIn(spec, makefile)
            self.assertIn(spec, windows)

    def test_desktop_loads_and_overlays_extension_glyphs(self) -> None:
        desktop = (ROOT / "userspace/gui/compositor/desktop.c").read_text(
            encoding="utf-8")
        self.assertIn("reist_gui_font_open_psf2", desktop)
        self.assertIn("reist_gui_font_raster_xrgb_region", desktop)
        self.assertIn("!reist_unicode_vga_has_glyph(scalar)", desktop)
        self.assertIn("intersect_rects", desktop)
        self.assertIn("DESKTOP_FONT_FALLBACK_OK", desktop)
        self.assertIn("desktop_editor_font_catalog_load", desktop)
        self.assertIn("DESKTOP_EDITOR_FONT_FALLBACK", desktop)
        self.assertIn("reist_gui_font_raster_scaled_xrgb", desktop)


if __name__ == "__main__":
    unittest.main()
