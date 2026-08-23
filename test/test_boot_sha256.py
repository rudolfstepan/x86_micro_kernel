import binascii
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
    KERNEL_B_RELATIVE_LBA,
    create_manifest,
)
from scripts.run_qemu_boot_integrity import (
    create_crc_valid_sha_mismatch,
    create_signature_mismatch,
)
from scripts.validate_boot_manifest import validate_image


SECTOR_SIZE = 512
PARTITION_LBA = 1
PARTITION_SECTORS = 6144
KERNEL_LBA = 128
TEST_SIGNATURE = bytes((index * 7 + 3) & 0xFF for index in range(256))


def image_with_kernel(kernel: bytes) -> bytearray:
    image = bytearray((PARTITION_LBA + PARTITION_SECTORS) * SECTOR_SIZE)
    image[510:512] = b"\x55\xaa"
    image[446] = 0x80
    image[450] = 0xDA
    struct.pack_into(
        "<II", image, 454, PARTITION_LBA, PARTITION_SECTORS
    )
    image[SECTOR_SIZE:2 * SECTOR_SIZE] = create_manifest(
        4, kernel, PARTITION_SECTORS, TEST_SIGNATURE
    )
    backup_manifest = (PARTITION_LBA + BACKUP_MANIFEST_RELATIVE_LBA) * SECTOR_SIZE
    image[backup_manifest:backup_manifest + SECTOR_SIZE] = create_manifest(
        4, kernel, PARTITION_SECTORS, TEST_SIGNATURE, KERNEL_B_RELATIVE_LBA
    )
    offset = (PARTITION_LBA + KERNEL_LBA) * SECTOR_SIZE
    image[offset:offset + len(kernel)] = kernel
    backup_offset = (PARTITION_LBA + KERNEL_B_RELATIVE_LBA) * SECTOR_SIZE
    image[backup_offset:backup_offset + len(kernel)] = kernel
    return image


class BootSha256Tests(unittest.TestCase):
    def test_signature_fixture_changes_only_checksummed_signature(self):
        kernel = bytes((index * 43 + 17) & 0xFF for index in range(4096))
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source.img"
            output = Path(directory) / "signature.img"
            source.write_bytes(image_with_kernel(kernel))
            mutation_offset = create_signature_mismatch(source, output)
            before = source.read_bytes()
            after = output.read_bytes()
            manifest_start = PARTITION_LBA * SECTOR_SIZE
            before_manifest = before[manifest_start:manifest_start + SECTOR_SIZE]
            after_manifest = after[manifest_start:manifest_start + SECTOR_SIZE]
            self.assertEqual(before_manifest[48:80], after_manifest[48:80])
            self.assertNotEqual(before_manifest[80:336], after_manifest[80:336])
            self.assertEqual(
                sum(struct.unpack("<128I", after_manifest)) & 0xFFFFFFFF, 0
            )
            kernel_start = (PARTITION_LBA + KERNEL_LBA) * SECTOR_SIZE
            self.assertEqual(
                before[kernel_start:kernel_start + len(kernel)],
                after[kernel_start:kernel_start + len(kernel)],
            )
            self.assertEqual(mutation_offset, 208)

    def test_negative_fixture_changes_sha_but_preserves_crc_and_manifest_checksum(self):
        kernel = bytes((index * 37 + 9) & 0xFF for index in range(4096))
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source.img"
            output = Path(directory) / "tampered.img"
            source.write_bytes(image_with_kernel(kernel))
            mutation_offset = create_crc_valid_sha_mismatch(source, output)
            data = output.read_bytes()
            manifest = data[SECTOR_SIZE:2 * SECTOR_SIZE]
            start = (PARTITION_LBA + KERNEL_LBA) * SECTOR_SIZE
            changed_kernel = data[start:start + len(kernel)]
            self.assertEqual(
                struct.unpack_from("<I", manifest, 36)[0],
                binascii.crc32(changed_kernel) & 0xFFFFFFFF,
            )
            self.assertEqual(
                sum(struct.unpack("<128I", manifest)) & 0xFFFFFFFF, 0
            )
            self.assertEqual(manifest[48:80], hashlib.sha256(kernel).digest())
            self.assertNotEqual(
                manifest[48:80], hashlib.sha256(changed_kernel).digest()
            )
            self.assertEqual(mutation_offset, len(kernel) // 2)
            with self.assertRaisesRegex(ValueError, "SHA-256 digest mismatch"):
                validate_image(output, "hdd")

    def test_stage2_contains_fixed_fips_sha_path_before_elf_parsing(self):
        source = (ROOT / "arch/x86/boot/bios/stage2_bios.asm").read_text(
            encoding="utf-8"
        )
        self.assertIn("sha256_transform:", source)
        self.assertIn("sha256_finalize:", source)
        self.assertIn("sha256_schedule:", source)
        self.assertIn("times 64 dd 0", source)
        constants = source.split("sha256_constants:", 1)[1].split(
            "sha256_schedule:", 1
        )[0]
        self.assertEqual(constants.count("0x"), 64)
        verification = source.index("call verify_kernel_integrity")
        elf_parse = source.index("call parse_elf_header")
        self.assertLess(verification, elf_parse)
        self.assertIn("Kernel SHA-256 verification failed", source)
        integrity = source.split("verify_kernel_integrity:", 1)[1].split(
            "protected_memory_operation:", 1
        )[0]
        self.assertEqual(integrity.count("call read_bounce"), 1)
        self.assertNotIn("malloc", integrity.lower())

    def test_runtime_mode_is_bounded_and_wired(self):
        runtime = (ROOT / "scripts/test-reist-runtime.ps1").read_text(
            encoding="utf-8"
        )
        runner = (ROOT / "scripts/run_qemu_boot_integrity.py").read_text(
            encoding="utf-8"
        )
        self.assertIn("'boot-integrity'", runtime)
        self.assertIn("run_qemu_boot_integrity.py", runtime)
        self.assertIn("timeout=timeout", runner)
        self.assertIn("BOOT_MARKER in transcript", runner)


if __name__ == "__main__":
    unittest.main()
