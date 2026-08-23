import hashlib
import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from scripts.create_native_boot_image import (
    BACKUP_MANIFEST_RELATIVE_LBA,
    BOOT_CONTROL_PRIMARY_RELATIVE_LBA,
    BOOT_CONTROL_SECONDARY_RELATIVE_LBA,
    KERNEL_B_RELATIVE_LBA,
    create_boot_control_record,
    create_manifest,
)
from scripts.validate_boot_manifest import validate_image


SECTOR_SIZE = 512
PARTITION_LBA = 1
PARTITION_SECTORS = 6144
KERNEL_LBA = 128
TEST_SIGNATURE = bytes((index * 13 + 1) & 0xFF for index in range(256))


def _refresh_checksum(manifest: bytearray) -> None:
    struct.pack_into("<I", manifest, 44, 0)
    checksum = (-sum(struct.unpack("<128I", manifest))) & 0xFFFFFFFF
    struct.pack_into("<I", manifest, 44, checksum)


def _hdd_image(kernel: bytes) -> bytearray:
    image = bytearray((PARTITION_LBA + PARTITION_SECTORS) * SECTOR_SIZE)
    image[510:512] = b"\x55\xaa"
    entry = bytearray(16)
    entry[0] = 0x80
    entry[4] = 0xDA
    struct.pack_into("<II", entry, 8, PARTITION_LBA, PARTITION_SECTORS)
    image[446:462] = entry
    manifest = create_manifest(4, kernel, PARTITION_SECTORS, TEST_SIGNATURE)
    image[PARTITION_LBA * SECTOR_SIZE:(PARTITION_LBA + 1) * SECTOR_SIZE] = manifest
    backup = create_manifest(
        4, kernel, PARTITION_SECTORS, TEST_SIGNATURE, KERNEL_B_RELATIVE_LBA
    )
    backup_start = (PARTITION_LBA + BACKUP_MANIFEST_RELATIVE_LBA) * SECTOR_SIZE
    image[backup_start:backup_start + SECTOR_SIZE] = backup
    control = create_boot_control_record()
    for control_lba in (
            BOOT_CONTROL_PRIMARY_RELATIVE_LBA,
            BOOT_CONTROL_SECONDARY_RELATIVE_LBA):
        control_start = (PARTITION_LBA + control_lba) * SECTOR_SIZE
        image[control_start:control_start + SECTOR_SIZE] = control
    start = (PARTITION_LBA + KERNEL_LBA) * SECTOR_SIZE
    image[start:start + len(kernel)] = kernel
    backup_kernel = (PARTITION_LBA + KERNEL_B_RELATIVE_LBA) * SECTOR_SIZE
    image[backup_kernel:backup_kernel + len(kernel)] = kernel
    return image


