"""Source and ABI regressions for the R1.2 memory-management milestone."""

from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


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


def typedef_fields(source: str, typedef_name: str) -> list[tuple[str, str]]:
    match = re.search(
        rf"typedef\s+struct\s*\{{(?P<body>[^{{}}]*)\}}\s*"
        rf"{typedef_name}\s*;",
        source,
        flags=re.DOTALL,
    )
    if match is None:
        raise AssertionError(f"missing typedef {typedef_name}")
    return re.findall(
        r"\b(uint(?:32|64)_t)\s+([A-Za-z_][A-Za-z0-9_]*)\s*;",
        match.group("body"),
    )


def make_target_body(makefile: str, target: str) -> str:
    match = re.search(
        rf"(?ms)^{re.escape(target)}:\s+[^\n]*\n"
        r"(?P<body>(?:\t[^\n]*(?:\n|$)|\s*\n)*)",
        makefile,
    )
    if match is None:
        raise AssertionError(f"missing make target {target}")
    return match.group("body")


class DirectMapAndPhysicalAllocatorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.paging_header = (ROOT / "arch/x86/mm/paging.h").read_text(
            encoding="utf-8"
        )
        cls.paging = (ROOT / "arch/x86/mm/paging.c").read_text(
            encoding="utf-8"
        )
        cls.memory = (ROOT / "mm/kmalloc.c").read_text(encoding="utf-8")

    def test_kernel_direct_map_covers_exactly_the_managed_first_gib(self):
        self.assertRegex(
            self.paging_header,
            r"(?m)^#define\s+KERNEL_PAGE_ENTRIES\s+256U\b",
        )
        self.assertRegex(
            self.paging_header,
            r"(?m)^#define\s+KERNEL_IDENTITY_LIMIT\s+"
            r"\(KERNEL_PAGE_ENTRIES\s*\*\s*PAGE_TABLE_ENTRIES\s*\*\s*"
            r"PAGE_SIZE\)",
        )
        self.assertIn(
            "_Static_assert(KERNEL_IDENTITY_LIMIT == USER_BASE",
            self.paging,
        )
        init = function_block(self.paging, "void init_paging(")
        self.assertIn("directory_index < KERNEL_PAGE_ENTRIES", init)
        self.assertIn("kernel_page_tables[directory_index]", init)
        self.assertRegex(
            self.memory,
            r"#define\s+PHYSICAL_MEMORY_LIMIT\s+"
            r"\(\(uint64_t\)KERNEL_IDENTITY_LIMIT\)",
        )

    def test_boot_self_test_writes_a_frame_at_or_above_256_mib(self):
        test = function_block(self.memory, "void test_memory(")
        self.assertIn(
            "allocate_frame_at_or_above(256U * 1024U * 1024U)", test
        )
        self.assertIn("volatile uint32_t *words", test)
        self.assertIn("words[0] =", test)
        self.assertIn("(FRAME_SIZE / sizeof(uint32_t)) - 1U", test)
        self.assertIn("free_frame(high_frame)", test)
        self.assertRegex(test, r"if\s*\(passed\s*!=\s*total\)")
        self.assertIn("panic(", test)


class MemoryMapAndHeapTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.memory = (ROOT / "mm/kmalloc.c").read_text(encoding="utf-8")
        cls.parser = (
            ROOT / "arch/x86/boot/multiboot_parser.c"
        ).read_text(encoding="utf-8")
        cls.guest = (ROOT / "examples/userspace/guest_test.c").read_text(
            encoding="utf-8"
        )

    def test_memory_regions_are_merged_sorted_and_capacity_checked(self):
        normalize = function_block(
            self.memory, "static int add_normalized_region("
        )
        compact = re.sub(r"\s+", " ", normalize)
        self.assertIn("base <= current_end", compact)
        self.assertIn("regions[i].base <= end", compact)
        self.assertIn("--(*count)", normalize)
        self.assertIn("if (*count >= MAX_MEMORY_REGIONS) return -1", compact)
        self.assertIn("regions[insert].base = base", normalize)
        self.assertIn("regions[insert].length = end - base", normalize)

    def test_nonusable_e820_regions_are_reserved_fail_closed(self):
        parse = function_block(self.parser, "void parse_multiboot1_info(")
        mmap_loop = parse[parse.index("while ((size_t)(mmap_end - cursor)") :]
        self.assertRegex(
            re.sub(r"\s+", " ", mmap_loop),
            r"if\s*\(mmap->type\s*==\s*1\).*?else\s+if\s*"
            r"\(mmap->length\s*!=\s*0\s*&&\s*memory_reserve_region\(",
        )
        fatal = mmap_loop.index(
            '"Fatal: reserved memory-map regions exceed capacity.\\n"'
        )
        recovery = mmap_loop[fatal : fatal + 220]
        self.assertIn("memory_map_reset();", recovery)
        self.assertIn("return;", recovery)

    def test_every_critical_multiboot_region_uses_fail_closed_reservation(self):
        helper = function_block(self.parser, "static bool reserve_boot_region(")
        compact_helper = re.sub(r"\s+", " ", helper)
        self.assertIn("memory_reserve_region(base, length) == 0", compact_helper)
        self.assertIn("memory_map_reset();", helper)
        self.assertIn("return false;", helper)

        parse = function_block(self.parser, "void parse_multiboot1_info(")
        compact = re.sub(r"\s+", " ", parse)
        reservations = (
            "reserve_boot_region((uint32_t)(uintptr_t)mb_info, "
            'sizeof(*mb_info), "Multiboot information")',
            "reserve_boot_region(mb_info->mmap_addr, mb_info->mmap_length, "
            '"Multiboot memory-map storage")',
            "reserve_boot_region( mb_info->mods_addr, (uint64_t)"
            "mb_info->mods_count * sizeof(multiboot1_module_t), "
            '"Multiboot module table")',
            "reserve_boot_region( mods[i].mod_start, mods[i].mod_end - "
            'mods[i].mod_start, "Multiboot module payload")',
        )
        for reservation in reservations:
            with self.subTest(reservation=reservation):
                self.assertIn(reservation, compact)

        # Every helper call is checked immediately; a reservation failure may
        # never leave a partially accepted boot memory map behind.
        self.assertEqual(
            compact.count("reserve_boot_region("),
            compact.count("!reserve_boot_region("),
        )

    def test_malformed_and_truncated_multiboot_maps_fail_closed(self):
        parse = function_block(self.parser, "void parse_multiboot1_info(")
        compact = re.sub(r"\s+", " ", parse)
        self.assertIn(
            "mmap->size < sizeof(multiboot1_mmap_entry_t) - "
            "sizeof(mmap->size)",
            compact,
        )
        self.assertIn(
            "(size_t)mmap->size > remaining - sizeof(mmap->size)",
            compact,
        )
        self.assertIn("if (cursor != mmap_end)", compact)
        for diagnostic in (
            '"Fatal: malformed Multiboot memory-map entry.\\n"',
            '"Fatal: truncated Multiboot memory map.\\n"',
        ):
            with self.subTest(diagnostic=diagnostic):
                start = parse.index(diagnostic)
                fail_path = parse[start : start + 180]
                self.assertIn("memory_map_reset();", fail_path)
                self.assertIn("return;", fail_path)

        # Pointer addition is validated before cursor/end construction, and
        # exhausting normalized usable-region capacity is fatal as well.
        self.assertIn(
            "mb_info->mmap_length <= UINTPTR_MAX - mb_info->mmap_addr",
            compact,
        )
        usable = function_block(
            self.parser, "static bool add_boot_usable_region("
        )
        self.assertIn("memory_map_reset();", usable)
        self.assertIn("return false;", usable)

    def test_reserved_regions_override_overlapping_usable_regions(self):
        check = function_block(self.memory, "int memory_region_is_usable(")
        self.assertLess(
            check.index("reserved_region_count"),
            check.index("usable_region_count"),
        )
        self.assertIn("base < reserved_end", check)
        self.assertIn("return 0;", check)

    def test_heap_grows_by_pinned_direct_map_arenas(self):
        self.assertRegex(
            self.memory,
            r"#define\s+INITIAL_HEAP_SIZE\s+\(1U\s*\*\s*1024U\s*\*\s*1024U\)",
        )
        self.assertRegex(
            self.memory,
            r"#define\s+HEAP_GROW_CHUNK\s+\(256U\s*\*\s*1024U\)",
        )
        extend = function_block(self.memory, "static bool extend_heap_locked(")
        self.assertIn("reserve_contiguous_frames(", extend)
        self.assertIn("++heap_arena_count", extend)
        self.assertIn("heap_backing_bytes += arena_size", extend)
        self.assertIn("insert_heap_block(block)", extend)
        malloc = function_block(self.memory, "void *k_malloc(")
        self.assertIn("extend_heap_locked(size)", malloc)

    def test_frame_and_heap_statistics_are_guarded_and_exercised(self):
        stats = function_block(self.memory, "void memory_get_stats(")
        self.assertIn("spinlock_acquire_irq(&frame_lock)", stats)
        self.assertIn("spinlock_acquire_irq(&heap_lock)", stats)
        for field in (
            "managed_bytes",
            "reserved_bytes",
            "allocated_frame_bytes",
            "free_frame_bytes",
            "heap_capacity_bytes",
            "heap_used_bytes",
            "heap_free_bytes",
            "heap_largest_free_block",
            "heap_arena_count",
        ):
            with self.subTest(field=field):
                self.assertIn(f"stats->{field}", stats)

        guest = function_block(self.guest, "static int test_memory_accounting(")
        self.assertIn(
            "before.managed_bytes != before.reserved_bytes +", guest
        )
        self.assertIn("before.allocated_frame_bytes + before.free_frame_bytes", guest)
        self.assertIn("before.heap_arena_count < 2U", guest)
        self.assertIn("before.heap_largest_free_block", guest)
        self.assertIn("reclaimed.managed_bytes != reclaimed.reserved_bytes +", guest)


class KernelStackAndReapingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.scheduler = (ROOT / "kernel/sched/scheduler.c").read_text(
            encoding="utf-8"
        )
        cls.scheduler_header = (
            ROOT / "kernel/sched/scheduler.h"
        ).read_text(encoding="utf-8")
        cls.process = (ROOT / "kernel/proc/process.c").read_text(
            encoding="utf-8"
        )
        cls.syscalls = (
            ROOT / "kernel/syscall/syscall_table.c"
        ).read_text(encoding="utf-8")
        cls.kernel = (ROOT / "kernel/init/kernel.c").read_text(
            encoding="utf-8"
        )
        cls.linker = (ROOT / "config/klink.ld").read_text(encoding="utf-8")
        cls.bootloader = (
            ROOT / "arch/x86/boot/bootloader.asm"
        ).read_text(encoding="utf-8")
        cls.guest = (ROOT / "examples/userspace/guest_test.c").read_text(
            encoding="utf-8"
        )

    def test_kernel_stacks_have_two_64_byte_canaries(self):
        self.assertRegex(
            self.scheduler,
            r"(?m)^#define\s+STACK_GUARD_WORDS\s+16U\b",
        )
        allocate = function_block(
            self.scheduler, "uint32_t *scheduler_allocate_kernel_stack("
        )
        validate = function_block(
            self.scheduler, "bool scheduler_kernel_stack_is_valid("
        )
        release = function_block(
            self.scheduler, "void scheduler_free_kernel_stack("
        )
        self.assertIn("STACK_SIZE + 2U * STACK_GUARD_BYTES", allocate)
        self.assertIn("lower[i] = stack_guard_value(i, false)", allocate)
        self.assertIn("upper[i] = stack_guard_value(i, true)", allocate)
        self.assertIn("lower[i] != stack_guard_value(i, false)", validate)
        self.assertIn("upper[i] != stack_guard_value(i, true)", validate)
        self.assertIn("scheduler_kernel_stack_is_valid(stack)", release)
        self.assertIn('panic("Kernel stack guard corrupted")', release)

    def test_static_kernel_stack_has_a_linker_reserved_64_byte_guard(self):
        stack_section = self.linker[
            self.linker.index(".stack ALIGN(4096)") :
            self.linker.index("} > kernel_ram", self.linker.index(".stack ALIGN(4096)"))
        ]
        compact = re.sub(r"\s+", " ", stack_section)
        self.assertRegex(
            compact,
            r"_stack_guard_start\s*=\s*\.\s*;\s*\.\s*\+=\s*64\s*;\s*"
            r"_stack_guard_end\s*=\s*\.\s*;\s*_stack_start\s*=\s*\.\s*;",
        )
        self.assertLess(
            stack_section.index("_stack_guard_end"),
            stack_section.index("_stack_start"),
        )
        self.assertIn(". += 8192", stack_section)

    def test_boot_entry_initializes_guard_before_calling_c(self):
        start = self.bootloader[self.bootloader.index("start:") :]
        guard = start[: start.index("; Debug markers")]
        for instruction in (
            "cld",
            "mov esp, _stack_end",
            "push eax",
            "push ebx",
            "mov edi, _stack_guard_start",
            "mov eax, 0x4B535447",
            "mov ecx, 16",
            "rep stosd",
            "pop ebx",
            "pop eax",
        ):
            with self.subTest(instruction=instruction):
                self.assertIn(instruction, guard)
        self.assertLess(start.index("rep stosd"), start.index("call kernel_main"))
        self.assertLess(guard.index("push eax"), guard.index("rep stosd"))
        self.assertLess(guard.index("rep stosd"), guard.index("pop eax"))

    def test_scheduler_and_kernel_validate_the_static_stack_guard(self):
        compact_header = re.sub(r"\s+", " ", self.scheduler_header)
        self.assertIn(
            "bool scheduler_kernel_context_stack_is_valid(void);",
            compact_header,
        )
        validate = function_block(
            self.scheduler, "bool scheduler_kernel_context_stack_is_valid("
        )
        compact = re.sub(r"\s+", " ", validate)
        self.assertIn("&_stack_guard_start", validate)
        self.assertIn("&_stack_guard_end", validate)
        self.assertIn("!= 64U", compact)
        self.assertIn("*guard++ != KERNEL_STACK_GUARD", compact)

        kernel_main = function_block(self.kernel, "void kernel_main(")
        self.assertLess(
            kernel_main.index("scheduler_kernel_context_stack_is_valid()"),
            kernel_main.index("multiboot_magic != MULTIBOOT1_BOOTLOADER_MAGIC"),
        )
        self.assertIn('panic("Static kernel stack guard was not initialized")',
                      kernel_main)
        # Definition plus checks on scheduler transitions to/from the static
        # kernel context.
        self.assertGreaterEqual(
            self.scheduler.count("validate_kernel_context_stack_or_panic("),
            3,
        )

    def test_single_task_reaping_is_locked_and_owner_validated(self):
        compact_header = re.sub(r"\s+", " ", self.scheduler_header)
        self.assertIn(
            "int scheduler_reap_finished_task_locked(int task_id, "
            "const Process* owner);",
            compact_header,
        )
        reap = function_block(
            self.scheduler, "int scheduler_reap_finished_task_locked("
        )
        compact = re.sub(r"\s+", " ", reap)
        self.assertIn("irq_enabled()", reap)
        self.assertIn("tasks[task_id].status != TASK_FINISHED", compact)
        self.assertIn("tasks[task_id].process != owner", compact)
        self.assertIn("release_task_resources(&tasks[task_id])", reap)

    def test_reaping_detaches_under_irq_and_frees_resources_after_restore(self):
        self.assertRegex(
            self.scheduler_header,
            r"(?m)^#define\s+TASK_REAPING\s+5\b",
        )
        for field in ("reap_kernel_stack", "reap_page_directory"):
            with self.subTest(field=field):
                self.assertIn(field, self.scheduler_header)

        detach = function_block(
            self.scheduler, "static void release_task_resources("
        )
        self.assertIn("task->reap_page_directory =", detach)
        self.assertIn("task->reap_kernel_stack = task->kernel_stack", detach)
        self.assertIn("task->page_directory = NULL", detach)
        self.assertIn("task->kernel_stack = NULL", detach)
        self.assertIn("task->status = TASK_REAPING", detach)
        self.assertNotIn("free_page_directory(", detach)
        self.assertNotIn("scheduler_free_kernel_stack(", detach)

        reap_all = function_block(
            self.scheduler, "size_t scheduler_reap_finished_tasks("
        )
        compact = re.sub(r"\s+", " ", reap_all)
        first_save = reap_all.index("irq_save()")
        detach_call = reap_all.index("release_task_resources(task)")
        first_restore = reap_all.index("irq_restore(flags)", detach_call)
        self.assertLess(first_save, detach_call)
        self.assertLess(detach_call, first_restore)
        self.assertIn("task->status == TASK_REAPING", compact)

        free_directory = reap_all.index("free_page_directory(page_directory)")
        free_stack = reap_all.index("scheduler_free_kernel_stack(kernel_stack)")
        claimed = reap_all.index("page_directory = task->reap_page_directory")
        claim_restore = reap_all.index("irq_restore(flags)", claimed)
        final_save = reap_all.index("flags = irq_save()", free_stack)
        self.assertLess(claim_restore, free_directory)
        self.assertLess(claim_restore, free_stack)
        self.assertLess(free_directory, final_save)
        self.assertLess(free_stack, final_save)
        self.assertLess(
            reap_all.index("scheduler_preempt_disable()"), first_save
        )
        self.assertGreater(
            reap_all.index("scheduler_preempt_enable()"), final_save
        )

        # No expensive page-table walk or heap free is allowed in any of the
        # explicit interrupt-disabled regions in this routine.
        for match in re.finditer(r"(?:uint32_t\s+)?flags\s*=\s*irq_save\(\);",
                                 reap_all):
            critical = reap_all[
                match.start() : reap_all.index("irq_restore(flags);", match.end())
            ]
            self.assertNotIn("free_page_directory(", critical)
            self.assertNotIn("scheduler_free_kernel_stack(", critical)

    def test_wait_completes_detached_reaping_after_restoring_interrupts(self):
        wait = function_block(self.syscalls, "static int syscall_wait(")
        completed = wait[wait.index("if (result > 0)") :]
        self.assertLess(
            completed.index("irq_restore(flags)"),
            completed.index("scheduler_reap_finished_tasks()"),
        )
        wrapper = function_block(self.process, "int process_wait_status(")
        self.assertLess(
            wrapper.index("irq_restore(flags)"),
            wrapper.index("scheduler_reap_finished_tasks()"),
        )

    def test_finished_tasks_are_reaped_before_process_allocations(self):
        release = function_block(
            self.scheduler, "static void release_task_resources("
        )
        self.assertIn("task->process->task_id = -1", release)
        reap_all = function_block(
            self.scheduler, "size_t scheduler_reap_finished_tasks("
        )
        self.assertIn("task->status == TASK_FINISHED", reap_all)
        self.assertIn("release_task_resources(task)", reap_all)
        create_task = function_block(self.scheduler, "int create_task(")
        self.assertIn("tasks[i].reap_kernel_stack == NULL", create_task)
        self.assertIn("tasks[i].reap_page_directory == NULL", create_task)

        create_user = function_block(
            self.process, "static int create_process_for_file_args_owned("
        )
        self.assertLess(
            create_user.index("scheduler_reap_finished_tasks()"),
            create_user.index("create_page_directory()"),
        )
        create_kernel = function_block(self.process, "int create_process(")
        self.assertLess(
            create_kernel.index("scheduler_reap_finished_tasks()"),
            create_kernel.index("scheduler_allocate_kernel_stack()"),
        )

    def test_wait_passes_the_process_owner_and_stress_checks_for_leaks(self):
        wait = function_block(
            self.process, "int process_wait_status_locked("
        )
        self.assertRegex(
            re.sub(r"\s+", " ", wait),
            r"scheduler_reap_finished_task_locked\(child->task_id, child\)",
        )
        self.assertRegex(
            self.guest,
            r"(?m)^#define\s+WAIT_STRESS_ITERATIONS\s+64\b",
        )
        stress = function_block(self.guest, "static int test_wait_wakeup(")
        self.assertIn("iteration < WAIT_STRESS_ITERATIONS", stress)
        self.assertIn("after.allocated_frame_bytes == before.allocated_frame_bytes", stress)
        self.assertIn("after.heap_used_bytes == before.heap_used_bytes", stress)


