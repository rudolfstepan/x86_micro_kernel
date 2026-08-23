import importlib.util
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RUNNER_SPEC = importlib.util.spec_from_file_location(
    "run_qemu_smoke", ROOT / "scripts/run_qemu_smoke.py")
RUNNER = importlib.util.module_from_spec(RUNNER_SPEC)
assert RUNNER_SPEC.loader is not None
RUNNER_SPEC.loader.exec_module(RUNNER)


class RuntimeDegradationTests(unittest.TestCase):
    def test_fault_profile_is_compile_time_only_and_buildable_both_ways(self):
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        windows = (ROOT / "scripts/build-windows.ps1").read_text(
            encoding="utf-8")
        kernel = (ROOT / "kernel/init/kernel.c").read_text(encoding="utf-8")
        self.assertIn("RUNTIME_DEGRADATION_FAULT_INJECTION ?= 0", makefile)
        self.assertIn("-DREIST_RUNTIME_DEGRADATION_FAULT_INJECTION", makefile)
        self.assertIn("[switch]$RuntimeDegradationFaultInjection", windows)
        self.assertIn("RUNTIME_DEGRADATION_FAULT_INJECTION=1", windows)
        guarded = kernel[kernel.index(
            "#ifdef REIST_RUNTIME_DEGRADATION_FAULT_INJECTION"):]
        guarded = guarded[:guarded.index("#endif")]
        self.assertIn("scheduler_policy_degradation_self_test()", guarded)
        self.assertIn("device_domain_irq_storm_self_test()", guarded)
        self.assertNotIn("syscall", guarded.lower())

    def test_fixed_guards_have_registered_authoritative_limits(self):
        header = (ROOT / "include/kernel/device_domain.h").read_text(
            encoding="utf-8")
        budgets = (ROOT / "safety/resource_budgets.toml").read_text(
            encoding="utf-8")
        self.assertIn("DEVICE_DOMAIN_IRQ_WINDOW_MS 100U", header)
        self.assertIn("DEVICE_DOMAIN_IRQ_WINDOW_LIMIT 128U", header)
        self.assertIn('symbol = "DEVICE_DOMAIN_IRQ_WINDOW_MS"', budgets)
        self.assertIn('symbol = "DEVICE_DOMAIN_IRQ_WINDOW_LIMIT"', budgets)
        for source_path in (
                "kernel/init/device_domain.c",
                "kernel/sched/scheduling_policy.c"):
            source = (ROOT / source_path).read_text(encoding="utf-8")
            self.assertNotIn("while (", source)

    def test_qemu_marker_must_precede_boot_and_normal_progress(self):
        transcript = "\n".join((
            RUNNER.REIST_RUNTIME_DEGRADATION_MARKER,
            RUNNER.BOOT_MARKER,
            RUNNER.TEST_MARKER,
            RUNNER.SHELL_PROMPT,
        ))
        self.assertIsNone(RUNNER.validate(
            transcript, expect_runtime_degradation=True))
        self.assertIn("pre-boot", RUNNER.validate(
            "\n".join((RUNNER.BOOT_MARKER,
                        RUNNER.REIST_RUNTIME_DEGRADATION_MARKER,
                        RUNNER.TEST_MARKER, RUNNER.SHELL_PROMPT)),
            expect_runtime_degradation=True))
        self.assertIn("runtime degradation", RUNNER.validate(
            "\n".join((RUNNER.BOOT_MARKER, RUNNER.TEST_MARKER,
                        RUNNER.SHELL_PROMPT)),
            expect_runtime_degradation=True))


if __name__ == "__main__":
    unittest.main()
