import hashlib
import shutil
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from scripts.create_boot_update_bundle import (
    BUNDLE_CRC_OFFSET,
    BUNDLE_HEADER_SIZE,
    BUNDLE_KERNEL_CAPACITY,
    create_update_bundle,
)
from scripts.create_native_boot_image import (
    BACKUP_MANIFEST_RELATIVE_LBA,
    BOOT_CONTROL_PRIMARY_RELATIVE_LBA,
    BOOT_CONTROL_SECONDARY_RELATIVE_LBA,
    KERNEL_B_RELATIVE_LBA,
    KERNEL_RELATIVE_LBA,
    create_boot_control_record,
    create_manifest,
)
from scripts.sign_boot_artifact import sign_artifact
from scripts.update_native_boot_slot import update_inactive_slot_from_bundle
from scripts.validate_boot_manifest import validate_image
from scripts.verify_boot_update_bundle import verify_update_bundle


POLICY = ROOT / "safety/boot_trust_policy.json"
PRIVATE_KEY = ROOT / "test/fixtures/reist-research-dev-private.pem"
SECTOR_SIZE = 512
PARTITION_LBA = 1
PARTITION_SECTORS = 6144


def minimal_kernel(seed: int, size: int = 4608) -> bytes:
    kernel = bytearray((index * seed + 7) & 0xFF for index in range(size))
    ident = b"\x7fELF\x01\x01\x01" + bytes(9)
    struct.pack_into(
        "<16sHHIIIIIHHHHHH", kernel, 0, ident, 2, 3, 1,
        0x00100000, 52, 0, 0, 52, 32, 1, 0, 0, 0,
    )
    struct.pack_into(
        "<IIIIIIII", kernel, 52, 1, 84, 0x00100000, 0x00100000,
        32, 64, 5, 4096,
    )
    return bytes(kernel)


def base_image(kernel: bytes, signature: bytes) -> bytes:
    image = bytearray((PARTITION_LBA + PARTITION_SECTORS) * SECTOR_SIZE)
    image[510:512] = b"\x55\xaa"
    image[446] = 0x80
    image[450] = 0xDA
    struct.pack_into("<II", image, 454, PARTITION_LBA, PARTITION_SECTORS)
    for manifest_lba, kernel_lba in (
            (0, KERNEL_RELATIVE_LBA),
            (BACKUP_MANIFEST_RELATIVE_LBA, KERNEL_B_RELATIVE_LBA)):
        manifest = create_manifest(
            4, kernel, PARTITION_SECTORS, signature, kernel_lba
        )
        offset = (PARTITION_LBA + manifest_lba) * SECTOR_SIZE
        image[offset:offset + SECTOR_SIZE] = manifest
        kernel_offset = (PARTITION_LBA + kernel_lba) * SECTOR_SIZE
        image[kernel_offset:kernel_offset + len(kernel)] = kernel
    control = create_boot_control_record()
    for control_lba in (
            BOOT_CONTROL_PRIMARY_RELATIVE_LBA,
            BOOT_CONTROL_SECONDARY_RELATIVE_LBA):
        offset = (PARTITION_LBA + control_lba) * SECTOR_SIZE
        image[offset:offset + SECTOR_SIZE] = control
    return bytes(image)


def repair_header_crc(bundle: bytearray) -> None:
    struct.pack_into("<I", bundle, BUNDLE_CRC_OFFSET, 0)
    crc = zlib.crc32(bundle[:BUNDLE_HEADER_SIZE])
    struct.pack_into("<I", bundle, BUNDLE_CRC_OFFSET, crc)


class BootUpdateBundleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        found = shutil.which("openssl")
        fallback = Path(r"C:\msys64\mingw64\bin\openssl.exe")
        cls.openssl = Path(found) if found else fallback
        if not cls.openssl.is_file():
            raise unittest.SkipTest("OpenSSL is required for update-bundle tests")
        cls.fixture = tempfile.TemporaryDirectory()
        fixture = Path(cls.fixture.name)
        cls.kernel = fixture / "kernel.bin"
        cls.signature = fixture / "kernel.bin.sig"
        cls.bundle = fixture / "kernel.rup"
        cls.kernel.write_bytes(minimal_kernel(29))
        sign_artifact(
            cls.kernel, cls.signature, PRIVATE_KEY, POLICY,
            cls.openssl, "research",
        )
        create_update_bundle(
            cls.kernel, cls.signature, cls.bundle, POLICY,
            cls.openssl, ROOT,
        )

    @classmethod
    def tearDownClass(cls):
        cls.fixture.cleanup()

    def test_round_trip_uses_exact_fixed_format_and_pinned_signature(self):
        verified = verify_update_bundle(
            self.bundle, POLICY, self.openssl, ROOT
        )
        kernel = self.kernel.read_bytes()
        self.assertEqual(verified.kernel, kernel)
        self.assertEqual(verified.signature, self.signature.read_bytes())
        self.assertEqual(
            verified.kernel_sha256, hashlib.sha256(kernel).hexdigest()
        )
        self.assertEqual(
            self.bundle.stat().st_size, BUNDLE_HEADER_SIZE + len(kernel)
        )

    def test_consumer_rejects_semantic_corruption_and_trailing_data(self):
        original = self.bundle.read_bytes()
        variants = {}

        reserved = bytearray(original)
        reserved[360] = 1
        repair_header_crc(reserved)
        variants["reserved"] = reserved

        algorithm = bytearray(original)
        struct.pack_into("<I", algorithm, 32, 2)
        repair_header_crc(algorithm)
        variants["algorithm"] = algorithm

        payload = bytearray(original)
        payload[BUNDLE_HEADER_SIZE + 100] ^= 1
        variants["payload"] = payload

        signature = bytearray(original)
        signature[72 + 100] ^= 0x80
        repair_header_crc(signature)
        variants["signature"] = signature

        variants["trailing"] = bytearray(original + b"x")
        variants["truncated"] = bytearray(original[:-1])

        with tempfile.TemporaryDirectory() as directory:
            for name, value in variants.items():
                with self.subTest(name=name):
                    path = Path(directory) / f"{name}.rup"
                    path.write_bytes(value)
                    with self.assertRaises(ValueError):
                        verify_update_bundle(
                            path, POLICY, self.openssl, ROOT
                        )

    def test_producer_failure_preserves_existing_output(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "update.rup"
            bad_signature = Path(directory) / "bad.sig"
            output.write_bytes(b"previous-valid-output")
            changed = bytearray(self.signature.read_bytes())
            changed[0] ^= 1
            bad_signature.write_bytes(changed)
            with self.assertRaisesRegex(ValueError, "verification failed"):
                create_update_bundle(
                    self.kernel, bad_signature, output, POLICY,
                    self.openssl, ROOT,
                )
            self.assertEqual(output.read_bytes(), b"previous-valid-output")

    def test_fixed_capacity_is_rejected_before_output(self):
        with tempfile.TemporaryDirectory() as directory:
            kernel = Path(directory) / "oversize.bin"
            signature = Path(directory) / "oversize.sig"
            output = Path(directory) / "oversize.rup"
            kernel.write_bytes(
                minimal_kernel(7) + bytes(BUNDLE_KERNEL_CAPACITY + 1 - 4608)
            )
            signature.write_bytes(bytes(256))
            with self.assertRaisesRegex(ValueError, "fixed.*capacity"):
                create_update_bundle(
                    kernel, signature, output, POLICY, self.openssl, ROOT
                )
            self.assertFalse(output.exists())

    def test_verified_bundle_enters_existing_inactive_slot_transaction(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source.img"
            output = Path(directory) / "updated.img"
            source.write_bytes(base_image(
                self.kernel.read_bytes(), self.signature.read_bytes()
            ))
            update_inactive_slot_from_bundle(
                source, self.bundle, output, POLICY, self.openssl, ROOT
            )
            info = validate_image(output, "hdd")
            self.assertEqual(info.active_slot, 0)
            self.assertEqual(info.pending_slot, 1)
            self.assertEqual(info.attempts_remaining, 2)

    def test_invalid_bundle_cannot_create_update_image(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source.img"
            invalid = Path(directory) / "invalid.rup"
            output = Path(directory) / "updated.img"
            source.write_bytes(base_image(
                self.kernel.read_bytes(), self.signature.read_bytes()
            ))
            damaged = bytearray(self.bundle.read_bytes())
            damaged[BUNDLE_HEADER_SIZE + 10] ^= 1
            invalid.write_bytes(damaged)
            with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                update_inactive_slot_from_bundle(
                    source, invalid, output, POLICY, self.openssl, ROOT
                )
            self.assertFalse(output.exists())

    def test_consumer_does_not_import_the_producer_parser(self):
        producer = (ROOT / "scripts/create_boot_update_bundle.py").read_text(
            encoding="utf-8"
        )
        consumer = (ROOT / "scripts/verify_boot_update_bundle.py").read_text(
            encoding="utf-8"
        )
        self.assertNotIn("create_boot_update_bundle", consumer)
        self.assertNotIn("active_slot", producer)
        self.assertNotIn("pending_slot", producer)


if __name__ == "__main__":
    unittest.main()
