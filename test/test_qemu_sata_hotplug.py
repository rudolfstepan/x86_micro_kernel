"""Contracts for destructive SATA backend faults on a disposable QEMU disk."""

from __future__ import annotations

import importlib.util
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RUNNER_PATH = ROOT / "scripts" / "run_qemu_sata_hotplug.py"


class QemuSataHotplugTests(unittest.TestCase):
    def test_guest_proves_independent_ring3_progress(self) -> None:
        source = (ROOT / "userspace" / "programs" /
                  "sata_write_test.c").read_text(encoding="utf-8")
        active = source.index('"SATA_WRITE ACTIVE')
        self.assertLess(source.index(
            'x86os_spawn("/libexec/reist/sleeper.prg")'), active)
        self.assertGreater(source.index("x86os_wait(progress_pid"), active)
        self.assertIn('"SATA_WRITE INDEPENDENT_PROGRESS_OK', source)

    def test_runner_uses_disposable_ahci_system_disk(self) -> None:
        source = RUNNER_PATH.read_text(encoding="utf-8")
        self.assertIn("shutil.copyfile", source)
        self.assertIn("file_sha256(reference_image)", source)
        self.assertIn("node-name=reistdisk", source)
        self.assertIn("ich9-ahci,id=reistahci", source)
        self.assertIn("id=reistsystemdisk", source)
        self.assertIn("werror=report,rerror=report", source)
        self.assertNotIn('"-snapshot"', source)

    def test_runner_requires_ordered_disconnect_and_reintegration(self) -> None:
        source = RUNNER_PATH.read_text(encoding="utf-8")
        active = source.index("ACTIVE, deadline")
        deactivate = source.index('"active": False', active)
        io_error = source.index("IO_ERROR, deadline", deactivate)
        reactivate = source.index('"active": True', io_error)
        reintegrated = source.index("REINTEGRATED,", reactivate)
        progress = source.index("INDEPENDENT_PROGRESS,", reintegrated)
        recovered = source.index("RECOVERY_RW", progress)
        complete = source.index("TEST_OK, deadline", recovered)
        self.assertLess(active, deactivate)
        self.assertLess(deactivate, io_error)
        self.assertLess(io_error, reactivate)
        self.assertLess(reactivate, reintegrated)
        self.assertLess(reintegrated, progress)
        self.assertLess(progress, recovered)
        self.assertLess(recovered, complete)
        self.assertIn('qmp.execute("blockdev-set-active"', source)
        self.assertIn("RECOVERY_EXHAUSTED", source)

    def test_qemu_command_has_stable_ahci_device_identity(self) -> None:
        spec = importlib.util.spec_from_file_location("sata_hotplug", RUNNER_PATH)
        module = importlib.util.module_from_spec(spec)
        assert spec.loader is not None
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as temporary:
            disk = Path(temporary) / "disk.img"
            command = module.qemu_command(Path("qemu-system-i386"), disk,
                                          43123)
        joined = " ".join(str(value) for value in command)
        self.assertIn("id=reistsystemdisk", joined)
        self.assertIn("tcp:127.0.0.1:43123", joined)
        self.assertNotIn("-snapshot", command)


if __name__ == "__main__":
    unittest.main()
