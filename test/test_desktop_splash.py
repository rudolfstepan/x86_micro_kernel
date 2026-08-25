import pathlib
import struct
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
ASSET = ROOT / "assets" / "images" / "reist-splash.bmp"
DESKTOP = ROOT / "userspace" / "gui" / "compositor" / "desktop.c"


class DesktopSplashContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.asset = ASSET.read_bytes()
        cls.desktop = DESKTOP.read_text(encoding="utf-8")
        cls.makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        cls.windows = (ROOT / "scripts" / "build-windows.ps1").read_text(
            encoding="utf-8"
        )

    def test_asset_is_fixed_bounded_bmp3(self):
        self.assertGreaterEqual(len(self.asset), 54)
        self.assertEqual(self.asset[:2], b"BM")
        file_size, = struct.unpack_from("<I", self.asset, 2)
        pixel_offset, = struct.unpack_from("<I", self.asset, 10)
        dib_size, width, height, planes, bits = struct.unpack_from(
            "<IiiHH", self.asset, 14
        )
        compression, image_size = struct.unpack_from("<II", self.asset, 30)
        self.assertEqual(file_size, len(self.asset))
        self.assertEqual(pixel_offset, 54)
        self.assertEqual(dib_size, 40)
        self.assertEqual((width, height), (512, 288))
        self.assertEqual((planes, bits, compression), (1, 24, 0))
        self.assertEqual(image_size, 512 * 288 * 3)
        self.assertLessEqual(len(self.asset), 512 * 1024)

    def test_startup_is_visible_before_optional_file_io(self):
        splash = self.desktop[
            self.desktop.index("static void desktop_splash_show(") :
            self.desktop.index("static int desktop_font_load(")
        ]
        self.assertLess(splash.index("x86os_fill_rect("),
                        splash.index("read_file_bounded("))
        self.assertLess(splash.index("x86os_draw_text_pixels("),
                        splash.index("read_file_bounded("))
        self.assertIn('static const char title[] = "REIST OS";', splash)
        self.assertIn("DESKTOP_SPLASH_FALLBACK", splash)

    def test_bounded_decoder_reuses_startup_storage(self):
        self.assertIn("reist_image_decode(", self.desktop)
        self.assertIn("desktop_startup_workspace.splash_decode", self.desktop)
        self.assertIn("desktop_startup_workspace.font_mappings", self.desktop)
        self.assertIn("DESKTOP_SPLASH_PIXEL_OFFSET", self.desktop)
        self.assertIn("splash buffers exceed shared font-file storage",
                      self.desktop)
        self.assertNotIn("x86os_malloc", self.desktop)

    def test_splash_precedes_optional_service_and_font_loading(self):
        main = self.desktop[self.desktop.index("int main(int argc, char **argv)") :]
        splash = main.index("if (argc == 1) desktop_splash_show(&display);")
        service = main.index("desktop_svga2d_connect(0U)")
        font = main.index("desktop_font_load(&display)")
        first_frame = main.index("render_desktop_measured(")
        self.assertLess(splash, service)
        self.assertLess(splash, font)
        self.assertLess(font, first_frame)
        self.assertIn("DESKTOP_SPLASH_READY", self.desktop)
        self.assertIn("Diagnostic modes are timing probes", main)

    def test_full_images_package_asset_but_floppy_excludes_it(self):
        spec = "usr/share/images/reist-splash.bmp=assets/images/reist-splash.bmp"
        self.assertEqual(self.makefile.count(spec), 1)
        floppy = self.makefile[self.makefile.index("FLOPPY_IMAGE_FILES :=") :]
        self.assertIn("usr/share/images/reist-splash.bmp=%", floppy)

        windows_spec = "usr/share/images/reist-splash.bmp=$(Join-Path $RepoRoot"
        self.assertEqual(self.windows.count(windows_spec), 1)
        image_arguments = self.windows[
            self.windows.index("$imageDataArguments = @(") :
            self.windows.index("$floppyDataArguments = @(")
        ]
        self.assertIn(windows_spec, image_arguments)


if __name__ == "__main__":
    unittest.main()
