import ast
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class QemuDriverDomainContracts(unittest.TestCase):
    def test_runner_is_bounded_and_requires_all_failure_classes(self):
        source = (ROOT / "scripts/run_qemu_driver_domain.py").read_text(
            encoding="utf-8")
        ast.parse(source)
        self.assertIn("time.monotonic() + timeout", source)
        for marker in (
                "AP_EXEC cpu=", "CRASH_RECOVERED", "HANG_RECOVERED",
                "STALE_GENERATION_REJECTED", "RESET_FAILURE_FENCED",
                "RESTART_BUDGET_EXHAUSTED", "smoke.SHELL_PROMPT"):
            self.assertIn(marker, source)
        self.assertIn('count("DRIVER_DOMAIN AP_EXEC cpu=")', source)
        self.assertIn("ap_executions < 2", source)
        self.assertIn("smoke.qemu_command(qemu, image, smp=4)", source)
        self.assertIn('smoke.SHELL_PROMPT not in "".join(transcript)', source)

    def test_fault_fixture_is_compile_time_only(self):
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        windows = (ROOT / "scripts/build-windows.ps1").read_text(
            encoding="utf-8")
        runtime = (ROOT / "scripts/test-reist-runtime.ps1").read_text(
            encoding="utf-8")
        device = (ROOT / "kernel/init/device_domain.c").read_text(
            encoding="utf-8")
        kernel = (ROOT / "kernel/init/kernel.c").read_text(encoding="utf-8")
        self.assertIn("DRIVER_DOMAIN_FAULT_INJECTION ?= 0", makefile)
        self.assertIn("-DREIST_DRIVER_DOMAIN_FAULT_INJECTION", makefile)
        self.assertIn("#ifdef REIST_DRIVER_DOMAIN_FAULT_INJECTION", device)
        self.assertIn("device_domain_fault_test_register", kernel)
        self.assertIn(".cpu_affinity_mask = driver_fault_ap_mask", kernel)
        self.assertIn("reset_fault_config", kernel)
        self.assertIn("[switch]$DriverDomainFaultInjection", windows)
        self.assertIn("DRIVER_DOMAIN_FAULT_INJECTION=1", windows)
        self.assertIn("-DriverDomainFaultInjection", runtime)
        self.assertIn("Start-Process -FilePath $Python -Wait -PassThru", runtime)
        self.assertIn("$exitCode = $runner.ExitCode", runtime)


if __name__ == "__main__":
    unittest.main()