class BootManifestTests(unittest.TestCase):
    def _validate(self, image: bytes, layout: str = "hdd"):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "boot.img"
            path.write_bytes(image)
            return validate_image(path, layout)

    def test_accepts_v3_manifest_with_exact_sha256_and_signature(self):
        kernel = bytes((index * 29 + 7) & 0xFF for index in range(4096))
        info = self._validate(_hdd_image(kernel))
        self.assertEqual(info.kernel_sha256, hashlib.sha256(kernel).hexdigest())
        self.assertEqual(info.partition_lba, PARTITION_LBA)
        self.assertEqual(info.slot_count, 2)
        self.assertEqual(
            info.signature_sha256, hashlib.sha256(TEST_SIGNATURE).hexdigest()
        )

    def test_rejects_tampered_kernel(self):
        kernel = bytes((index * 17 + 3) & 0xFF for index in range(4096))
        image = _hdd_image(kernel)
        image[(PARTITION_LBA + KERNEL_LBA) * SECTOR_SIZE + 99] ^= 0x40
        with self.assertRaisesRegex(ValueError, "SHA-256 digest mismatch"):
            self._validate(image)

    def test_rejects_missing_digest_even_with_valid_manifest_checksum(self):
        image = _hdd_image(bytes(4096))
        start = PARTITION_LBA * SECTOR_SIZE
        manifest = bytearray(image[start:start + SECTOR_SIZE])
        manifest[48:80] = bytes(32)
        _refresh_checksum(manifest)
        image[start:start + SECTOR_SIZE] = manifest
        with self.assertRaisesRegex(ValueError, "digest is missing"):
            self._validate(image)

    def test_rejects_v1_manifest(self):
        image = _hdd_image(bytes(4096))
        start = PARTITION_LBA * SECTOR_SIZE
        manifest = bytearray(image[start:start + SECTOR_SIZE])
        manifest[:8] = b"X86BOOT1"
        struct.pack_into("<I", manifest, 8, 1)
        _refresh_checksum(manifest)
        image[start:start + SECTOR_SIZE] = manifest
        with self.assertRaisesRegex(ValueError, "magic"):
            self._validate(image)

    def test_rejects_kernel_extent_above_fixed_validator_limit(self):
        image = _hdd_image(bytes(4096))
        start = PARTITION_LBA * SECTOR_SIZE
        manifest = bytearray(image[start:start + SECTOR_SIZE])
        struct.pack_into("<I", manifest, 28, 64 * 1024 * 1024 + 1)
        _refresh_checksum(manifest)
        image[start:start + SECTOR_SIZE] = manifest
        with self.assertRaisesRegex(ValueError, "64 MiB validator maximum"):
            self._validate(image)

    def test_accepts_floppy_layout(self):
        kernel = bytes((index * 11) & 0xFF for index in range(4096))
        sectors = 2880
        image = bytearray(sectors * SECTOR_SIZE)
        image[510:512] = b"\x55\xaa"
        image[SECTOR_SIZE:2 * SECTOR_SIZE] = create_manifest(
            4, kernel, sectors - 1, TEST_SIGNATURE
        )
        start = (1 + KERNEL_LBA) * SECTOR_SIZE
        image[start:start + len(kernel)] = kernel
        info = self._validate(image, "floppy")
        self.assertEqual(info.layout, "floppy")

    def test_manifest_parsers_require_v3_signed_header_and_nonzero_digest(self):
        sources = [
            ROOT / "arch/x86/boot/bios/stage1_floppy.asm",
            ROOT / "arch/x86/boot/bios/stage2_bios.asm",
        ]
        for path in sources:
            text = path.read_text(encoding="utf-8")
            self.assertIn("0x32544F4F", text, path.name)
            self.assertIn("MANIFEST_HEADER_SIZE", text, path.name)
        mbr = (ROOT / "arch/x86/boot/bios/stage1_mbr.asm").read_text(
            encoding="utf-8"
        )
        self.assertNotIn("MANIFEST_MAGIC", mbr)
        self.assertIn("STAGE2_LOAD_SECTORS", mbr)
        stage2 = sources[1].read_text(encoding="utf-8")
        self.assertIn("MANIFEST_KERNEL_SHA", stage2)
        self.assertIn("MANIFEST_KERNEL_SIGNATURE", stage2)
        self.assertIn(".manifest_sha", stage2)

    def test_rejects_missing_signature_with_repaired_checksum(self):
        image = _hdd_image(bytes(4096))
        start = PARTITION_LBA * SECTOR_SIZE
        manifest = bytearray(image[start:start + SECTOR_SIZE])
        manifest[80:336] = bytes(256)
        _refresh_checksum(manifest)
        image[start:start + SECTOR_SIZE] = manifest
        with self.assertRaisesRegex(ValueError, "signature is missing"):
            self._validate(image)

    def test_rejects_unsigned_flag_with_repaired_checksum(self):
        image = _hdd_image(bytes(4096))
        start = PARTITION_LBA * SECTOR_SIZE
        manifest = bytearray(image[start:start + SECTOR_SIZE])
        struct.pack_into("<I", manifest, 40, 0)
        _refresh_checksum(manifest)
        image[start:start + SECTOR_SIZE] = manifest
        with self.assertRaisesRegex(ValueError, "signed-kernel profile"):
            self._validate(image)

    def test_builds_run_independent_manifest_validator(self):
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        windows = (ROOT / "scripts/build-windows.ps1").read_text(encoding="utf-8")
        self.assertGreaterEqual(makefile.count("validate_boot_manifest.py"), 2)
        self.assertGreaterEqual(windows.count("validate_boot_manifest.py"), 2)
        self.assertIn("function Invoke-PythonProcess", windows)
        self.assertIn("[System.Diagnostics.ProcessStartInfo]::new()", windows)
        self.assertIn("RedirectStandardOutput = $true", windows)
        self.assertIn("RedirectStandardError = $true", windows)
        self.assertIn("$process.ExitCode", windows)
        self.assertIn("Remove-Item -LiteralPath $FloppyImage", windows)
        self.assertIn("Remove-Item -LiteralPath $RawImage", windows)


if __name__ == "__main__":
    unittest.main()
