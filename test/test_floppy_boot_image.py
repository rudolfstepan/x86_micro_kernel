import hashlib
import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from scripts.create_floppy_boot_image import (
    FAT12_SECTORS_PER_FAT,
    FLOPPY_SIZE,
    MANIFEST_LBA,
    create_floppy_image,
)
from test_native_boot_image import minimal_kernel


class FloppyBootImageTests(unittest.TestCase):
    def test_kernel_is_compiled_and_linked_as_release(self):
        makefile = (Path(__file__).parents[1] / "Makefile").read_text(
            encoding="utf-8"
        )
        self.assertIn("-O2 -DNDEBUG", makefile)
        self.assertIn("LDFLAGS := -m elf_i386 -nostdlib --strip-all", makefile)
        self.assertIn("$(OUTPUT_DIR)/kernel.bin", makefile)

    def test_creates_exact_1440_kib_signed_image(self):
        stage1 = bytes(510) + b"\x55\xaa"
        image = create_floppy_image(stage1, bytes(2048), minimal_kernel())
        self.assertEqual(len(image), FLOPPY_SIZE)
        self.assertEqual(image[510:512], b"\x55\xaa")
        manifest = image[MANIFEST_LBA * 512:(MANIFEST_LBA + 1) * 512]
        self.assertEqual(manifest[:8], b"X86BOOT2")
        self.assertEqual(struct.unpack_from("<II", manifest, 8), (2, 80))
        self.assertEqual(manifest[48:80], hashlib.sha256(minimal_kernel()).digest())
        self.assertEqual(sum(struct.unpack("<128I", manifest)) & 0xFFFFFFFF, 0)
        reserved = struct.unpack_from("<H", image, 14)[0]
        self.assertGreater(reserved, 128)
        self.assertEqual(image[reserved * 512:reserved * 512 + 3],
                         b"\xf0\xff\xff")
        sectors_per_fat = struct.unpack_from("<H", image, 22)[0]
        self.assertEqual(sectors_per_fat, FAT12_SECTORS_PER_FAT)
        root_sector = reserved + image[16] * sectors_per_fat
        self.assertEqual(image[root_sector * 512:root_sector * 512 + 11],
                         b"README  TXT")

    def test_fat_covers_more_than_the_old_1024_cluster_limit(self):
        stage1 = bytes(510) + b"\x55\xaa"
        image = create_floppy_image(
            stage1,
            bytes(2048),
            minimal_kernel(),
            {"large.bin": bytes(1100 * 512)},
        )
        self.assertEqual(len(image), FLOPPY_SIZE)
        self.assertEqual(
            struct.unpack_from("<H", image, 22)[0],
            FAT12_SECTORS_PER_FAT,
        )

    def test_rejects_kernel_that_does_not_fit(self):
        stage1 = bytes(510) + b"\x55\xaa"
        with self.assertRaisesRegex(ValueError, "too large"):
            create_floppy_image(stage1, bytes(2048), minimal_kernel(FLOPPY_SIZE))

    def test_reist_fat12_uses_v2_redundant_journal_headers(self):
        stage1 = bytes(510) + b"\x55\xaa"
        image = create_floppy_image(
            stage1, bytes(2048), minimal_kernel(), reist_fat12=True
        )
        reserved = struct.unpack_from("<H", image, 14)[0]
        layout_base = reserved - 195
        primary = image[layout_base * 512:(layout_base + 1) * 512]
        mirror = image[(layout_base + 1) * 512:(layout_base + 2) * 512]
        magic, version = struct.unpack_from("<IH", primary)
        self.assertEqual((magic, version), (0x524A3132, 2))
        self.assertEqual(primary, mirror)
        self.assertGreaterEqual(reserved, 196)


if __name__ == "__main__":
    unittest.main()
