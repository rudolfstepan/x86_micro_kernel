"""Host/source regressions for R1.1 wait queues, sleep, yield and time."""

from __future__ import annotations

import importlib.util
import os
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GCC = shutil.which("gcc")


def extract_block(source: str, opening_brace: int) -> str:
    depth = 0
    for index in range(opening_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening_brace:index + 1]
    raise AssertionError("unterminated C block")


def function_block(source: str, signature: str) -> str:
    start = source.index(signature)
    return extract_block(source, source.index("{", start))


def case_block(source: str, name: str) -> str:
    match = re.search(
        rf"\bcase\s+{re.escape(name)}\s*:(?P<body>.*?)"
        r"(?=\n\s*(?:case\s+\w+\s*:|default\s*:))",
        source,
        flags=re.DOTALL,
    )
    if not match:
        raise AssertionError(f"missing dispatcher case {name}")
    return match.group("body")


def assigned_integer(source: str, name: str) -> int:
    match = re.search(rf"\b{re.escape(name)}\s*=\s*(\d+)\b", source)
    if not match:
        match = re.search(rf"^\s*#define\s+{re.escape(name)}\s+(\d+)\b",
                          source, flags=re.MULTILINE)
    if not match:
        raise AssertionError(f"missing integer assignment for {name}")
    return int(match.group(1))


