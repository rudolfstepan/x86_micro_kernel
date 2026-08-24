"""Contracts for bounded emulator timing baselines."""

from __future__ import annotations

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VALIDATOR_PATH = ROOT / "scripts" / "validate_wcet_budgets.py"
SPEC = importlib.util.spec_from_file_location("validate_wcet_budgets", VALIDATOR_PATH)
VALIDATOR = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(VALIDATOR)


class ReistWcetContracts(unittest.TestCase):
    def read(self, relative: str) -> str:
        return (ROOT / relative).read_text(encoding="utf-8")

    def test_machine_budget_is_bounded_and_disclaims_hardware_wcet(self) -> None:
        budget = ROOT / "safety" / "wcet_budgets.json"
        self.assertEqual(VALIDATOR.validate(budget), [])
        document = json.loads(budget.read_text(encoding="utf-8"))
        document["platforms"]["qemu"]["scheduler_decision_max_ns"] = 10_000_001
        with tempfile.TemporaryDirectory() as temporary:
            invalid = Path(temporary) / "wcet.json"
            invalid.write_text(json.dumps(document), encoding="utf-8")
            self.assertTrue(VALIDATOR.validate(invalid))

    def test_timing_abi_is_append_only_fixed_and_default_deny(self) -> None:
        libc = self.read("lib/libc/stdlib.h")
        sdk_h = self.read("userspace/sdk/include/x86os.h")
        sdk_c = self.read("userspace/sdk/x86os.c")
        syscall = self.read("kernel/syscall/syscall_table.c")
        process = self.read("kernel/proc/process.c")
        process_h = self.read("kernel/proc/process.h")
        self.assertIn("#define SYS_RUNTIME_TIMING 116", libc)
        self.assertIn("X86OS_SYS_RUNTIME_TIMING = 116", sdk_h)
        self.assertIn("X86OS_RUNTIME_TIMING_VERSION 1U", sdk_h)
        self.assertIn("sizeof(x86os_runtime_timing_t) == 72U", sdk_c)
        self.assertIn("(void*)&syscall_runtime_timing", syscall)
        self.assertIn("case SYS_RUNTIME_TIMING", syscall)
        self.assertIn("PROCESS_DOMAIN_SYSCALL_LIMIT 124U", process_h)
        probe = process[process.index("static const uint8_t probe_syscalls[]"):]
        self.assertIn("SYS_RUNTIME_TIMING", probe)

    def test_measurement_is_saturating_irq_safe_and_non_authoritative(self) -> None:
        scheduler = self.read("kernel/sched/scheduler.c")
        header = self.read("kernel/sched/scheduler.h")
        self.assertIn("RUNTIME_TIMING_STATS_VERSION 1U", header)
        self.assertIn("sizeof(runtime_timing_stats_t) == 72U", scheduler)
        self.assertIn("saturating_add_u64", scheduler)
        self.assertIn("saturating_increment_u64", scheduler)
        self.assertIn("irq_save()", scheduler)
        self.assertIn("cpu_cycle_counter_read()", scheduler)
        self.assertLess(scheduler.index("if (cpu_frequency == 0U)"),
                        scheduler.index("if (end_cycles < start_cycles)"))
        self.assertIn("runtime_timing_record_syscall", scheduler)
        self.assertNotIn("k_malloc", scheduler[scheduler.index(
            "static runtime_timing_stats_t"):scheduler.index(
            "int create_task")])

    def test_scheduler_measurement_finishes_before_context_switch(self) -> None:
        scheduler = self.read("kernel/sched/scheduler.c")
        body = scheduler[scheduler.index("void scheduler_interrupt_handler"):]
        body = body[:body.index("void scheduler_preempt_disable")]
        self.assertIn("runtime_timing_begin", body)
        self.assertGreaterEqual(body.count("runtime_timing_finish_scheduler"), 4)
        for position in [index for index in range(len(body))
                         if body.startswith("swtch(", index)]:
            self.assertGreater(body.rfind("runtime_timing_finish_scheduler", 0, position), -1)

    def test_supervised_probe_publishes_only_after_finite_samples(self) -> None:
        probe = self.read("userspace/programs/reist_probe.c")
        supervisor = self.read("kernel/init/supervisor.c")
        runtime = self.read("scripts/test-reist-runtime.ps1")
        self.assertIn("REIST_WCET BASELINE", supervisor)
        self.assertIn("probe_wcet_baseline_reported", supervisor)
        self.assertIn("REIST_WCET_MINIMUM_SAMPLES 64U", probe)
        self.assertIn("REIST_WCET_SAMPLE_INTERVAL_MS 20U", probe)
        self.assertIn("REIST_WCET_SAMPLE_DEADLINE_MS 15000U", probe)
        self.assertIn("x86os_runtime_timing", probe)
        self.assertIn("X86OS_REIST_REPORT_WCET_BASELINE", probe)
        self.assertIn("X86OS_REIST_REPORT_WCET_REJECT", probe)
        self.assertIn("REIST_WCET REJECT reason=%u", supervisor)
        duplicate = supervisor[
            supervisor.index("if (probe_wcet_baseline_reported)"):
            supervisor.index("probe_wcet_baseline_reported = true")
        ]
        self.assertIn("return 0;", duplicate)
        self.assertNotIn("return -1;", duplicate)
        self.assertNotIn("x86os_puts(report)", probe)
        self.assertIn("'wcet-baseline'", runtime)
        self.assertIn("AddSeconds(45)", runtime)


if __name__ == "__main__":
    unittest.main()
