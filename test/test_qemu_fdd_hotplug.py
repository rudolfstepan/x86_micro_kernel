from __future__ import annotations

import importlib.util
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "run_qemu_fdd_hotplug.py"
SPEC = importlib.util.spec_from_file_location("run_qemu_fdd_hotplug", SCRIPT)
RUNNER = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(RUNNER)


class QemuFddHotplugTests(unittest.TestCase):
    def test_generated_floppy_is_valid_fat12_with_probe_file(self):
        with tempfile.TemporaryDirectory() as directory:
            image = Path(directory) / "hotplug.img"
            RUNNER.create_test_floppy(image)
            data = image.read_bytes()
        self.assertEqual(len(data), 1_474_560)
        self.assertEqual(data[510:512], b"\x55\xAA")
        self.assertEqual(data[54:62], b"FAT12   ")
        root = 19 * 512
        self.assertEqual(data[root:root + 11], b"HOTPLUG TXT")
        self.assertEqual(data[33 * 512:33 * 512 + 14], b"REIST-HOTPLUG\n")

    def test_qemu_uses_hdd_boot_qmp_and_named_removable_floppy(self):
        command = RUNNER.qemu_command(
            Path("qemu-system-i386"), Path("system.img"),
            Path("hotplug.img"), 43123)
        joined = " ".join(str(part) for part in command)
        self.assertIn("-boot c", joined)
        self.assertIn("if=floppy", joined)
        self.assertIn("id=reistfloppy", joined)
        self.assertIn("-qmp tcp:127.0.0.1:43123,server=on,wait=off", joined)
        self.assertIn("-snapshot", command)

    def test_guest_contract_waits_for_real_disconnect_and_reintegration(self):
        guest = (ROOT / "examples/userspace/guest_test.c").read_text("utf-8")
        self.assertIn('text_equal(argv[1], "FDD_HOTPLUG")', guest)
        armed = guest.index('"REIST_FDD HOTPLUG_ARMED')
        disconnected = guest.index('"REIST_FDD DISCONNECT_DETECTED')
        reintegrated = guest.index('"TEST_STAGE FDD_HOTPLUG_REINTEGRATED_OK')
        self.assertLess(armed, disconnected)
        self.assertLess(disconnected, reintegrated)

    def test_hotplug_gate_and_hazard_are_part_of_the_reist_contract(self):
        makefile = (ROOT / "Makefile").read_text("utf-8")
        runtime = (ROOT / "scripts/test-reist-runtime.ps1").read_text("utf-8")
        hazards = (ROOT / "safety/hazards.toml").read_text("utf-8")
        roadmap = (ROOT / "docs/development/OS_GAP_ANALYSIS_AND_ROADMAP.md").read_text("utf-8")
        self.assertIn("test-smoke-fdd-hotplug: native-image", makefile)
        self.assertIn("'fdd-hotplug'", runtime)
        self.assertIn('id = "HZ-MEDIA-001"', hazards)
        self.assertIn("RESOURCE_REINTEGRATED_RW 1", roadmap)


if __name__ == "__main__":
    unittest.main()