class SpawnvAndMemoryStatsAbiTests(unittest.TestCase):
    EXPECTED_FIELDS = [
        ("uint32_t", "version"),
        ("uint32_t", "struct_size"),
        ("uint64_t", "detected_usable_bytes"),
        ("uint64_t", "managed_bytes"),
        ("uint64_t", "reserved_bytes"),
        ("uint64_t", "allocated_frame_bytes"),
        ("uint64_t", "free_frame_bytes"),
        ("uint64_t", "heap_capacity_bytes"),
        ("uint64_t", "heap_used_bytes"),
        ("uint64_t", "heap_free_bytes"),
        ("uint64_t", "heap_largest_free_block"),
        ("uint64_t", "heap_arena_count"),
    ]

    @classmethod
    def setUpClass(cls) -> None:
        cls.memory_header = (ROOT / "mm/kmalloc.h").read_text(
            encoding="utf-8"
        )
        cls.memory = (ROOT / "mm/kmalloc.c").read_text(encoding="utf-8")
        cls.kernel_api = (ROOT / "lib/libc/stdlib.h").read_text(
            encoding="utf-8"
        )
        cls.user_header = (
            ROOT / "userspace/sdk/include/x86os.h"
        ).read_text(encoding="utf-8")
        cls.user_sdk = (ROOT / "userspace/sdk/x86os.c").read_text(
            encoding="utf-8"
        )
        cls.syscalls = (
            ROOT / "kernel/syscall/syscall_table.c"
        ).read_text(encoding="utf-8")

    def test_spawnv_copies_argument_storage_to_the_kernel_heap(self):
        spawnv = function_block(self.syscalls, "static int syscall_spawnv(")
        self.assertIn("char *arguments = (char*)k_malloc(", spawnv)
        self.assertIn("(size_t)argc * SYSCALL_ARGUMENT_CAPACITY", spawnv)
        self.assertNotRegex(
            spawnv,
            r"char\s+arguments\s*\[\s*SYSCALL_MAX_ARGUMENTS\s*\]",
        )
        self.assertGreaterEqual(spawnv.count("k_free(arguments)"), 3)
        self.assertLess(
            spawnv.index("process_spawn_args("),
            spawnv.rindex("k_free(arguments)"),
        )

    def test_syscall_43_v1_struct_is_identical_and_88_bytes(self):
        self.assertRegex(
            self.kernel_api,
            r"(?m)^#define\s+SYS_MEMORY_STATS\s+43\b",
        )
        self.assertRegex(
            self.user_header,
            r"\bX86OS_SYS_MEMORY_STATS\s*=\s*43\b",
        )
        self.assertRegex(
            self.memory_header,
            r"(?m)^#define\s+MEMORY_STATS_VERSION\s+1U\b",
        )
        self.assertRegex(
            self.user_header,
            r"(?m)^#define\s+X86OS_MEMORY_STATS_VERSION\s+1U\b",
        )
        self.assertEqual(
            typedef_fields(self.memory_header, "memory_stats_t"),
            self.EXPECTED_FIELDS,
        )
        self.assertEqual(
            typedef_fields(self.user_header, "x86os_memory_stats_t"),
            self.EXPECTED_FIELDS,
        )
        self.assertIn("sizeof(memory_stats_t) == 88U", self.memory)
        self.assertIn("sizeof(x86os_memory_stats_t) == 88U", self.user_sdk)

    def test_syscall_43_negotiates_version_and_size_before_copyout(self):
        syscall = function_block(
            self.syscalls, "static int syscall_memory_stats("
        )
        compact = re.sub(r"\s+", " ", syscall)
        self.assertIn("version != MEMORY_STATS_VERSION", compact)
        self.assertIn("user_size < sizeof(memory_stats_t)", compact)
        self.assertIn("return -22", compact)
        self.assertIn("copy_to_user(user_stats, &stats, sizeof(stats))", compact)

        wrapper = function_block(
            self.user_sdk, "int x86os_memory_stats("
        )
        self.assertIn("X86OS_SYS_MEMORY_STATS", wrapper)
        self.assertIn("(uintptr_t)stats, sizeof(*stats)", wrapper)
        self.assertIn("X86OS_MEMORY_STATS_VERSION", wrapper)


class DynamicProgramStagingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.process = (ROOT / "kernel/proc/process.c").read_text(
            encoding="utf-8"
        )
        cls.memory = (ROOT / "mm/kmalloc.c").read_text(encoding="utf-8")
        cls.memory_header = (ROOT / "mm/kmalloc.h").read_text(
            encoding="utf-8"
        )

    def test_runtime_loader_has_no_fixed_physical_staging_region(self):
        runtime_memory_sources = (
            self.process + "\n" + self.memory + "\n" + self.memory_header
        )
        for obsolete in (
            "PROGRAM_STAGING_ADDRESS",
            "KERNEL_PROGRAM_REGION_START",
            "KERNEL_PROGRAM_REGION_SIZE",
            "load_program_into_memory",
        ):
            with self.subTest(obsolete=obsolete):
                self.assertNotIn(obsolete, runtime_memory_sources)
        self.assertNotIn("0x02100000", runtime_memory_sources.lower())

    def test_loader_allocates_exactly_the_validated_file_size(self):
        loader = function_block(self.process, "static int load_program_file(")
        compact = re.sub(r"\s+", " ", loader)
        self.assertIn("uint8_t **image_out", self.process[
            self.process.index("static int load_program_file(") :
            self.process.index("{", self.process.index("static int load_program_file("))
        ])
        self.assertIn("*image_out = NULL", loader)
        self.assertLess(
            loader.index("node->size > PROGRAM_REGION_SIZE"),
            loader.index("uint32_t loaded_size = node->size"),
        )
        self.assertIn("uint8_t *image = (uint8_t*)k_malloc(loaded_size)", compact)
        self.assertIn("vfs_read(node, 0, loaded_size, image)", compact)
        self.assertNotRegex(loader, r"k_malloc\s*\(\s*PROGRAM_REGION_SIZE\s*\)")
        self.assertIn("*image_out = image", loader)

    def test_loader_failure_paths_release_owned_file_and_heap_resources(self):
        loader = function_block(self.process, "static int load_program_file(")

        invalid_file = extract_block(
            loader,
            loader.index("{", loader.index("if (node->type != VFS_FILE")),
        )
        self.assertIn("vfs_close(node)", invalid_file)

        allocation_failure = extract_block(
            loader,
            loader.index("{", loader.index("if (image == NULL)")),
        )
        self.assertIn("vfs_close(node)", allocation_failure)

        read_failure = extract_block(
            loader,
            loader.index("{", loader.index("if (result != (int)loaded_size)")),
        )
        self.assertIn("vfs_close(node)", read_failure)
        self.assertIn("k_free(image)", read_failure)

        validation_start = loader.index("if (vfs_close(node) != VFS_OK")
        validation_brace = loader.index("{", validation_start)
        validation_condition = loader[validation_start:validation_brace]
        validation_failure = extract_block(loader, validation_brace)
        self.assertIn("vfs_close(node)", validation_condition)
        self.assertIn("program_image_validate(", validation_condition)
        self.assertIn("k_free(image)", validation_failure)

    def test_successful_staging_ownership_is_freed_after_copying(self):
        create = function_block(
            self.process, "static int create_process_for_file_args_owned("
        )
        self.assertIn("load_program_file(filename, &program_image)", create)
        self.assertIn("program_image + offset", create)

        page_directory_failure = extract_block(
            create,
            create.index("{", create.index("if (page_directory == NULL)")),
        )
        self.assertIn("k_free(program_image)", page_directory_failure)

        mapping_failures = list(
            re.finditer(r"if\s*\(frame\s*==\s*0\s*\|\|\s*map_page\(", create)
        )
        self.assertEqual(len(mapping_failures), 2)
        for failure in mapping_failures:
            block = extract_block(create, create.index("{", failure.start()))
            self.assertIn("free_page_directory(page_directory)", block)
            self.assertIn("k_free(program_image)", block)

        releases = [
            match.start()
            for match in re.finditer(r"k_free\(program_image\);", create)
        ]
        self.assertEqual(len(releases), 4)
        successful_release = releases[-1]
        self.assertLess(
            successful_release,
            create.index("scheduler_allocate_kernel_stack()"),
        )
        self.assertNotIn("header->", create[successful_release:])


class MemorySmokePackagingTests(unittest.TestCase):
    MEMORY_MATRIX = ("32M", "64M", "256M", "1024M")

    def test_memory_smoke_matrix_runs_32_64_256_and_1024_mib(self):
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        body = make_target_body(makefile, "test-smoke-memory")
        self.assertEqual(body.count("scripts/run_qemu_smoke.py"), 4)
        for amount in self.MEMORY_MATRIX:
            with self.subTest(amount=amount):
                self.assertEqual(body.count(f"--memory {amount}"), 1)
                self.assertIn(
                    f"guest-smoke-memory-{amount.lower()}.log", body.lower()
                )

    def test_ci_runs_and_archives_the_memory_matrix(self):
        workflow = (ROOT / ".github/workflows/build.yml").read_text(
            encoding="utf-8"
        )
        self.assertIn("make test-smoke-memory", workflow)
        for amount in self.MEMORY_MATRIX:
            with self.subTest(amount=amount):
                self.assertIn(
                    f"guest-smoke-memory-{amount.lower()}.log",
                    workflow.lower(),
                )


if __name__ == "__main__":
    unittest.main()
