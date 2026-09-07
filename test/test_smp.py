"""Host and source contracts for bounded x86 SMP bring-up."""
import shutil
import subprocess
import tempfile
import unittest
import importlib.util
import re
import os
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class SmpTests(unittest.TestCase):
    def test_pat_is_initialized_on_each_cpu_before_online(self):
        paging = (ROOT / "arch/x86/mm/paging.c").read_text(encoding="utf-8")
        smp = (ROOT / "arch/x86/cpu/smp.c").read_text(encoding="utf-8")
        self.assertIn("pat_checked[X86_CPU_LOCAL_MAX]", paging)
        self.assertIn("pat_write_combining[X86_CPU_LOCAL_MAX]", paging)
        self.assertIn("KASSERT(paging_prepare_cpu_memory_types())", paging)
        self.assertIn("write_combining == pat_write_combining[0]", paging)
        entry = smp[smp.index("void x86_smp_ap_entry"):smp.index("bool x86_smp_initialize")]
        self.assertLess(entry.index("paging_prepare_cpu_memory_types()"),
                        entry.index("x86_cpu_local_mark_online(cpu_index)"))
        mapping = paging[paging.index("void *map_kernel_write_combining"):
                         paging.index("int map_page(")]
        self.assertIn("!pat_write_combining[cpu]", mapping)
        self.assertNotIn("prepare_pat_write_combining()", mapping)

    def test_cpu_local_gdtr_binding_behavior(self):
        smp = (ROOT / "arch/x86/cpu/smp.c").read_text(encoding="utf-8")
        self.assertIn("gdtr.base = (uint32_t)(uintptr_t)bootstrap_gdt", smp)
        self.assertIn("gdtr.limit = sizeof(bootstrap_gdt) - 1U", smp)
        compiler = shutil.which("gcc") or shutil.which("clang")
        command = [compiler] if compiler else [shutil.which("zig"), "cc"]
        self.assertIsNotNone(command[0], "C compiler required for CPU identity proof")
        environment = os.environ.copy()
        environment["ZIG_GLOBAL_CACHE_DIR"] = str(ROOT / "build/zig-global-cache")
        environment["ZIG_LOCAL_CACHE_DIR"] = str(ROOT / "build/zig-cache")
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "cpu-local-test.exe"
            compiled = subprocess.run(command + [
                "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-DREIST_HOST_TEST", "-I", str(ROOT),
                str(ROOT / "arch/x86/cpu/cpu_local.c"),
                str(ROOT / "test/test_cpu_local_host.c"),
                "-o", str(executable)], env=environment,
                capture_output=True, text=True, timeout=60)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            result = subprocess.run([str(executable)], check=True,
                                    capture_output=True, text=True, timeout=10)
            self.assertIn("runtime_cpuid=0", result.stdout)

    def test_cross_cpu_wait_queue_wake_requests_bounded_reschedule(self):
        header = (ROOT / "arch/x86/include/smp.h").read_text(
            encoding="utf-8")
        smp = (ROOT / "arch/x86/cpu/smp.c").read_text(encoding="utf-8")
        irq_asm = (ROOT / "arch/x86/cpu/irq.asm").read_text(
            encoding="utf-8")
        irq = (ROOT / "arch/x86/cpu/irq.c").read_text(encoding="utf-8")
        scheduler = (ROOT / "kernel/sched/scheduler.c").read_text(
            encoding="utf-8")

        self.assertIn("X86_SMP_RESCHEDULE_VECTOR 0xF3U", header)
        self.assertIn("x86_smp_request_reschedule", header)
        self.assertIn("x86_smp_reschedule_isr", header)
        self.assertIn("smp_reschedule_interrupt", irq_asm)
        self.assertIn("X86_SMP_RESCHEDULE_VECTOR", irq)

        request = smp[smp.index("bool x86_smp_request_reschedule"):
                      smp.index("void x86_smp_reschedule_isr")]
        self.assertIn("SMP_RESCHEDULE_IPI_SPIN_LIMIT", request)
        self.assertIn("smp_scheduler_ack_mask", request)
        self.assertIn("x86_cpu_current_index()", request)
        self.assertIn("apic_send_ipi_bounded", request)

        isr = smp[smp.index("void x86_smp_reschedule_isr"):
                  smp.index("static void smp_lock_probe_publish")]
        self.assertLess(isr.index("apic_eoi();"),
                        isr.index("irq_context_exit();"))
        self.assertLess(isr.index("irq_context_exit();"),
                        isr.index("scheduler_interrupt_handler();"))

        wake = scheduler[
            scheduler.index("bool wait_queue_wake_one_locked"):
            scheduler.index("static size_t wait_queue_wake_all_task_locked")]
        self.assertIn("task->cpu_affinity_mask", scheduler)
        self.assertLess(wake.index("spinlock_release(&task_table_lock);"),
                        wake.index("x86_smp_request_reschedule"))

    def test_madt_parser(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "acpi-madt-test.exe"
            subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-DREIST_HOST_TEST", "-I", str(ROOT),
                 str(ROOT / "arch/x86/platform/acpi.c"),
                 str(ROOT / "test/test_acpi_madt_host.c"),
                 "-o", str(executable)],
                check=True, capture_output=True, text=True, timeout=30)
            result = subprocess.run([str(executable)], timeout=10)
            self.assertEqual(result.returncode, 0)

    def test_smp_runtime_is_bounded_and_releases_isolated_ap_tasks(self):
        source = (ROOT / "arch/x86/cpu/smp.c").read_text(encoding="utf-8")
        header = (ROOT / "arch/x86/include/smp.h").read_text(encoding="utf-8")
        trampoline = (ROOT / "arch/x86/boot/ap_trampoline.asm").read_text(
            encoding="utf-8")
        self.assertIn("X86_SMP_MAX_CPUS 16U", header)
        self.assertIn("SMP_AP_START_TIMEOUT_MS", source)
        self.assertIn("pit_monotonic_ms", source)
        self.assertIn("cpu_halt();", source)
        self.assertIn("x86_smp_ap_entry", source)
        self.assertIn("SMP_AP_STATE_ONLINE", source)
        self.assertIn("smp_lock_probe_publish(cpu_index)", source)
        self.assertIn("REIST_SMP LOCK_READY", source)
        self.assertIn("REIST_SMP TLB_READY", source)
        self.assertIn("REIST_SMP IRQ_AFFINITY_READY", source)
        self.assertIn("REIST_SMP TIMER_READY", source)
        self.assertIn("SMP_SCHEDULER_TIMEOUT_MS", source)
        self.assertIn("REIST_SMP SCHEDULER_READY", source)
        self.assertIn("REIST_SMP MUTEX_READY", source)
        self.assertIn("REIST_SMP INTEGRITY_READY", source)
        self.assertIn("REIST_SMP SUBSYSTEM_READY", source)
        self.assertIn("REIST_SMP REAP_READY", source)
        self.assertIn("x86_smp_set_parallel_probe", header)
        self.assertIn("create_affined_kernel_task", source)
        self.assertIn("cli", trampoline)
        self.assertIn("hlt", trampoline)

    def test_qemu_smoke_exposes_bounded_smp_profile(self):
        path = ROOT / "scripts/run_qemu_smoke.py"
        spec = importlib.util.spec_from_file_location("run_qemu_smoke", path)
        module = importlib.util.module_from_spec(spec)
        assert spec.loader is not None
        spec.loader.exec_module(module)
        command = module.qemu_command(
            Path("qemu-system-i386"), Path("reist-os.img"), smp=4)
        self.assertIn("-smp", command)
        self.assertEqual(command[command.index("-smp") + 1], "4")
        smoke = path.read_text(encoding="utf-8")
        self.assertIn("REIST_SMP LOCK_READY", smoke)
        self.assertIn("REIST_SMP TLB_READY", smoke)
        self.assertIn("REIST_SMP IRQ_AFFINITY_READY", smoke)
        self.assertIn("REIST_SMP TIMER_READY", smoke)
        self.assertIn("REIST_SMP SCHEDULER_READY", smoke)
        self.assertIn("REIST_SMP MUTEX_READY", smoke)
        self.assertIn("REIST_SMP INTEGRITY_READY", smoke)
        self.assertIn("REIST_SMP SUBSYSTEM_READY", smoke)
        self.assertIn("REIST_SMP REAP_READY", smoke)

    def test_kernel_reserves_trampoline_before_memory_initialization(self):
        source = (ROOT / "kernel/init/kernel.c").read_text(encoding="utf-8")
        reserve = source.index("X86_SMP_TRAMPOLINE_BASE")
        memory_init = source.index("initialize_memory_system()")
        self.assertLess(reserve, memory_init)
        self.assertIn("x86_acpi_capture_early();", source)

    def test_per_cpu_state_separates_preemption_irq_and_address_spaces(self):
        header = (ROOT / "arch/x86/include/cpu_local.h").read_text(
            encoding="utf-8")
        irq = (ROOT / "arch/x86/cpu/irq.c").read_text(encoding="utf-8")
        paging = (ROOT / "arch/x86/mm/paging.c").read_text(encoding="utf-8")
        scheduler = (ROOT / "kernel/sched/scheduler.c").read_text(
            encoding="utf-8")
        for field in ("irq_context_depth", "preempt_disable_count",
                      "preemption_pending", "scheduler_current_task",
                      "irq_context_vector",
                      "scheduler_context_saved", "scheduler_handoff_task",
                      "scheduler_handoff_action", "scheduler_context",
                      "current_page_directory", "lapic_timer_ticks",
                      "lapic_timer_calibrated", "kernel_idle_stack_low",
                      "kernel_idle_stack_high"):
            self.assertIn(field, header)
        self.assertIn("current_irq_context_depth", irq)
        self.assertIn("irq_context_note_vector(regs->irq_number)", irq)
        self.assertIn("local->current_page_directory", paging)
        self.assertIn("x86_cpu_local_current()->preempt_disable_count",
                      scheduler)
        self.assertIn("scheduler_cpu_local()->scheduler_current_task",
                      scheduler)
        self.assertIn("scheduler_cpu_local()->scheduler_context", scheduler)
        self.assertNotIn("KASSERT(local->cpu_index == 0U)", scheduler)
        self.assertIn("local->kernel_idle_stack_low", scheduler)

    def test_each_ap_loads_private_gdt_and_tss_before_online(self):
        smp = (ROOT / "arch/x86/cpu/smp.c").read_text(encoding="utf-8")
        gdt = (ROOT / "arch/x86/cpu/gdt.c").read_text(encoding="utf-8")
        tss = (ROOT / "arch/x86/cpu/tss.c").read_text(encoding="utf-8")
        self.assertIn("tss_init_cpu(cpu_index", smp)
        self.assertIn("gdt_install_cpu(cpu_index)", smp)
        self.assertIn("ap_gdt[X86_CPU_LOCAL_MAX - 1U][7]", gdt)
        self.assertIn("ap_kernel_tss[X86_CPU_LOCAL_MAX - 1U]", tss)
        self.assertIn("REIST_SMP PERCPU_READY", smp)

    def test_spinlock_is_cpu_owned_and_bounded(self):
        lock = (ROOT / "include/lib/spinlock.h").read_text(encoding="utf-8")
        self.assertIn("owner_cpu", lock)
        self.assertIn("SPINLOCK_ACQUIRE_SPIN_LIMIT", lock)
        self.assertIn("SPINLOCK_ACQUIRE_TIMED_SPIN_LIMIT", lock)
        self.assertIn("SPINLOCK_ACQUIRE_TIMEOUT_MS", lock)
        self.assertIn("spinlock_acquire_bounded", lock)
        self.assertIn("x86_cpu_current_index()", lock)
        self.assertIn("cpu_cycle_counter_read()", lock)
        self.assertIn("cpu_frequency", lock)
        self.assertNotIn("while (__sync_lock_test_and_set", lock)

    def test_static_spinlocks_publish_no_owner_initially(self):
        declaration = re.compile(
            r"static\s+spinlock_t\s+[A-Za-z_][A-Za-z0-9_]*\s*;"
        )
        offenders = []
        for path in ROOT.rglob("*.c"):
            if any(part in {"build", ".git"} for part in path.parts):
                continue
            if declaration.search(path.read_text(encoding="utf-8")):
                offenders.append(str(path.relative_to(ROOT)))
        self.assertEqual(offenders, [])

    def test_page_table_mutations_use_the_smp_lock(self):
        paging = (ROOT / "arch/x86/mm/paging.c").read_text(encoding="utf-8")
        self.assertIn("page_table_lock = SPINLOCK_INIT", paging)
        for signature in ("page_directory_t *create_page_directory(",
                          "bool free_page_directory_step(",
                          "int unmap_page(", "int map_page("):
            start = paging.index(signature)
            body = paging[start:paging.index("\n}", start) + 2]
            self.assertIn("page_table_lock_acquire_irq()", body)
            self.assertIn("page_table_lock_release_irq(", body)

        # Directory teardown releases the lock between bounded steps. Check
        # the actual mutator and its delegate, not a long outer IRQ exclusion.
        start = paging.index("void free_page_directory(")
        wrapper = paging[start:paging.index("\n}", start) + 2]
        self.assertIn("free_page_directory_step(pd, &cursor)", wrapper)
        self.assertIn("PAGE_TABLE_ENTRIES / 64U", wrapper)
        self.assertNotIn("page_table_lock_acquire_irq()", wrapper)
        self.assertNotIn("free_page(", wrapper)
        start = paging.index("bool free_page_directory_step(")
        step = paging[start:paging.index("\n}", start) + 2]
        self.assertIn("work < 64U", step)
        acquired = step.index("page_table_lock_acquire_irq()")
        released = step.index("page_table_lock_release_irq(flags)")
        for mutation in ("table[j] = 0U;", "entries[i] = 0U;",
                         "free_page(table)", "free_page(pd)",
                         "free_page((void*)(uintptr_t)(entry & 0xFFFFF000U))"):
            with self.subTest(mutation=mutation):
                self.assertLess(acquired, step.index(mutation))
                self.assertLess(step.index(mutation), released)
        self.assertLess(released, step.index("return complete;"))

    def test_tlb_shootdown_is_generation_scoped_and_bounded(self):
        paging = (ROOT / "arch/x86/mm/paging.c").read_text(encoding="utf-8")
        irq_asm = (ROOT / "arch/x86/cpu/irq.asm").read_text(
            encoding="utf-8")
        smp = (ROOT / "arch/x86/cpu/smp.c").read_text(encoding="utf-8")
        self.assertIn("tlb_shootdown_request_t", paging)
        self.assertIn("TLB_SHOOTDOWN_ACK_SPIN_LIMIT", paging)
        self.assertIn("tlb_observed_generation", paging)
        self.assertIn("apic_send_ipi_bounded", paging)
        self.assertIn("paging_tlb_shootdown_isr", paging)
        self.assertIn("__sync_fetch_and_or", paging)
        self.assertIn("tlb_shootdown_interrupt", irq_asm)
        self.assertIn("irq_enable();\n    while (1) {", smp)
        for signature in ("int unmap_kernel_page(", "int unmap_page(",
                          "int map_page("):
            start = paging.index(signature)
            body = paging[start:paging.index("\n}", start) + 2]
            self.assertIn("tlb_shootdown_or_panic", body)

    def test_page_table_wait_remains_tlb_ipi_responsive(self):
        paging = (ROOT / "arch/x86/mm/paging.c").read_text(
            encoding="utf-8")
        acquire_start = paging.index(
            "static uint32_t page_table_lock_acquire_irq(")
        acquire_end = paging.index("\n}", acquire_start)
        acquire = paging[acquire_start:acquire_end]
        self.assertIn('"sti\\n\\tpause\\n\\tcli"', acquire)
        self.assertIn("SPINLOCK_ACQUIRE_SPIN_LIMIT", acquire)
        self.assertIn("spinlock_trylock(&page_table_lock)", acquire)
        self.assertNotIn("spinlock_acquire_irq(&page_table_lock)", paging)

    def test_timer_scheduler_does_not_take_the_page_table_lock(self):
        scheduler = (ROOT / "kernel/sched/scheduler.c").read_text(
            encoding="utf-8")
        start = scheduler.index("void scheduler_interrupt_handler(void)")
        end = scheduler.index("void scheduler_preempt_disable", start)
        interrupt_path = scheduler[start:end]
        self.assertIn("validate_running_task_stack_irq_or_panic", interrupt_path)
        self.assertIn("validate_kernel_context_stack_irq_or_panic", interrupt_path)
        self.assertNotIn("scheduler_kernel_stack_is_valid", interrupt_path)
        self.assertNotIn("paging_kernel_page_present", interrupt_path)
        runtime_start = scheduler.index("static void validate_task_stack_or_panic")
        runtime_end = scheduler.index("static void refresh_effective_classes_locked",
                                      runtime_start)
        runtime_validation = scheduler[runtime_start:runtime_end]
        self.assertNotIn("scheduler_kernel_stack_is_valid(", runtime_validation)
        self.assertNotIn("paging_kernel_page_present", runtime_validation)

    def test_legacy_irqs_are_explicitly_bsp_affine(self):
        irq = (ROOT / "arch/x86/cpu/irq.c").read_text(encoding="utf-8")
        header = (ROOT / "arch/x86/include/sys.h").read_text(
            encoding="utf-8")
        self.assertIn("IRQ_BSP_AFFINITY_MASK", irq)
        self.assertIn("irq_set_affinity", irq)
        self.assertIn("irq_affinity_bsp_only_ready", irq)
        self.assertIn("x86_cpu_current_index()", irq)
        self.assertIn("irq_pic_mask_line((uint8_t)irq)", irq)
        self.assertIn("irq_set_affinity", header)

    def test_each_cpu_calibrates_a_masked_lapic_timer(self):
        apic = (ROOT / "kernel/time/apic.c").read_text(encoding="utf-8")
        smp = (ROOT / "arch/x86/cpu/smp.c").read_text(encoding="utf-8")
        self.assertIn("apic_calibrate_current_cpu_timer_masked", apic)
        self.assertIn("lapic_timer_calibrated", apic)
        self.assertIn("TIMER_MASKED | APIC_VECTOR_BASE", apic)
        self.assertIn("apic_calibrate_current_cpu_timer_masked(&timer_ticks)",
                      smp)
        self.assertIn("calibrated_cpus != smp_status.online_cpu_count", smp)
        pit = (ROOT / "kernel/time/pit.c").read_text(encoding="utf-8")
        self.assertIn("timer_tick_sequence", pit)
        self.assertIn("PIT_MONOTONIC_READ_RETRY_LIMIT", pit)
        self.assertIn("PIT_MONOTONIC_READ_TIMEOUT_MS", pit)
        self.assertIn("cpu_cycle_counter_read()", pit)

    def test_scheduler_samples_monotonic_time_before_task_lock(self):
        scheduler = (ROOT / "kernel/sched/scheduler.c").read_text(
            encoding="utf-8")
        account_start = scheduler.index(
            "static void account_current_runtime_locked(")
        account_end = scheduler.index("static int find_next_runnable(",
                                      account_start)
        self.assertNotIn("pit_monotonic_ms()",
                         scheduler[account_start:account_end])
        for signature, end_signature in (
            ("int scheduler_yield(void)",
             "void scheduler_set_apic_timer_active"),
            ("void scheduler_interrupt_handler(void)",
             "void scheduler_preempt_disable"),
            ("void task_exit_status(int status)",
             "void scheduler_kill_current"),
        ):
            start = scheduler.index(signature)
            end = scheduler.index(end_signature, start)
            body = scheduler[start:end]
            lock_positions = [body.index(marker) for marker in (
                "spinlock_acquire(&task_table_lock)",
                "spinlock_trylock(&task_table_lock)") if marker in body]
            self.assertTrue(lock_positions)
            self.assertLess(body.index("pit_monotonic_ms()"),
                            min(lock_positions))

    def test_task_table_admin_transactions_are_locked_but_switches_are_not(self):
        scheduler = (ROOT / "kernel/sched/scheduler.c").read_text(
            encoding="utf-8")
        process = (ROOT / "kernel/proc/process.c").read_text(encoding="utf-8")
        header = (ROOT / "kernel/sched/scheduler.h").read_text(
            encoding="utf-8")
        self.assertIn("task_table_lock = SPINLOCK_INIT", scheduler)
        self.assertIn("task_table_lock_irqsave()", scheduler)
        self.assertIn("scheduler_task_state_snapshot", scheduler)
        self.assertIn("scheduler_task_state_snapshot", process)
        self.assertNotIn("tasks[", process)
        self.assertNotIn("extern task_t tasks[]", header)
        interrupt_start = scheduler.index("void scheduler_interrupt_handler(")
        interrupt_end = scheduler.index("void scheduler_preempt_disable(",
                                        interrupt_start)
        self.assertNotIn("task_table_lock_irqsave", scheduler[
            interrupt_start:interrupt_end])
        self.assertIn("spinlock_trylock(&task_table_lock)", scheduler[
            interrupt_start:interrupt_end])
        self.assertIn("spinlock_release(&task_table_lock)", scheduler[
            interrupt_start:interrupt_end])
        exit_start = scheduler.index("void task_exit_status(")
        exit_end = scheduler.index("void scheduler_kill_current(", exit_start)
        exit_body = scheduler[exit_start:exit_end]
        self.assertLess(exit_body.index("spinlock_release(&task_table_lock)"),
                        exit_body.index("swtch(NULL"))

    def test_spinlock_recursion_uses_atomic_owner_token(self):
        lock = (ROOT / "include/lib/spinlock.h").read_text(encoding="utf-8")
        bounded = lock[lock.index("spinlock_acquire_bounded("):
                       lock.index("static inline void spinlock_acquire(")]
        self.assertIn("uint32_t owner_token = cpu + 1U", bounded)
        self.assertIn("__sync_val_compare_and_swap(", bounded)
        self.assertLess(bounded.index("uint32_t observed = lock->lock"),
                        bounded.index("__sync_val_compare_and_swap("))
        self.assertLess(bounded.index("if (observed == 0U)"),
                        bounded.index("if (observed == owner_token)"))
        self.assertNotIn("if (lock->owner_cpu == cpu)", bounded)

    def test_spinlock_timeout_identifies_lock_owner_and_waiter(self):
        lock = (ROOT / "include/lib/spinlock.h").read_text(encoding="utf-8")
        acquire = lock[lock.index("static inline void spinlock_acquire("):
                       lock.index("static inline void spinlock_release(")]
        self.assertIn("__builtin_return_address(0)", acquire)
        self.assertIn("(uint32_t)(uintptr_t)lock", acquire)
        self.assertIn("lock->owner_cpu << 24U", acquire)
        self.assertIn("caller & 0x00FFFFFFU", acquire)

    def test_ap_scheduler_release_is_affined_bounded_and_guarded(self):
        smp = (ROOT / "arch/x86/cpu/smp.c").read_text(encoding="utf-8")
        scheduler = (ROOT / "kernel/sched/scheduler.c").read_text(
            encoding="utf-8")
        header = (ROOT / "kernel/sched/scheduler.h").read_text(
            encoding="utf-8")
        irq_asm = (ROOT / "arch/x86/cpu/irq.asm").read_text(
            encoding="utf-8")
        self.assertIn("cpu_affinity_mask", header)
        self.assertIn("create_affined_kernel_task", header)
        self.assertIn("tasks[index].cpu_affinity_mask & cpu_bit", scheduler)
        self.assertIn("cpu_windows[X86_CPU_LOCAL_MAX]", scheduler)
        self.assertIn("scheduling_class_cursors[X86_CPU_LOCAL_MAX]", scheduler)
        self.assertIn("scheduler_allocate_kernel_stack()", smp)
        self.assertIn("X86_SMP_SCHEDULER_RELEASE_VECTOR", smp)
        self.assertIn("smp_scheduler_release_interrupt", irq_asm)
        self.assertIn("smp_scheduler_settled_mask", smp)
        self.assertIn("SMP_SCHEDULER_TIMEOUT_MS", smp)
        self.assertIn("scheduler_reap_finished_tasks()", smp)
        self.assertIn("REIST_SMP REAP_READY", smp)

    def test_per_cpu_idle_stacks_do_not_consume_task_stack_capacity(self):
        scheduler = (ROOT / "kernel/sched/scheduler.c").read_text(
            encoding="utf-8")
        paging = (ROOT / "arch/x86/mm/paging.h").read_text(
            encoding="utf-8")
        self.assertIn("KERNEL_STACK_SLOT_COUNT 48U", paging)
        self.assertIn("MAX_TASKS + X86_CPU_LOCAL_MAX - 1U", scheduler)
        self.assertIn(
            "kernel_stack_slots[KERNEL_STACK_SLOT_COUNT]", scheduler)
        self.assertIn("slot < KERNEL_STACK_SLOT_COUNT", scheduler)

    def test_runqueue_has_atomic_cpu_ownership_and_deferred_handoff(self):
        scheduler = (ROOT / "kernel/sched/scheduler.c").read_text(
            encoding="utf-8")
        header = (ROOT / "kernel/sched/scheduler.h").read_text(
            encoding="utf-8")
        local = (ROOT / "arch/x86/include/cpu_local.h").read_text(
            encoding="utf-8")
        self.assertIn("volatile int32_t running_cpu", header)
        self.assertIn("TASK_CPU_NONE", header)
        self.assertIn("TASK_HANDOFF", header)
        self.assertIn("scheduler_handoff_task", local)
        self.assertIn("scheduler_handoff_action", local)
        claim_start = scheduler.index("static bool claim_task_for_current_cpu")
        claim_end = scheduler.index("static int claim_next_runnable",
                                    claim_start)
        claim = scheduler[claim_start:claim_end]
        self.assertIn("__sync_bool_compare_and_swap(&task->running_cpu",
                      claim)
        select_start = scheduler.index("static int find_next_runnable")
        select_end = scheduler.index("static uint32_t active_task_count_locked",
                                     select_start)
        select = scheduler[select_start:select_end]
        self.assertIn("ready_unowned", select)
        self.assertIn("running_here", select)
        self.assertIn("MAX_TASKS", select)
        handoff_start = scheduler.index("static void prepare_task_handoff")
        handoff_end = scheduler.index("static uint32_t active_task_count_locked",
                                      handoff_start)
        handoff = scheduler[handoff_start:handoff_end]
        self.assertIn("TASK_HANDOFF", handoff)
        self.assertIn("finish_task_handoff", handoff)
        self.assertIn("TASK_CPU_NONE", handoff)
        self.assertIn("finish_task_handoff();\n    int index = current_task",
                      scheduler)
        for call in (
                "swtch(&tasks[blocked].context, &tasks[next].context);",
                "swtch(&tasks[blocked].context, &kernel_context);",
                "swtch(&tasks[previous].context, &kernel_context);",
                "swtch(&kernel_context, &tasks[next].context);",
                "swtch(&tasks[previous].context, &tasks[next].context);"):
            position = scheduler.index(call)
            self.assertIn("finish_task_handoff();",
                          scheduler[position:position + len(call) + 80])

    def test_keyboard_wait_atomically_transfers_from_input_lock(self):
        keyboard = (ROOT / "drivers/char/kb.c").read_text(encoding="utf-8")
        getchar_start = keyboard.index("char getchar(void)")
        getchar_end = keyboard.index("char getchar_nonblocking(void)",
                                     getchar_start)
        getchar = keyboard[getchar_start:getchar_end]
        self.assertIn("spinlock_acquire(&input_queue_lock)", getchar)
        self.assertIn("wait_queue_block_until_spinlocked(", getchar)
        self.assertIn("&input_queue_lock, flags", getchar)
        self.assertNotIn("wait_queue_block_until_locked(", getchar)

    def test_sleepable_kernel_mutex_has_bounded_atomic_wait_transfer(self):
        header = (ROOT / "kernel/sched/mutex.h").read_text(encoding="utf-8")
        source = (ROOT / "kernel/sched/mutex.c").read_text(encoding="utf-8")
        scheduler = (ROOT / "kernel/sched/scheduler.c").read_text(
            encoding="utf-8")
        self.assertIn("KERNEL_MUTEX_RECURSION_LIMIT", header)
        self.assertIn("kernel_mutex_lock_until", header)
        self.assertIn("kernel_mutex_lock_for", header)
        self.assertNotIn("KASSERT_CAN_SLEEP();", source)
        self.assertIn("bool may_block = scheduler_can_sleep();", source)
        self.assertIn("pit_monotonic_ms()", source)
        self.assertIn("wait_queue_block_until_spinlocked(", source)
        self.assertIn("&mutex->state_lock, flags", source)
        self.assertIn("wait_queue_wake_one_locked(&mutex->waiters)", source)
        self.assertIn("scheduler_current_task_identity", source)
        self.assertIn("owner_generation", header)
        self.assertIn("kernel_mutex_abandon_task_owner", header)
        self.assertIn("kernel_mutex_trylock_pinned", header)
        self.assertIn("owner_preempt_pinned", header)
        self.assertIn("scheduler_current_task_identity_pinned", source)
        self.assertIn("scheduler_mutex_owner_register", source)
        self.assertIn("scheduler_mutex_owner_unregister", source)
        self.assertIn("SCHEDULER_HELD_MUTEX_CAPACITY 8U", (ROOT /
                      "kernel/sched/scheduler.h").read_text(encoding="utf-8"))
        self.assertIn("scheduler_abandon_task_mutexes", scheduler)
        terminate = scheduler[
            scheduler.index("void scheduler_terminate_task("):
            scheduler.index("void task_exit(void)")]
        self.assertLess(terminate.index("scheduler_abandon_task_mutexes("),
                        terminate.index("process_close_all_files(process)"))
        abandon = source[source.index(
            "bool kernel_mutex_abandon_task_owner("):]
        for reset in (
                "mutex->owner_task = KERNEL_MUTEX_NO_OWNER_TASK",
                "mutex->owner_generation = 0U",
                "mutex->owner_cpu = X86_CPU_INDEX_INVALID",
                "mutex->recursion_depth = 0U",
                "wait_queue_wake_one_locked(&mutex->waiters)"):
            self.assertIn(reset, abandon)
        self.assertIn("if (identity.task < 0)", source)
        self.assertIn("scheduler_preempt_disable();", source)
        self.assertIn("if (release_kernel_preempt_guard)", source)
        self.assertIn("scheduler_preempt_enable();", source)
        contention = source[source.index("uint64_t now = pit_monotonic_ms()"):
                            source.index("wait_queue_block_until_spinlocked(")]
        self.assertIn("identity.task < 0 || !may_block", contention)
        self.assertIn("return KERNEL_MUTEX_WOULD_BLOCK;", contention)
        self.assertIn("uint32_t task_generation", (ROOT /
                      "kernel/sched/scheduler.h").read_text(encoding="utf-8"))
        self.assertIn("KASSERT(next_task_generation != UINT32_MAX)", scheduler)
        wait = source.index("wait_queue_block_until_spinlocked(")
        self.assertNotIn("spinlock_acquire(&mutex->state_lock)",
                         source[wait:source.index("if (result != 0)", wait)])
        smp = (ROOT / "arch/x86/cpu/smp.c").read_text(encoding="utf-8")
        self.assertIn("kernel_mutex_lock_for(&smp_scheduler_mutex, 500U)", smp)
        self.assertIn("pit_delay(1U);", smp)
        self.assertIn("REIST_SMP MUTEX_READY", smp)

    def test_storage_stack_uses_sleepable_mutexes(self):
        vfs = (ROOT / "fs/vfs/vfs.c").read_text(encoding="utf-8")
        fat32 = (ROOT / "fs/fat32/fat32.c").read_text(encoding="utf-8")
        ata = (ROOT / "drivers/block/ata.c").read_text(encoding="utf-8")
        ahci = (ROOT / "drivers/block/ahci.c").read_text(encoding="utf-8")
        fdd = (ROOT / "drivers/block/fdd.c").read_text(encoding="utf-8")
        for source, mutex in (
                (vfs, "vfs_operation_mutex"),
                (fat32, "fat32_operation_mutex"),
                (ata, "ata_transaction_mutex"),
                (ahci, "port_mutex"),
                (fdd, "fdd_transaction_mutex")):
            with self.subTest(mutex=mutex):
                self.assertIn(f"kernel_mutex_t {mutex}", source)
                if mutex == "port_mutex":
                    self.assertIn("kernel_mutex_lock_until(&port_mutex", source)
                    self.assertIn("kernel_mutex_unlock(&port_mutex", source)
                else:
                    self.assertIn(f"kernel_mutex_lock_for(&{mutex}", source)
                    self.assertIn(f"kernel_mutex_unlock(&{mutex})", source)
        self.assertNotIn("scheduler_preempt_disable();", vfs)
        self.assertNotIn("scheduler_preempt_disable();", fat32)
        self.assertNotIn("scheduler_preempt_disable();", ata)
        self.assertNotIn("scheduler_preempt_disable();", ahci)
        self.assertNotIn("scheduler_preempt_disable();", fdd)

    def test_smp_parallel_probe_exercises_read_only_storage(self):
        kernel = (ROOT / "kernel/init/kernel.c").read_text(encoding="utf-8")
        smp = (ROOT / "arch/x86/cpu/smp.c").read_text(encoding="utf-8")
        self.assertIn("block_device_read_sector", kernel)
        self.assertIn("smp_storage_probe_worker", kernel)
        self.assertIn("x86_smp_set_parallel_probe", kernel)
        self.assertIn("smp_parallel_probe_arrived_mask", smp)
        self.assertIn("smp_parallel_probe_passed_mask", smp)
        self.assertIn("SMP_PARALLEL_PROBE_TIMEOUT_MS", smp)
        self.assertIn("REIST_SMP SUBSYSTEM_READY", smp)

    def test_smp_parallel_probe_injects_bounded_integrity_faults(self):
        kernel = (ROOT / "kernel/init/kernel.c").read_text(encoding="utf-8")
        smp = (ROOT / "arch/x86/cpu/smp.c").read_text(encoding="utf-8")
        self.assertIn("smp_integrity_shared", kernel)
        self.assertIn("smp_integrity_private[X86_SMP_MAX_CPUS]", kernel)
        self.assertIn("critical_object_update(&smp_integrity_shared", kernel)
        self.assertIn("CRITICAL_READ_CORRECTED", kernel)
        self.assertIn("CRITICAL_READ_RECOVERED", kernel)
        self.assertIn("CRITICAL_READ_UNCORRECTABLE", kernel)
        self.assertIn("REIST_SMP INTEGRITY_READY", smp)

    def test_process_lifecycle_is_locked_before_scheduler_state(self):
        process = (ROOT / "kernel/proc/process.c").read_text(encoding="utf-8")
        scheduler = (ROOT / "kernel/sched/scheduler.c").read_text(
            encoding="utf-8")
        syscall = (ROOT / "kernel/syscall/syscall_table.c").read_text(
            encoding="utf-8")
        header = (ROOT / "kernel/proc/process.h").read_text(encoding="utf-8")
        self.assertIn("process_table_lock = SPINLOCK_INIT", process)
        self.assertIn("bool terminating;", header)
        self.assertIn("static Process process_list[MAX_PROGRAMS]", process)
        wait_start = syscall.index("static int syscall_wait(")
        wait_end = syscall.index("static int syscall_process_info(", wait_start)
        wait = syscall[wait_start:wait_end]
        self.assertIn("process_table_lock_irqsave()", wait)
        self.assertIn("wait_queue_block_until_spinlocked(", wait)
        self.assertIn("process_table_lock_ref(), flags", wait)
        terminate_start = scheduler.index("void scheduler_terminate_task(")
        terminate_end = scheduler.index("void task_exit(", terminate_start)
        terminate = scheduler[terminate_start:terminate_end]
        self.assertLess(terminate.index("process_table_lock_irqsave()"),
                        terminate.index("spinlock_acquire(&task_table_lock)"))
        self.assertLess(terminate.index("process_close_all_files("),
                        terminate.index(
                            "uint32_t flags = process_table_lock_irqsave();"))
        close = terminate.index("process_close_all_files(")
        self.assertLess(terminate.index("scheduler_preempt_enable();"), close)
        self.assertGreater(terminate.index("scheduler_preempt_disable();",
                                           close), close)
        exit_start = scheduler.index("void task_exit_status(")
        exit_end = scheduler.index("void scheduler_kill_current(", exit_start)
        exit_path = scheduler[exit_start:exit_end]
        cleanup = exit_path.index("process_close_all_files(")
        self.assertNotIn("scheduler_preempt_disable();", exit_path[:cleanup])


    def test_fault_tolerant_driver_domain_is_explicitly_ap_affined(self):
        supervisor = (ROOT / "include/kernel/supervisor.h").read_text(
            encoding="utf-8")
        kernel = (ROOT / "kernel/init/kernel.c").read_text(encoding="utf-8")
        scheduler = (ROOT / "kernel/sched/scheduler.c").read_text(
            encoding="utf-8")
        self.assertIn("uint32_t cpu_affinity_mask;", supervisor)
        self.assertIn(".cpu_affinity_mask = driver_fault_ap_mask", kernel)
        self.assertIn("local == NULL || local->online == 0U", scheduler)
        self.assertIn('"DRIVER_DOMAIN AP_EXEC cpu=%u', scheduler)


if __name__ == "__main__":
    unittest.main()
