"""Contracts for deterministic, test-only memory failure injection."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class MemoryFaultInjectionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        cls.header = (ROOT / "mm/kmalloc.h").read_text(encoding="utf-8")
        cls.source = (ROOT / "mm/kmalloc.c").read_text(encoding="utf-8")

    def test_injection_is_absent_from_normal_build(self) -> None:
        self.assertIn("MEMORY_FAULT_INJECTION ?= 0", self.makefile)
        self.assertIn("-DREIST_MEMORY_FAULT_INJECTION", self.makefile)
        self.assertIn("#ifdef REIST_MEMORY_FAULT_INJECTION", self.header)
        self.assertNotIn("SYS_MEMORY_FAULT", self.makefile + self.header)

    def test_heap_and_frame_failures_use_bounded_countdowns(self) -> None:
        self.assertIn("heap_fault_countdown", self.source)
        self.assertIn("frame_fault_countdown", self.source)
        self.assertIn("memory_fault_injection_disarm", self.source)
        self.assertNotIn("while (heap_fault", self.source)
        self.assertNotIn("while (frame_fault", self.source)

    def test_boot_test_proves_rollback_and_slot_reuse(self) -> None:
        self.assertIn("after.heap_used_bytes != before.heap_used_bytes",
                      self.source)
        self.assertIn("after.allocated_frame_bytes != before.allocated_frame_bytes",
                      self.source)
        self.assertIn("after.free_frame_bytes != before.free_frame_bytes",
                      self.source)
        self.assertIn("scheduler_kernel_stack_is_valid(stack)", self.source)
        self.assertIn("REIST_MEMORY_FAULT_INJECTION_OK", self.source)

    def test_qemu_gate_builds_a_separate_fault_image(self) -> None:
        self.assertIn("test-smoke-memory-fault:", self.makefile)
        self.assertIn("MEMORY_FAULT_INJECTION=1", self.makefile)
        self.assertIn("build/memory-fault-injection/reist-os.img", self.makefile)
        self.assertIn("--expect-memory-fault", self.makefile)
        runner = (ROOT / "scripts/run_qemu_smoke.py").read_text(encoding="utf-8")
        self.assertIn("REIST_MEMORY_FAULT_MARKER", runner)
        self.assertIn("missing pre-boot memory fault-injection marker", runner)


if __name__ == "__main__":
    unittest.main()
