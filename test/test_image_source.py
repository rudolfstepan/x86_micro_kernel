#!/usr/bin/env python3
import subprocess
import shutil
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class ImageLibraryTests(unittest.TestCase):
    def test_decoder_behavior(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "image-library-test.exe"
            subprocess.run([
                compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-Iuserspace/image/include", "test/test_image_host.c",
                "userspace/image/lib/image.c", "-o", str(output),
            ], cwd=ROOT, check=True, capture_output=True, text=True)
            subprocess.run([
                str(output), str(ROOT / "assets/images/demo-desktop.bmp"),
                str(ROOT / "assets/images/demo-colors.gif"),
            ], cwd=ROOT, check=True)

    def test_layering_and_packaging(self):
        viewer = (ROOT / "userspace/gui/apps/image_viewer/main.c").read_text()
        sdk = (ROOT / "scripts/build_user_sdk.py").read_text()
        programs = (ROOT / "scripts/build_system_programs.py").read_text()
        syscalls = (ROOT / "kernel/syscall/syscall_table.c").read_text()
        associations = (ROOT / "config/etc/reist/filetypes.conf").read_text()
        self.assertIn('#include "reist/image.h"', viewer)
        self.assertIn("reist_image_decode", viewer)
        self.assertIn("x86os_draw_pixels", viewer)
        self.assertNotIn("while (x < draw_width)", viewer)
        self.assertNotIn("decode_bmp", viewer)
        self.assertNotIn("gif_lzw", viewer)
        self.assertIn("libreistimage.a", sdk)
        self.assertIn('"IMAGEVIEWER.PRG"', programs)
        self.assertIn(".bmp=/usr/gui/bin/imageviewer.prg", associations)
        self.assertIn(".gif=/usr/gui/bin/imageviewer.prg", associations)
        self.assertIn("FILE_READ_CHUNK_CAPACITY = 16U * 1024U", syscalls)


if __name__ == "__main__":
    unittest.main()