@unittest.skipUnless(GCC, "gcc is required for the wait-queue host harness")
class WaitQueueHostTests(unittest.TestCase):
    def test_raw_intrusive_queue_invariants(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / (
                "test_wait_queue_host.exe" if os.name == "nt"
                else "test_wait_queue_host"
            )
            subprocess.run(
                [
                    GCC,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    f"-I{ROOT}",
                    str(ROOT / "test/test_wait_queue_host.c"),
                    str(ROOT / "kernel/sched/wait_queue.c"),
                    "-o",
                    str(executable),
                ],
                check=True,
                cwd=ROOT,
                capture_output=True,
            )
            subprocess.run(
                [str(executable)],
                check=True,
                cwd=ROOT,
                capture_output=True,
                timeout=10,
            )


class SchedulerTimeSourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.scheduler = (ROOT / "kernel/sched/scheduler.c").read_text(
            encoding="utf-8"
        )
        cls.scheduler_header = (
            ROOT / "kernel/sched/scheduler.h"
        ).read_text(encoding="utf-8")
        cls.pit = (ROOT / "kernel/time/pit.c").read_text(encoding="utf-8")
        cls.pit_header = (ROOT / "kernel/time/pit.h").read_text(
            encoding="utf-8"
        )
        cls.syscalls = (ROOT / "kernel/syscall/syscall_table.c").read_text(
            encoding="utf-8"
        )
        cls.irq = (ROOT / "arch/x86/cpu/irq.c").read_text(encoding="utf-8")

    def test_scheduler_exports_the_r1_blocking_operations(self) -> None:
        compact = re.sub(r"\s+", " ", self.scheduler_header)
        for declaration in (
            "int wait_queue_block_locked(wait_queue_t *queue, "
            "task_block_kind_t kind);",
            "bool wait_queue_wake_one_locked(wait_queue_t *queue);",
            "size_t wait_queue_wake_all_locked(wait_queue_t *queue);",
            "int scheduler_sleep_ms(uint32_t milliseconds);",
            "int scheduler_yield(void);",
            "void scheduler_wake_expired_sleepers_locked(uint64_t now_ms);",
        ):
            with self.subTest(declaration=declaration):
                self.assertIn(re.sub(r"\s+", " ", declaration), compact)

    def test_generic_block_and_wake_paths_update_scheduler_state(self) -> None:
        block = function_block(
            self.scheduler, "static int wait_queue_block_until_task_locked("
        )
        wake_one = function_block(
            self.scheduler, "static bool wait_queue_wake_one_task_locked("
        )
        wake_all = function_block(
            self.scheduler, "size_t wait_queue_wake_all_locked("
        )
        self.assertIn("wait_queue_push_locked(", block)
        self.assertIn("TASK_BLOCK_WAITING", block)
        self.assertIn("TASK_BLOCK_SLEEPING", block)
        self.assertIn("schedule_blocked_current_locked(", block)
        self.assertIn("wait_queue_pop_locked(", wake_one)
        self.assertIn("TASK_READY", wake_one)
        self.assertIn("wait_queue_wake_all_task_locked(queue)", wake_all)

    def test_sleep_uses_an_ordered_deadline_queue_and_does_not_poll(self) -> None:
        sleep = function_block(self.scheduler, "int scheduler_sleep_ms(")
        compact = re.sub(r"\s+", " ", sleep)
        self.assertRegex(compact, r"if \(milliseconds == 0\) return 0;")
        self.assertIn("uint64_t now = pit_monotonic_ms();", compact)
        self.assertIn("uint64_t deadline", compact)
        self.assertIn("wait_queue_insert_ordered_locked(", sleep)
        self.assertIn("TASK_SLEEPING", sleep)
        self.assertIn("schedule_blocked_current_locked(", sleep)
        self.assertNotIn("pit_delay(", sleep)
        self.assertNotRegex(sleep, r"\bwhile\s*\(")

    def test_expired_sleepers_are_woken_in_deadline_order(self) -> None:
        wake = function_block(
            self.scheduler,
            "void scheduler_wake_expired_sleepers_locked(",
        )
        compact = re.sub(r"\s+", " ", wake)
        self.assertIn("sleep_waiters.head->key <= now_ms", compact)
        self.assertIn("wait_queue_wake_one_task_locked(&sleep_waiters)",
                      compact)

    def test_timed_waiters_use_bounded_task_scan(self) -> None:
        wake = function_block(
            self.scheduler, "void scheduler_wake_expired_waiters_locked("
        )
        self.assertIn("MAX_TASKS", wake)
        self.assertIn("wait_queue_remove_locked(", wake)
        self.assertIn("TASK_READY", wake)
        self.assertIn("-110", wake)

    def test_yield_selects_another_ready_task_without_polling(self) -> None:
        yield_block = function_block(self.scheduler, "int scheduler_yield(")
        self.assertIn("claim_next_runnable(", yield_block)
        self.assertIn("TASK_READY", yield_block)
        self.assertIn("swtch(", yield_block)
        self.assertNotRegex(yield_block, r"\bwhile\s*\(")

    def test_monotonic_clock_is_64_bit_and_read_atomically_on_i386(self) -> None:
        self.assertRegex(
            self.pit,
            r"static\s+volatile\s+uint64_t\s+timer_tick_count\s*;",
        )
        self.assertIn("uint64_t pit_monotonic_ms(void);", self.pit_header)
        read = function_block(self.pit, "uint64_t pit_monotonic_ms(")
        self.assertIn("timer_tick_sequence", read)
        self.assertIn("PIT_MONOTONIC_READ_RETRY_LIMIT", read)
        self.assertIn("before == after", read)
        self.assertIn("panic(\"PIT monotonic clock read timed out\")", read)
        irq = function_block(self.pit, "void timer_irq_handler(")
        self.assertIn("scheduler_wake_expired_sleepers_locked(", irq)

    def test_pit_fallback_schedules_only_after_pic_eoi(self) -> None:
        irq_handler = function_block(self.irq, "void irq_handler(")
        master_eoi = irq_handler.index("outb(0x20, 0x20)")
        fallback = irq_handler.index("scheduler_pit_interrupt_handler()")
        self.assertLess(master_eoi, fallback)
        pit_fallback = function_block(
            self.scheduler, "void scheduler_pit_interrupt_handler("
        )
        self.assertIn("scheduler_interrupt_handler();", pit_fallback)

    def test_delay_syscall_keeps_number_two_but_uses_blocking_user_path(self):
        legacy = case_block(self.syscalls, "SYS_DELAY")
        self.assertIn("syscall_delay(regs, arg1)", legacy)
        self.assertNotIn("pit_delay(", legacy)
        table_start = self.syscalls.index("void* syscall_table[")
        table = extract_block(
            self.syscalls, self.syscalls.index("{", table_start)
        )
        self.assertRegex(
            table,
            r"\(void\*\)&scheduler_sleep_ms,\s*// Syscall 2:",
        )
        self.assertNotRegex(
            table,
            r"\(void\*\)&pit_delay,\s*// Syscall 2:",
        )
        helper = function_block(self.syscalls, "static int syscall_delay(")
        self.assertLess(helper.index("scheduler_sleep_ms(milliseconds)"),
                        helper.index("pit_delay(milliseconds)"))
        self.assertIn("(regs->cs & 3U) == 3U", helper)

    def test_new_syscalls_dispatch_to_scheduler_and_64_bit_copyout(self):
        self.assertIn("scheduler_yield()", case_block(self.syscalls, "SYS_YIELD"))
        self.assertIn(
            "syscall_delay(regs, arg1)",
            case_block(self.syscalls, "SYS_SLEEP_MS"),
        )
        self.assertIn(
            "syscall_monotonic_ms(",
            case_block(self.syscalls, "SYS_MONOTONIC_MS"),
        )
        monotonic = function_block(
            self.syscalls, "static int syscall_monotonic_ms("
        )
        self.assertIn("uint64_t value = pit_monotonic_ms();", monotonic)
        self.assertIn("copy_to_user(", monotonic)
        self.assertIn("sizeof(value)", monotonic)


class SchedulerTimeAbiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.kernel_api = (ROOT / "lib/libc/stdlib.h").read_text(
            encoding="utf-8"
        )
        cls.user_header = (
            ROOT / "userspace/sdk/include/x86os.h"
        ).read_text(encoding="utf-8")
        cls.user_sdk = (ROOT / "userspace/sdk/x86os.c").read_text(
            encoding="utf-8"
        )

    def test_syscall_numbers_are_stable_and_consistent(self) -> None:
        expected = {
            "DELAY": 2,
            "YIELD": 40,
            "SLEEP_MS": 41,
            "MONOTONIC_MS": 42,
        }
        for suffix, number in expected.items():
            with self.subTest(syscall=suffix):
                self.assertEqual(
                    assigned_integer(self.kernel_api, f"SYS_{suffix}"), number
                )
                self.assertEqual(
                    assigned_integer(self.user_header, f"X86OS_SYS_{suffix}"),
                    number,
                )

    def test_sdk_wrappers_forward_the_exact_r1_arguments(self) -> None:
        delay = function_block(self.user_sdk, "void x86os_delay(")
        sleep = function_block(self.user_sdk, "int x86os_sleep_ms(")
        yield_block = function_block(self.user_sdk, "int x86os_yield(")
        monotonic = function_block(self.user_sdk, "int x86os_monotonic_ms(")
        self.assertIn("X86OS_SYS_DELAY, milliseconds, 0, 0", delay)
        self.assertIn("X86OS_SYS_SLEEP_MS, milliseconds, 0, 0", sleep)
        self.assertIn("X86OS_SYS_YIELD, 0, 0, 0", yield_block)
        self.assertIn("X86OS_SYS_MONOTONIC_MS", monotonic)
        self.assertIn("(uintptr_t)value, 0, 0", monotonic)


class SchedulerTimeGuestAndPackagingTests(unittest.TestCase):
    def test_guest_exercises_sleep_yield_time_and_emits_stage_marker(self):
        guest = (ROOT / "userspace/programs/guest_test.c").read_text(
            encoding="utf-8"
        )
        for contract in (
            "x86os_yield()",
            "x86os_sleep_ms(0)",
            "x86os_monotonic_ms(",
            "X86OS_PROCESS_SLEEPING",
            'TEST_STAGE SCHED_TIME_OK\\n',
        ):
            with self.subTest(contract=contract):
                self.assertIn(contract, guest)

    def test_sleeper_program_is_built_and_packaged_in_both_images(self):
        programs = (ROOT / "scripts/build_system_programs.py").read_text(
            encoding="utf-8"
        )
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        self.assertIn('"SLEEPER.PRG"', programs)
        self.assertIn('"CAPWAIT.PRG"', programs)
        self.assertIn(
            "libexec/reist/sleeper.prg=$(SYSTEM_PROGRAM_DIR)/SLEEPER.PRG",
            makefile,
        )
        self.assertIn(
            "libexec/reist/capwait.prg=$(SYSTEM_PROGRAM_DIR)/CAPWAIT.PRG",
            makefile,
        )
        self.assertIn("$(foreach spec,$(SYSTEM_IMAGE_FILES),--data-file $(spec))", makefile)
        self.assertIn("$(foreach spec,$(FLOPPY_IMAGE_FILES),--data-file $(spec))", makefile)

    def test_runner_no_apic_mode_uses_the_qemu_cpu_flag(self) -> None:
        runner_path = ROOT / "scripts/run_qemu_smoke.py"
        spec = importlib.util.spec_from_file_location("qemu_smoke", runner_path)
        self.assertIsNotNone(spec)
        self.assertIsNotNone(spec.loader)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)

        normal = module.qemu_command(Path("qemu"), Path("image"), False)
        fallback = module.qemu_command(Path("qemu"), Path("image"), True)
        self.assertNotIn("qemu32,-apic", normal)
        cpu = fallback.index("-cpu")
        self.assertEqual(fallback[cpu + 1], "qemu32,-apic")

    def test_make_and_ci_run_a_separate_pit_fallback_smoke(self) -> None:
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        workflow = (ROOT / ".github/workflows/build.yml").read_text(
            encoding="utf-8"
        )
        self.assertRegex(makefile, r"(?m)^\.PHONY:.*\btest-smoke-pit\b")
        match = re.search(
            r"(?ms)^test-smoke-pit:\s+native-image\s*$"
            r"(?P<body>.*?)(?=^\S[^:]*:|\Z)",
            makefile,
        )
        self.assertIsNotNone(match)
        body = match.group("body")
        self.assertIn("scripts/run_qemu_smoke.py", body)
        self.assertIn("--no-apic", body)
        self.assertIn("guest-smoke-pit.log", body)
        normal = workflow.index("run: make test-smoke TARGET=qemu VIDEO=vga")
        fallback = workflow.index(
            "run: make test-smoke-pit TARGET=qemu VIDEO=vga"
        )
        self.assertLess(normal, fallback)


if __name__ == "__main__":
    unittest.main()
