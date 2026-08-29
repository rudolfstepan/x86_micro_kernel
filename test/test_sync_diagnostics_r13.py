"""Source-contract regressions for the R1.3 synchronization/diagnostics work.

These tests deliberately describe public and cross-file contracts.  They are
not substitutes for the existing behavioural scheduler tests; their purpose is
to keep context checks, lock ordering and crash diagnostics from silently
drifting apart as more subsystems start sharing locks.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def compact(source: str) -> str:
    return re.sub(r"\s+", " ", source)


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


def macro_body(source: str, name: str) -> str:
    match = re.search(
        rf"(?ms)^\s*#define\s+{re.escape(name)}\s*\([^)]*\)\s*"
        rf"(?P<body>.*?)(?=^\s*#define\s+|^\s*(?:typedef|extern|void|int|bool|"
        rf"uint\w*_t|const)\b|\Z)",
        source,
    )
    if match is None:
        raise AssertionError(f"missing macro {name}")
    return match.group("body").replace("\\\n", " ")


class IrqAndSleepContextContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.interrupt_h = read("arch/x86/include/interrupt.h")
        cls.irq_c = read("arch/x86/cpu/irq.c")
        cls.scheduler_h = read("kernel/sched/scheduler.h")
        cls.scheduler_c = read("kernel/sched/scheduler.c")
        cls.panic_h = read("include/kernel/panic.h")
        cls.spinlock_h = read("include/lib/spinlock.h")

    def test_irq_context_has_a_nested_depth_contract(self) -> None:
        header = compact(self.interrupt_h)
        for declaration in (
            "void irq_context_enter(void);",
            "void irq_context_exit(void);",
        ):
            with self.subTest(declaration=declaration):
                self.assertIn(declaration, header)
        self.assertRegex(header, r"(?:bool|int) irq_in_context\(void\);")

        enter = function_block(self.irq_c, "void irq_context_enter(")
        leave = function_block(self.irq_c, "void irq_context_exit(")
        query_signature = (
            "bool irq_in_context("
            if "bool irq_in_context(" in self.irq_c
            else "int irq_in_context("
        )
        query = function_block(self.irq_c, query_signature)
        depth_names = set(re.findall(
            r"\b(?:(?:current_)?irq_)?context_depth\b", enter))
        self.assertEqual(len(depth_names), 1, "IRQ context must use a depth counter")
        depth = next(iter(depth_names))
        self.assertIn(depth, leave)
        self.assertIn(depth, query)
        self.assertRegex(enter, rf"(?:\+\+\s*{depth}|{depth}\s*\+\+|{depth}\s*\+=\s*1)")
        self.assertRegex(leave, rf"(?:--\s*{depth}|{depth}\s*--|{depth}\s*-=\s*1)")
        self.assertRegex(query, rf"return\s+{depth}\s*!=\s*0")
        self.assertRegex(
            enter,
            rf"KASSERT\s*\(\s*{depth}\s*(?:<\s*UINT32_MAX|!=\s*UINT32_MAX)\s*\)",
        )
        self.assertRegex(
            leave,
            rf"KASSERT\s*\(\s*{depth}\s*(?:>\s*0|!=\s*0)\s*\)",
        )

    def test_irq_dispatch_marks_the_entire_device_callback_window(self) -> None:
        handler = function_block(self.irq_c, "void irq_handler(")
        enter = handler.index("irq_context_enter();")
        callback = re.search(r"\bhandler\s*\(\s*regs\s*\)\s*;", handler)
        self.assertIsNotNone(callback, "IRQ dispatcher no longer invokes callbacks")
        leave = handler.index("irq_context_exit();")
        self.assertLess(enter, callback.start())
        self.assertLess(callback.end(), leave)
        master_eoi = handler.index("outb(0x20, 0x20)")
        self.assertLess(master_eoi, leave)
        self.assertEqual(handler.count("irq_context_enter();"), 1)
        self.assertEqual(handler.count("irq_context_exit();"), 1)
        # Scheduling may switch away and return only when the interrupted task
        # runs again.  A per-CPU depth must therefore be cleared before this
        # tail call or unrelated tasks would appear to run in hard IRQ context.
        timer = handler.index("scheduler_pit_interrupt_handler();")
        self.assertLess(leave, timer)

    def test_apic_irq_context_ends_before_the_scheduling_tail(self) -> None:
        apic = read("kernel/time/apic.c")
        handler = function_block(apic, "void apic_timer_isr(")
        enter = handler.index("irq_context_enter();")
        eoi = handler.index("apic[0xB0 / 4] = 0;")
        leave = handler.index("irq_context_exit();")
        schedule = handler.index("scheduler_interrupt_handler();")
        self.assertLess(enter, eoi)
        self.assertLess(eoi, leave)
        self.assertLess(leave, schedule)

    def test_preemption_and_sleep_queries_expose_real_context_state(self) -> None:
        header = compact(self.scheduler_h)
        self.assertIn("bool scheduler_preempt_is_disabled(void);", header)
        self.assertIn("bool scheduler_can_sleep(void);", header)

        disabled = function_block(
            self.scheduler_c, "bool scheduler_preempt_is_disabled("
        )
        can_sleep = function_block(self.scheduler_c, "bool scheduler_can_sleep(")
        self.assertRegex(disabled, r"preempt_disable_count\s*!=\s*0")
        for required in (
            "!irq_in_context()",
            "!scheduler_preempt_is_disabled()",
            "irq_enabled()",
        ):
            with self.subTest(required=required):
                self.assertIn(required, compact(can_sleep))

    def test_preemption_nesting_fails_closed_on_overflow_and_underflow(self) -> None:
        disable = function_block(
            self.scheduler_c, "void scheduler_preempt_disable("
        )
        enable = function_block(
            self.scheduler_c, "void scheduler_preempt_enable("
        )
        self.assertRegex(
            disable,
            r"KASSERT\s*\(\s*preempt_disable_count\s*"
            r"(?:<|!=)\s*UINT32_MAX\s*\)",
        )
        self.assertRegex(
            enable,
            r"KASSERT\s*\(\s*preempt_disable_count\s*"
            r"(?:>|!=)\s*0\s*\)",
        )
        self.assertRegex(disable, r"\+\+\s*preempt_disable_count")
        self.assertRegex(enable, r"--\s*preempt_disable_count")

    def test_named_context_assertions_fail_closed(self) -> None:
        expected = {
            "KASSERT_IRQ_DISABLED": r"!\s*irq_enabled\s*\(",
            "KASSERT_NOT_IRQ": r"!\s*irq_in_context\s*\(",
            "KASSERT_CAN_SLEEP": r"(?:scheduler_can_sleep\s*\(|"
            r"irq_enabled\s*\(.*!\s*irq_in_context\s*\(.*"
            r"!\s*scheduler_preempt_is_disabled\s*\()",
        }
        for name, condition in expected.items():
            with self.subTest(assertion=name):
                body = macro_body(self.panic_h, name)
                self.assertRegex(body, condition)
                self.assertRegex(body, r"\bKASSERT\s*\(")

    def test_raw_spinlocks_assert_context_and_misuse(self) -> None:
        bounded = function_block(
            self.spinlock_h, "static inline bool spinlock_acquire_bounded("
        )
        release = function_block(
            self.spinlock_h, "static inline void spinlock_release("
        )
        self.assertRegex(
            bounded,
            r"(?:KASSERT_IRQ_DISABLED\s*\(\s*\)|"
            r"KASSERT\s*\(\s*!\s*irq_enabled\s*\(\s*\)\s*\))",
        )
        self.assertIn("spin < spin_limit", bounded)
        self.assertIn("__sync_val_compare_and_swap", bounded)
        self.assertIn("uint32_t owner_token = cpu + 1U", bounded)
        self.assertIn("if (observed == owner_token)", bounded)
        self.assertIn("__builtin_return_address(0)", bounded)
        self.assertIn("(cpu << 24U) | (caller & 0x00FFFFFFU)", bounded)
        self.assertIn('panic("Recursive SMP spinlock acquisition")', bounded)
        acquire = function_block(
            self.spinlock_h, "static inline void spinlock_acquire("
        )
        self.assertIn("SPINLOCK_ACQUIRE_TIMED_SPIN_LIMIT", acquire)
        self.assertIn("SPINLOCK_ACQUIRE_TIMEOUT_MS", self.spinlock_h)
        self.assertIn("cpu_cycle_counter_read()", bounded)
        self.assertIn("cpu_frequency", bounded)
        self.assertIn("panic_context_set_result(-110", acquire)
        self.assertIn("(uint32_t)(uintptr_t)lock", acquire)
        self.assertIn("lock->owner_cpu << 24U", acquire)
        self.assertIn("caller & 0x00FFFFFFU", acquire)
        self.assertIn("KASSERT(lock->lock == cpu + 1U)", release)
        self.assertIn("lock->owner_cpu == cpu", release)

    def test_heap_entry_points_reject_hard_irq_context(self) -> None:
        memory = read("mm/kmalloc.c")
        for signature in ("void *k_malloc(", "void k_free("):
            with self.subTest(function=signature):
                block = function_block(memory, signature)
                self.assertIn("KASSERT_NOT_IRQ();", block)
                assertion = block.index("KASSERT_NOT_IRQ();")
                lock = block.index("spinlock_acquire_irq(")
                self.assertLess(
                    assertion,
                    lock,
                    "non-IRQ-safe heap APIs must reject IRQ context before "
                    "entering their internal lock",
                )

    def test_locked_scheduler_apis_assert_irqs_are_disabled(self) -> None:
        signatures = (
            "int scheduler_reap_finished_task_locked(",
            "void wait_queue_cancel_locked(",
            "int wait_queue_block_locked(",
            "bool wait_queue_wake_one_locked(",
            "size_t wait_queue_wake_all_locked(",
            "void scheduler_wake_expired_sleepers_locked(",
        )
        for signature in signatures:
            with self.subTest(function=signature):
                block = function_block(self.scheduler_c, signature)
                self.assertIn("KASSERT_IRQ_DISABLED();", block)

    def test_blocking_entry_points_assert_sleepable_context(self) -> None:
        for signature in (
            "int scheduler_sleep_ms(",
            "int scheduler_yield(",
        ):
            with self.subTest(function=signature):
                block = function_block(self.scheduler_c, signature)
                self.assertIn("KASSERT_CAN_SLEEP();", block)
                self.assertLess(
                    block.index("KASSERT_CAN_SLEEP();"),
                    block.index("irq_save()"),
                    "sleepability must be checked while the caller's IF state "
                    "is still observable",
                )

    def test_task_teardown_closes_vfs_state_before_scheduler_irq_lock(self) -> None:
        cases = (
            ("void scheduler_terminate_task(",
             "uint32_t flags = process_table_lock_irqsave();"),
            ("void task_exit_status(", "irq_disable()"),
        )
        for signature, mask_operation in cases:
            with self.subTest(function=signature):
                block = function_block(self.scheduler_c, signature)
                close = block.index("process_close_all_files(")
                mask = block.index(mask_operation)
                self.assertLess(
                    close,
                    mask,
                    "VFS/file-system teardown must finish before entering "
                    "the scheduler's IF=0 lock domain",
                )
                self.assertNotIn(
                    "process_close_all_files(",
                    block[mask:],
                    "VFS teardown may not run under the scheduler IRQ lock",
                )

    def test_process_terminate_drops_its_lookup_lock_before_vfs_teardown(self) -> None:
        process = read("kernel/proc/process.c")
        terminate = function_block(process, "int process_terminate(")
        call = terminate.index("scheduler_terminate_task(task_id);")
        restore = terminate.rfind(
            "process_table_unlock_irqrestore(flags);", 0, call
        )
        self.assertGreaterEqual(
            restore,
            0,
            "PID lookup synchronization must be released before termination "
            "can close files",
        )
        self.assertLess(restore, call)


class LockOrderingDocumentationTests(unittest.TestCase):
    CONTRACT = "docs/architecture/SYNCHRONIZATION_CONTRACT.md"

    @classmethod
    def setUpClass(cls) -> None:
        cls.document = read(cls.CONTRACT)
        cls.lower = cls.document.lower()

    def test_contract_covers_every_required_execution_context(self) -> None:
        for concept in (
            "irq",
            "sleep",
            "irq_save",
            "scheduler_can_sleep",
            "kassert_irq_disabled",
            "kassert_not_irq",
            "kassert_can_sleep",
        ):
            with self.subTest(concept=concept):
                self.assertIn(concept, self.lower)
        self.assertRegex(self.lower, r"(?:pr[äa]emption|preemption)")

    def test_contract_names_all_lock_domains_and_a_total_order(self) -> None:
        domains = {
            "scheduler": ("scheduler",),
            "vfs": ("vfs",),
            "filesystem": ("dateisystem", "filesystem"),
            "driver": ("treiber", "driver"),
        }
        for domain, alternatives in domains.items():
            with self.subTest(domain=domain):
                self.assertTrue(any(name in self.lower for name in alternatives))

        # A total order must be machine-readable enough to review mechanically:
        # either one arrow chain or four uniquely numbered rank/table rows.
        arrow_line = re.search(
            r"(?im)^.*vfs.*(?:->|→).*(?:dateisystem|filesystem).*"
            r"(?:->|→).*(?:treiber|driver).*(?:->|→).*scheduler.*$",
            self.document,
        )
        ordered_rows = []
        for domain in ("vfs", "(?:dateisystem|filesystem)",
                       "(?:treiber|driver)", "scheduler"):
            row = re.search(
                rf"(?im)^\s*\|?\s*(\d+)\s*\|[^\n]*\b{domain}\b[^\n]*$",
                self.document,
            )
            if row is not None:
                ordered_rows.append(int(row.group(1)))
        self.assertTrue(
            arrow_line is not None or ordered_rows == sorted(set(ordered_rows))
            and len(ordered_rows) == 4,
            "lock contract needs an unambiguous VFS -> filesystem -> "
            "driver -> scheduler total order",
        )

    def test_preemption_guard_is_explicitly_outside_lock_ranking(self) -> None:
        self.assertRegex(
            self.lower,
            r"(?:pr[äa]emption|preemption)[^\n]{0,120}(?:kein|nicht|not)"
            r"[^\n]{0,60}lock[- ]?rank",
        )
        for forbidden_boundary in ("sleep", "yield", "block", "exit", "swtch"):
            with self.subTest(boundary=forbidden_boundary):
                self.assertRegex(
                    self.lower,
                    rf"(?:preempt_guard|pr[äa]emption|preemption)[^\n]{{0,180}}"
                    rf"(?:nicht|nie|never|must not)[^\n]{{0,80}}{forbidden_boundary}",
                )

    def test_allocator_suborder_is_heap_before_frame(self) -> None:
        self.assertRegex(
            self.document,
            r"(?im)^.*HEAP.*(?:->|→).*FRAME.*$",
        )

    def test_contract_forbids_sleep_and_recursive_spinlock_acquisition(self) -> None:
        self.assertRegex(self.lower, r"(?:spinlock|lock)[^\n]{0,100}nicht\s+(?:schlafen|blockieren)")
        self.assertRegex(self.lower, r"(?:rekursiv|recursive)[^\n]{0,100}(?:verboten|nicht erlaubt|unzulässig)")


class StructuredKernelLoggingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        candidates = (
            "include/kernel/kernel_log.h",
            "include/kernel/log.h",
            "kernel/init/kernel_log.h",
        )
        for candidate in candidates:
            path = ROOT / candidate
            if path.exists():
                cls.header_path = candidate
                cls.header = path.read_text(encoding="utf-8")
                break
        else:
            raise AssertionError("missing public structured kernel log header")

        implementations = (
            "kernel/init/kernel_log.c",
            "kernel/init/log.c",
            "lib/libk/kernel_log.c",
        )
        for candidate in implementations:
            path = ROOT / candidate
            if path.exists():
                cls.source_path = candidate
                cls.source = path.read_text(encoding="utf-8")
                break
        else:
            raise AssertionError("missing structured kernel log implementation")

    def test_levels_are_ordered_and_have_stable_names(self) -> None:
        enum_match = re.search(
            r"typedef\s+enum\s*(?:\w+\s*)?\{(?P<body>.*?)\}\s*\w+\s*;",
            self.header,
            flags=re.DOTALL,
        )
        self.assertIsNotNone(enum_match, "log levels must be a typed enum")
        enum_body = enum_match.group("body")
        names = re.findall(r"\b(KLOG_(?:TRACE|DEBUG|INFO|WARN|ERROR))\b", enum_body)
        self.assertEqual(
            names,
            ["KLOG_TRACE", "KLOG_DEBUG", "KLOG_INFO", "KLOG_WARN", "KLOG_ERROR"],
        )

    def test_logger_emits_level_and_component_prefixes(self) -> None:
        logger_match = re.search(
            r"\bklog\s*\([^;{}]*\)\s*\{", self.source
        )
        self.assertIsNotNone(logger_match, "missing central kernel logger")
        logger = extract_block(self.source, self.source.index("{", logger_match.start()))
        for label in ("TRACE", "DEBUG", "INFO", "WARN", "ERROR"):
            with self.subTest(label=label):
                self.assertIn(f'"{label}"', self.source)
        self.assertRegex(logger, r"\bcomponent\b")
        self.assertRegex(logger, r"printf\s*\(")

    def test_public_logger_carries_level_component_and_format_contract(self) -> None:
        declaration = compact(self.header)
        self.assertRegex(
            declaration,
            r"void klog\s*\(\s*kernel_log_level_t\s+level\s*,\s*"
            r"const char\s*\*\s*component\s*,\s*const char\s*\*\s*format\s*,\s*"
            r"\.\.\.\s*\)\s*[^;]*;",
        )
        self.assertRegex(
            declaration,
            r"__attribute__\s*\(\(\s*format\s*\(\s*printf\s*,\s*3\s*,\s*4\s*\)",
        )


class PanicCrashContextContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.panic_h = read("include/kernel/panic.h")
        cls.panic_c = read("kernel/init/panic.c")
        cls.isr_c = read("arch/x86/cpu/isr.c")
        cls.makefile = read("Makefile")
        cls.linker = read("config/klink.ld")

    def test_public_panic_api_accepts_a_full_exception_frame(self) -> None:
        header = compact(self.panic_h)
        declarations = (
            r"const char\s*\*\s*kernel_build_id\s*\(\s*void\s*\)\s*;",
            r"void panic_dump_exception_context\s*\(\s*const Registers\s*\*\s*"
            r"registers\s*,\s*uint32_t\s+cr2\s*\)\s*;",
            r"void\s+(?:__attribute__\s*\(\(\s*noreturn\s*\)\)\s*)?"
            r"panic_with_exception\s*\(\s*const char\s*\*\s*message\s*,\s*"
            r"const Registers\s*\*\s*registers\s*,\s*uint32_t\s+cr2\s*\)\s*;",
        )
        for pattern in declarations:
            with self.subTest(pattern=pattern):
                self.assertRegex(header, pattern)
        self.assertRegex(
            self.panic_h,
            r"__attribute__\s*\(\(\s*noreturn\s*\)\)\s*panic_with_exception",
        )

    def test_panic_dump_prints_cr2_and_every_register_group(self) -> None:
        dump = function_block(
            self.panic_c, "void panic_dump_exception_context("
        )
        self.assertIn("CR2", dump)
        self.assertRegex(dump, r"0x%08X")
        self.assertIn("Register frame: unavailable", dump)
        for field in (
            "eax", "ebx", "ecx", "edx",
            "esi", "edi", "ebp", "esp",
            "eip", "eflags",
            "cs", "ss", "ds", "es", "fs", "gs",
            "irq_number", "error_code",
        ):
            with self.subTest(register=field):
                self.assertRegex(dump, rf"registers\s*->\s*{field}\b")

    def test_every_panic_path_prints_a_build_id_and_cr2_status(self) -> None:
        dump = function_block(
            self.panic_c, "void panic_dump_exception_context("
        )
        self.assertIn("kernel_build_id()", dump)
        self.assertIn("CR2", dump)
        for signature in (
            "void __attribute__((noreturn)) panic(",
            "void __attribute__((noreturn)) kassert_fail(",
            "void __attribute__((noreturn)) panic_with_exception(",
        ):
            with self.subTest(function=signature):
                block = function_block(self.panic_c, signature)
                self.assertIn("panic_dump_exception_context(", block)
        self.assertIn("panic_dump_exception_context(NULL, read_cr2())", compact(self.panic_c))

    def test_kernel_exceptions_forward_frame_and_cr2_to_panic(self) -> None:
        for signature in (
            "void generic_exception_handler(",
            "void divide_by_zero_handler(",
            "void page_fault_handler(",
        ):
            with self.subTest(function=signature):
                block = function_block(self.isr_c, signature)
                kernel_path = block[: block.index("} else {")]
                self.assertIn("panic_with_exception(", kernel_path)
                self.assertRegex(kernel_path, r"panic_with_exception\s*\([^;]*r\s*,")
        self.assertRegex(self.isr_c, r"mov\s+%%cr2")

    def test_build_id_is_sha1_and_preserved_by_the_linker_script(self) -> None:
        self.assertRegex(self.makefile, r"(?m)^LDFLAGS\s*:=.*--build-id=sha1")
        self.assertIn(".note.gnu.build-id", self.linker)
        self.assertRegex(self.linker, r"KEEP\s*\(\s*\*\(\.note\.gnu\.build-id\)\s*\)")
        for symbol in (
            "_kernel_build_id_note_start",
            "_kernel_build_id_note_end",
        ):
            with self.subTest(symbol=symbol):
                self.assertIn(symbol, self.linker)

        build_id = function_block(self.panic_c, "const char* kernel_build_id(")
        self.assertIn('"0123456789ABCDEF"', build_id)
        self.assertRegex(self.panic_c, r"#define\s+GNU_BUILD_ID_SHA1_SIZE\s+20U")
        self.assertIn("GNU_BUILD_ID_SHA1_SIZE * 2U", build_id)

    def test_panic_context_is_fixed_redundant_and_checksum_validated(self) -> None:
        header = compact(self.panic_h)
        self.assertIn("panic_context_set(", header)
        self.assertIn("panic_context_set_result(", header)
        self.assertIn("panic_context_slots[2]", self.panic_c)
        self.assertIn("context_checksum(", self.panic_c)
        self.assertIn("context_valid(", self.panic_c)
        self.assertIn("panic_context_snapshot(", self.panic_c)
        for forbidden in ("malloc(", "k_malloc(", "vfs_"):
            context = self.panic_c[
                self.panic_c.index("void panic_context_set("):
                self.panic_c.index("const char* kernel_build_id(")
            ]
            self.assertNotIn(forbidden, context)

    def test_all_panic_screens_print_failure_context(self) -> None:
        self.assertIn('panic_label("FAILURE CONTEXT")', self.panic_c)
        for field in ("Phase", "Component", "Operation", "Subject",
                      "Result", "Details", "Sequence"):
            self.assertIn(field, self.panic_c)
        for signature in (
            "void __attribute__((noreturn)) panic(",
            "void __attribute__((noreturn)) kassert_fail(",
            "void __attribute__((noreturn)) panic_with_exception(",
        ):
            with self.subTest(function=signature):
                block = function_block(self.panic_c, signature)
                self.assertIn("panic_dump_failure_context(", block)

    def test_boot_pci_and_program_loader_publish_breadcrumbs(self) -> None:
        kernel = read("kernel/init/kernel.c")
        process = read("kernel/proc/process.c")
        pci = read("drivers/bus/pci.c")
        for component in ('"AHCI"', '"ATA"', '"VFS"', '"REIST probe"'):
            self.assertIn(component, kernel)
        self.assertIn('"/libexec/reist/reist.prg"', kernel)
        self.assertIn('"program-load"', process)
        self.assertIn("program_name", process)
        self.assertIn("pci_register_driver_named", pci)
        self.assertIn("panic_context_set_result(result, identity, location)",
                      compact(pci))


if __name__ == "__main__":
    unittest.main()
