import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "userspace/image/include/reist/image.h"
SOURCE = ROOT / "userspace/image/lib/image_ico.c"
DISPATCH = ROOT / "userspace/image/lib/image.c"
HOST = ROOT / "test/test_image_ico_host.c"
ICON_NAMES = (
    "folder-empty.ico", "folder-full.ico", "program.ico", "text.ico",
    "audio.ico", "image.ico", "settings.ico", "unknown.ico",
)


class ImageIcoSourceTests(unittest.TestCase):
    def test_decoder_is_bounded_and_all_assets_are_raw_dib_ico(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")
        dispatch = DISPATCH.read_text(encoding="utf-8")
        self.assertIn("REIST_IMAGE_FORMAT_ICO", header)
        self.assertIn("reist_image_decode_ico", header)
        self.assertIn("reist_image_decode_ico", dispatch)
        self.assertIn("ICO_MAX_DIRECTORY_ENTRIES 16U", source)
        self.assertIn("ICO_MAX_DIMENSION 32U", source)
        self.assertNotRegex(source, r"\b(malloc|calloc|realloc|free)\s*\(")
        for name in ICON_NAMES:
            encoded = (ROOT / "assets/icons" / name).read_bytes()
            self.assertGreaterEqual(len(encoded), 22)
            self.assertEqual(encoded[:6], b"\x00\x00\x01\x00\x01\x00")
            self.assertEqual(encoded[6], 32)
            self.assertEqual(encoded[7], 32)
            offset = int.from_bytes(encoded[18:22], "little")
            self.assertNotEqual(encoded[offset:offset + 8], b"\x89PNG\r\n\x1a\n")
            self.assertEqual(int.from_bytes(encoded[offset:offset + 4], "little"), 40)

    def test_host_behavior(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "image-ico-test.exe"
            subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-Iuserspace/image/include", str(DISPATCH), str(SOURCE),
                 str(HOST), "-o", str(executable)],
                cwd=ROOT, check=True, capture_output=True, text=True,
            )
            subprocess.run([str(executable)], cwd=ROOT, check=True,
                           capture_output=True, text=True)


if __name__ == "__main__":
    unittest.main()
