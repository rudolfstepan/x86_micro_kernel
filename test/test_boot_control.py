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
    BOOT_CONTROL_SLOT_B,
    KERNEL_B_RELATIVE_LBA,
    KERNEL_RELATIVE_LBA,
    create_boot_control_record,
    create_manifest,
)
from scripts.update_native_boot_slot import update_inactive_slot
from scripts.validate_boot_manifest import (
    parse_boot_control_record,
    read_boot_control,
    validate_image,
)


SECTOR_SIZE = 512
PARTITION_LBA = 1
PARTITION_SECTORS = 6144
SIGNATURE_A = bytes((index * 13 + 1) & 0xFF for index in range(256))
SIGNATURE_B = bytes((index * 17 + 3) & 0xFF for index in range(256))


def minimal_kernel(seed: int, size: int = 4096) -> bytes:
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


def base_image(kernel: bytes) -> bytes:
    image = bytearray((PARTITION_LBA + PARTITION_SECTORS) * SECTOR_SIZE)
    image[510:512] = b"\x55\xaa"
    image[446] = 0x80
    image[450] = 0xDA
    struct.pack_into("<II", image, 454, PARTITION_LBA, PARTITION_SECTORS)
    for manifest_lba, kernel_lba in (
            (0, KERNEL_RELATIVE_LBA),
            (BACKUP_MANIFEST_RELATIVE_LBA, KERNEL_B_RELATIVE_LBA)):
        manifest = create_manifest(
            4, kernel, PARTITION_SECTORS, SIGNATURE_A, kernel_lba
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


class SimulatedPowerLoss(RuntimeError):
    pass


class BootControlTests(unittest.TestCase):
    def _paths(self, directory: str):
        root = Path(directory)
        source = root / "source.img"
        kernel = root / "kernel-b.bin"
        signature = root / "kernel-b.sig"
        output = root / "updated.img"
        source.write_bytes(base_image(minimal_kernel(19)))
        kernel.write_bytes(minimal_kernel(23, 4608))
        signature.write_bytes(SIGNATURE_B)
        return source, kernel, signature, output

    @staticmethod
    def _verified(artifact, _signature, _policy, _openssl, _root):
        return hashlib.sha256(artifact.read_bytes()).hexdigest()

    def _update(self, paths, hook=None):
        source, kernel, signature, output = paths
        update_inactive_slot(
            source, kernel, signature, output,
            ROOT / "safety/boot_trust_policy.json", Path("openssl"), ROOT,
            verifier=self._verified, boundary_hook=hook,
        )

    def test_record_round_trip_has_fixed_crc_and_semantics(self):
        record = create_boot_control_record()
        info = parse_boot_control_record(
            record, BOOT_CONTROL_PRIMARY_RELATIVE_LBA
        )
        self.assertEqual(len(record), SECTOR_SIZE)
        self.assertEqual(info.sequence, 1)
        self.assertEqual(info.active_slot, 0)
        self.assertEqual(info.pending_slot, 0xFF)
        self.assertEqual(info.attempts_remaining, 0)
        self.assertEqual(info.successful_mask, 1)

        confirmed_b = parse_boot_control_record(
            create_boot_control_record(
                sequence=7, active_slot=BOOT_CONTROL_SLOT_B,
                successful_mask=3,
            ),
            BOOT_CONTROL_SECONDARY_RELATIVE_LBA,
        )
        self.assertEqual(confirmed_b.active_slot, BOOT_CONTROL_SLOT_B)
        self.assertEqual(confirmed_b.pending_slot, 0xFF)
        self.assertEqual(confirmed_b.successful_mask, 3)

    def test_redundancy_recovers_one_copy_and_rejects_ambiguity(self):
        image = bytearray(base_image(minimal_kernel(29)))
        primary = (PARTITION_LBA + BOOT_CONTROL_PRIMARY_RELATIVE_LBA) * 512
        secondary = (PARTITION_LBA + BOOT_CONTROL_SECONDARY_RELATIVE_LBA) * 512
        image[primary + 40] ^= 0x01
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "one-valid.img"
            path.write_bytes(image)
            self.assertEqual(validate_image(path, "hdd").boot_control_sequence, 1)
            image[secondary + 40] ^= 0x01
            path.write_bytes(image)
            with self.assertRaisesRegex(ValueError, "both boot-control records"):
                validate_image(path, "hdd")

        first = create_boot_control_record(sequence=5)
        divergent = create_boot_control_record(
            sequence=5, pending_slot=BOOT_CONTROL_SLOT_B,
            attempts_remaining=2,
        )
        image = bytearray(base_image(minimal_kernel(31)))
        image[primary:primary + 512] = first
        image[secondary:secondary + 512] = divergent
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "ambiguous.img"
            path.write_bytes(image)
            with self.assertRaisesRegex(ValueError, "equal-sequence"):
                validate_image(path, "hdd")

    def test_updater_verifies_before_output_and_preserves_source(self):
        with tempfile.TemporaryDirectory() as directory:
            paths = self._paths(directory)
            source_before = paths[0].read_bytes()

            def reject(*_args):
                raise ValueError("signature rejected")

            with self.assertRaisesRegex(ValueError, "signature rejected"):
                update_inactive_slot(
                    paths[0], paths[1], paths[2], paths[3],
                    ROOT / "safety/boot_trust_policy.json", Path("openssl"),
                    ROOT, verifier=reject,
                )
            self.assertFalse(paths[3].exists())
            self.assertEqual(paths[0].read_bytes(), source_before)

            self._update(paths)
            final = validate_image(paths[3], "hdd")
            self.assertEqual(final.boot_control_sequence, 2)
            self.assertEqual(final.pending_slot, BOOT_CONTROL_SLOT_B)
            self.assertEqual(final.attempts_remaining, 2)
            self.assertEqual(paths[0].read_bytes(), source_before)
            updated = paths[3].read_bytes()
            active_start = (PARTITION_LBA + KERNEL_RELATIVE_LBA) * 512
            self.assertEqual(
                source_before[active_start:active_start + 4096],
                updated[active_start:active_start + 4096],
            )

    def test_every_durable_boundary_is_old_or_bootable_pending_state(self):
        boundaries = (
            "kernel-b", "manifest-b", "control-first", "control-second"
        )
        for boundary in boundaries:
            with self.subTest(boundary=boundary), \
                    tempfile.TemporaryDirectory() as directory:
                paths = self._paths(directory)

                def stop(name, _path):
                    if name == boundary:
                        raise SimulatedPowerLoss(name)

                with self.assertRaisesRegex(SimulatedPowerLoss, boundary):
                    self._update(paths, stop)
                with paths[3].open("rb") as image:
                    control = read_boot_control(image, PARTITION_LBA)
                if boundary in ("kernel-b", "manifest-b"):
                    self.assertEqual(control.pending_slot, 0xFF)
                else:
                    self.assertEqual(control.pending_slot, BOOT_CONTROL_SLOT_B)
                    self.assertEqual(control.attempts_remaining, 2)
                if boundary != "kernel-b":
                    validate_image(paths[3], "hdd")

    def test_stage2_persists_attempt_before_pending_kernel_verification(self):
        stage2 = (ROOT / "arch/x86/boot/bios/stage2_bios.asm").read_text(
            encoding="utf-8"
        )
        prepare = stage2.split("prepare_boot_candidate:", 1)[1].split(
            "load_boot_control:", 1
        )[0]
        pending = prepare.split(".pending:", 1)[1].split(".rollback:", 1)[0]
        self.assertLess(
            pending.index("call persist_boot_control"),
            pending.index("mov dword [manifest_relative_lba], BACKUP_MANIFEST_LBA"),
        )
        self.assertIn("BOOT_CONTROL_PENDING_B attempts=1", stage2)
        self.assertIn("BOOT_CONTROL_PENDING_B attempts=0", stage2)
        self.assertIn("BOOT_CONTROL_ROLLBACK_A", stage2)
        self.assertIn("mov ax, 0x4300", stage2)
        write_copy = stage2.split("write_boot_control_copy:", 1)[1].split(
            "write_bounce_sector:", 1
        )[0]
        self.assertIn("call read_bounce", write_copy)
        self.assertIn("repe cmpsw", write_copy)

    def test_runtime_uses_three_short_persistent_qemu_boots(self):
        runner = (ROOT / "scripts/run_qemu_boot_control.py").read_text(
            encoding="utf-8"
        )
        runtime = (ROOT / "scripts/test-reist-runtime.ps1").read_text(
            encoding="utf-8"
        )
        self.assertIn("persistent=True", runner)
        self.assertIn("BOOT_CONTROL_PENDING_B attempts=1", runner)
        self.assertIn("BOOT_CONTROL_PENDING_B attempts=0", runner)
        self.assertIn("BOOT_CONTROL_ROLLBACK_A", runner)
        self.assertIn("'boot-control'", runtime)
        self.assertIn("run_qemu_boot_control.py", runtime)


if __name__ == "__main__":
    unittest.main()
