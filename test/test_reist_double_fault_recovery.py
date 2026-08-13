import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class ReistDoubleFaultRecoveryTests(unittest.TestCase):
    def test_injection_is_compile_time_gated_and_uses_vector_8(self):
        fatal = (ROOT / "kernel/init/fatal.c").read_text(encoding="utf-8")
        kernel = (ROOT / "kernel/init/kernel.c").read_text(encoding="utf-8")
        self.assertIn("#ifdef REIST_FAULT_INJECTION", fatal)
        self.assertIn('__asm__ __volatile__("int $8")', fatal)
        self.assertIn("#ifdef REIST_FAULT_INJECTION", kernel)
        self.assertIn("fatal_test_trigger_double_fault();", kernel)

    def test_injection_runs_once_after_recovered_record(self):
        kernel = (ROOT / "kernel/init/kernel.c").read_text(encoding="utf-8")
        self.assertIn("fatal_last_crash_record()->magic != FATAL_CRASH_RECORD_MAGIC", kernel)
        self.assertIn('"REIST_TEST FATAL_RECOVERY_OK\\n"', kernel)

    def test_make_uses_isolated_artifact_directory(self):
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        self.assertIn("test-smoke-fatal-recovery:", makefile)
        self.assertIn("OUTPUT_DIR=build/fatal-injection", makefile)
        self.assertIn("--expect-fatal-recovery", makefile)

    def test_windows_reference_builder_requires_explicit_switch(self):
        script = (ROOT / "scripts/build-windows.ps1").read_text(encoding="utf-8")
        self.assertIn("[switch]$FaultInjection", script)
        self.assertIn("$makeArguments += 'FAULT_INJECTION=1'", script)


if __name__ == "__main__":
    unittest.main()
