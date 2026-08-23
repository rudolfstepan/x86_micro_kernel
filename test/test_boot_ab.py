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
    KERNEL_RELATIVE_LBA,
    STAGE2_MAX_SECTORS,
    STAGE2_RELATIVE_LBA,
    create_manifest,
)
from scripts.validate_boot_manifest import validate_image


SECTOR_SIZE = 512
PARTITION_LBA = 1
PARTITION_SECTORS = 6144
SIGNATURE = bytes((index * 29 + 7) & 0xFF for index in range(256))


def ab_image(kernel: bytes) -> bytearray:
    image = bytearray((PARTITION_LBA + PARTITION_SECTORS) * SECTOR_SIZE)
    image[510:512] = b"\x55\xaa"
    image[446] = 0x80
    image[450] = 0xDA
    struct.pack_into(
        "<II", image, 454, PARTITION_LBA, PARTITION_SECTORS
    )
    primary = create_manifest(
        4, kernel, PARTITION_SECTORS, SIGNATURE, KERNEL_RELATIVE_LBA
    )
    backup = create_manifest(
        4, kernel, PARTITION_SECTORS, SIGNATURE, KERNEL_B_RELATIVE_LBA
    )
    primary_offset = PARTITION_LBA * SECTOR_SIZE
    backup_offset = (
        PARTITION_LBA + BACKUP_MANIFEST_RELATIVE_LBA
    ) * SECTOR_SIZE
    image[primary_offset:primary_offset + SECTOR_SIZE] = primary
    image[backup_offset:backup_offset + SECTOR_SIZE] = backup
    for kernel_lba in (KERNEL_RELATIVE_LBA, KERNEL_B_RELATIVE_LBA):
        offset = (PARTITION_LBA + kernel_lba) * SECTOR_SIZE
        image[offset:offset + len(kernel)] = kernel
    return image


class BootAbTests(unittest.TestCase):
    def _validate(self, image: bytes):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "ab.img"
            path.write_bytes(image)
            return validate_image(path, "hdd")

    def test_fixed_slot_geometry_is_non_overlapping_and_bounded(self):
        self.assertLessEqual(
            STAGE2_RELATIVE_LBA + STAGE2_MAX_SECTORS,
            BACKUP_MANIFEST_RELATIVE_LBA,
        )
        self.assertLess(BACKUP_MANIFEST_RELATIVE_LBA, KERNEL_RELATIVE_LBA)
        self.assertLess(KERNEL_RELATIVE_LBA, KERNEL_B_RELATIVE_LBA)
        self.assertLess(KERNEL_B_RELATIVE_LBA, PARTITION_SECTORS)

    def test_validator_requires_both_exact_signed_slots(self):
        kernel = bytes((index * 37 + 11) & 0xFF for index in range(4096))
        info = self._validate(ab_image(kernel))
        self.assertEqual(info.slot_count, 2)

    def test_validator_rejects_backup_kernel_corruption(self):
        kernel = bytes((index * 41 + 3) & 0xFF for index in range(4096))
        image = ab_image(kernel)
        offset = (PARTITION_LBA + KERNEL_B_RELATIVE_LBA) * SECTOR_SIZE
        image[offset + 91] ^= 0x80
        with self.assertRaisesRegex(ValueError, "SHA-256 digest mismatch"):
            self._validate(image)

    def test_validator_rejects_redirected_backup_slot(self):
        kernel = bytes((index * 43 + 5) & 0xFF for index in range(4096))
        image = ab_image(kernel)
        offset = (PARTITION_LBA + BACKUP_MANIFEST_RELATIVE_LBA) * SECTOR_SIZE
        manifest = bytearray(image[offset:offset + SECTOR_SIZE])
        struct.pack_into("<I", manifest, 24, KERNEL_B_RELATIVE_LBA - 1)
        struct.pack_into("<I", manifest, 44, 0)
        checksum = (-sum(struct.unpack("<128I", manifest))) & 0xFFFFFFFF
        struct.pack_into("<I", manifest, 44, checksum)
        image[offset:offset + SECTOR_SIZE] = manifest
        with self.assertRaisesRegex(ValueError, "kernel offset is unsupported"):
            self._validate(image)

    def test_bios_stages_bind_and_bound_the_fallback(self):
        stage1 = (ROOT / "arch/x86/boot/bios/stage1_mbr.asm").read_text(
            encoding="utf-8"
        )
        stage2 = (ROOT / "arch/x86/boot/bios/stage2_bios.asm").read_text(
            encoding="utf-8"
        )
        self.assertIn("STAGE2_LOAD_SECTORS  equ 64", stage1)
        self.assertIn("mov cx, STAGE2_LOAD_SECTORS", stage1)
        self.assertIn("xor ebx, ebx", stage1)
        self.assertNotIn("MANIFEST_MAGIC", stage1)
        self.assertIn("BACKUP_MANIFEST_LBA   equ 96", stage2)
        self.assertIn("KERNEL_B_LBA          equ 3136", stage2)
        self.assertIn("BOOT_SLOT_A_FAILED_TRY_B", stage2)
        candidate = stage2.split("candidate_error:", 1)[1].split(
            "fatal:", 1
        )[0]
        self.assertIn("cmp dword [manifest_relative_lba], PRIMARY_MANIFEST_LBA",
                      candidate)
        self.assertIn("jmp load_manifest_candidate", candidate)
        self.assertLess(stage2.index("call rsa_pss_verify"),
                        stage2.index("call parse_elf_header"))


if __name__ == "__main__":
    unittest.main()
