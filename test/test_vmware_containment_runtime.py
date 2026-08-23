"""Contract tests for the bounded VMware containment runtime gate."""

from pathlib import Path
import tomllib
import unittest

ROOT = Path(__file__).resolve().parents[1]
WORKER = ROOT / "scripts/run_vmware_containment.ps1"
LAUNCHER = ROOT / "scripts/run_vmware_containment.cmd"
SCOPE = ROOT / "safety/assurance_scope.toml"
AUTOMATION = ROOT / "automation/reist-s03b.toml"


class VmwareContainmentRuntimeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.worker = WORKER.read_text(encoding="utf-8")
        cls.launcher = LAUNCHER.read_text(encoding="utf-8")
        cls.automation = AUTOMATION.read_text(encoding="utf-8")

    def test_launcher_is_registered_as_runtime_gate(self) -> None:
        self.assertIn('.\\\\scripts\\\\run_vmware_containment.cmd',
                      self.automation)

    def test_fresh_generated_package_is_booted_directly(self) -> None:
        self.assertIn("build\\vmware\\reist-os", self.worker)
        self.assertIn("Remove-Item -LiteralPath $serial -Force", self.worker)
        self.assertNotIn("Copy-Item", self.worker + self.launcher)

    def test_every_containment_wait_is_bounded(self) -> None:
        self.assertIn("[TimeSpan]::FromSeconds(60)", self.worker)
        self.assertIn("$attempt -le 3", self.worker)
        self.assertIn("@('nogui', 'nogui', 'gui')", self.worker)
        self.assertIn("-T ws start $vmx $startMode", self.worker)
        self.assertIn("-T ws stop $vmx hard", self.worker)
        self.assertIn("AddSeconds(5)", self.worker)

    def test_acceptance_requires_recovery_boot_and_ring3_shell(self) -> None:
        for marker in (
            "Watchdog: external backend required",
            "REIST_PROBE RECOVERY_SEQUENCE_OK",
            "BOOT_OK",
            "Starting userspace command interpreter from /bin/shell.prg",
            "REIST OS userspace shell",
        ):
            self.assertIn(marker, self.worker)
        for marker in ("*** KERNEL PANIC ***", "TEST_FAIL", "FATAL:"):
            self.assertIn(marker, self.worker)

    def test_automated_scope_excludes_physical_qualification(self) -> None:
        scope = tomllib.loads(SCOPE.read_text(encoding="utf-8"))
        self.assertEqual(["qemu-i386", "vmware-i386"],
                         scope["automated_targets"])
        self.assertEqual(["experimental-x86-target"],
                         scope["manual_targets"])
        self.assertIn("physical external-monitor qualification",
                      scope["manual_evidence_only"])


if __name__ == "__main__":
    unittest.main()
