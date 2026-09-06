"""PSF2 Ring-3 font parser, asset and integration contracts."""

from pathlib import Path
import hashlib
import importlib.util
import shutil
import struct
import subprocess
import tempfile
import tomllib
import unittest


ROOT = Path(__file__).resolve().parents[1]
GCC = shutil.which("gcc")
GENERATOR_SPEC = importlib.util.spec_from_file_location(
    "reist_generate_psf2_font", ROOT / "scripts/generate_psf2_font.py")
assert GENERATOR_SPEC is not None and GENERATOR_SPEC.loader is not None
GENERATOR = importlib.util.module_from_spec(GENERATOR_SPEC)
GENERATOR_SPEC.loader.exec_module(GENERATOR)


class GuiFontTests(unittest.TestCase):
    def test_normalized_licenses_retain_pinned_upstream_provenance(self) -> None:
        catalog = tomllib.loads(
            (ROOT / "assets/fonts/catalog.toml").read_text(encoding="utf-8"))
        families = {family["id"]: family for family in catalog["families"]}
        # Reconstruct only the two documented, independently checked upstream
        # byte sequences. The ordinary integrity test still hashes raw files;
        # it never normalizes a corrupted input into an accepted one.
        for family_id in (1, 3):
            with self.subTest(family_id=family_id):
                family = families[family_id]
                data = (ROOT / family["license_path"]).read_bytes()
                self.assertNotIn(b"\r", data)
                self.assertEqual(hashlib.sha256(data).hexdigest(),
                                 family["license_sha256"])
                if family_id == 1:
                    self.assertEqual(family["license_normalization"],
                                     "CRLF to LF; trim trailing spaces and tabs")
                    line = b"fonts, including any derivative works, can be bundled, embedded,\n"
                    self.assertEqual(data.count(line), 1)
                    data = data.replace(line, line[:-1] + b" \n")
                    expected = "869692af094c57fb7258c57fe26820c759319603321d0ffeb278de3651763ded"
                    self.assertEqual(family["license_archive_member"],
                                     "unifont-16.0.04/OFL-1.1.txt")
                else:
                    self.assertEqual(family["license_normalization"], "CRLF to LF")
                    expected = "7c940e28a5388e9bba866cf0e408edda45fe0899ba98665b8f6ab31dc5e4b8ff"
                self.assertTrue(family["license_upstream_url"].startswith("https://"))
                self.assertEqual(family["license_upstream_sha256"], expected)
                self.assertEqual(hashlib.sha256(data.replace(b"\n", b"\r\n")).hexdigest(),
                                 expected)

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
        self.assertEqual(catalog["schema"], "reist.font-catalog/2")
        self.assertEqual(catalog["generator_version"], 2)
        self.assertEqual(catalog["generator_grayscale_threshold"], 96)
        self.assertEqual(catalog["pixel_heights"],
                         [10, 12, 14, 16, 18, 20, 24, 28])
        families = catalog["families"]
        self.assertEqual([family["id"] for family in families],
                         [1, 2, 3, 4, 5])
        self.assertEqual([family["name"] for family in families], [
            "GNU Unifont", "JetBrains Mono", "Source Code Pro", "Iosevka",
            "Fira Code"])
        raster_fingerprints = set()
        sized_fingerprints = {height: set()
                              for height in catalog["pixel_heights"]}
        outline_sources = {
            name: source for name, source, _ in GENERATOR.EDITOR_FONTS}
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
            if family["id"] == 1:
                continue
            self.assertEqual(len(family["runtime_paths"]), 8)
            self.assertEqual(len(family["runtime_sha256s"]), 8)
            self.assertEqual(len(family["raster_point_sizes"]), 8)
            self.assertEqual(len(family["cell_widths"]), 8)
            family_key = Path(family["runtime_path"]).stem.removeprefix(
                "reist-")
            for index, height in enumerate(catalog["pixel_heights"]):
                sized_runtime = ROOT / "assets/fonts" / Path(
                    family["runtime_paths"][index]).name
                sized_data = sized_runtime.read_bytes()
                self.assertEqual(
                    hashlib.sha256(sized_data).hexdigest(),
                    family["runtime_sha256s"][index])
                sized_header = struct.unpack_from("<8I", sized_data)
                self.assertEqual(sized_header[0:4],
                                 (0x864AB572, 0, 32, 1))
                self.assertEqual(sized_header[4], 96)
                self.assertEqual(sized_header[6], height)
                self.assertEqual(sized_header[7],
                                 family["cell_widths"][index])
                selected = GENERATOR.editor_font_for_height(
                    outline_sources[family_key], height)
                self.assertEqual(selected[1], family["cell_widths"][index])
                self.assertEqual(selected[4],
                                 family["raster_point_sizes"][index])
                self.assertLessEqual(sized_header[7], 32)
                self.assertEqual(
                    set(self._glyph_bytes(sized_data, ord(" "))), {0})
                for scalar in map(ord, "AEeg09"):
                    glyph = self._glyph_bytes(sized_data, scalar)
                    foreground = sum(byte.bit_count() for byte in glyph)
                    self.assertGreaterEqual(foreground, 2)
                    self.assertLess(foreground,
                                    sized_header[6] * sized_header[7] * 3 // 4)
                sized_fingerprints[height].add(
                    self._glyph_fingerprint(sized_data, ord("A")))
        self.assertEqual(len(raster_fingerprints), 5)
        for fingerprints in sized_fingerprints.values():
            self.assertEqual(len(fingerprints), 4)

    @staticmethod
    def _glyph_fingerprint(data: bytes, scalar: int) -> str:
        return hashlib.sha256(
            GuiFontTests._glyph_bytes(data, scalar)).hexdigest()

    @staticmethod
    def _glyph_bytes(data: bytes, scalar: int) -> bytes:
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
                    return data[start:start + char_size]
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
        for family in (
                "jetbrains-mono", "source-code-pro", "iosevka", "fira-code"):
            for height in (10, 12, 14, 16, 18, 20, 28):
                self.assertIn(
                    f"usr/share/fonts/reist-{family}-{height}.psf", makefile)
            self.assertIn(f"'{family}'", windows)
        self.assertIn("@(10, 12, 14, 16, 18, 20, 28)", windows)

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
        self.assertIn("[REIST_GUI_FONT_SIZE_COUNT]", desktop)
        self.assertIn("DESKTOP_EDITOR_FONT_FILE_CAPACITY 12288U", desktop)
        self.assertIn("reist_gui_font_catalog_asset", desktop)
        self.assertIn("font->width == width && font->height == height", desktop)
        self.assertIn("reist_gui_font_raster_xrgb(", desktop)


if __name__ == "__main__":
    unittest.main()
