from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


class X8664BootstrapContractTests(unittest.TestCase):
    def read(self, relative: str) -> str:
        return (ROOT / relative).read_text(encoding="utf-8")

    def test_i386_remains_default_and_bootstrap_is_isolated(self):
        makefile = self.read("Makefile")
        self.assertIn("ARCH := x86", makefile)
        self.assertIn("CFLAGS := -m32", makefile)
        self.assertIn("LDFLAGS := -m elf_i386", makefile)
        self.assertIn("all: native-image", makefile)
        target = makefile.split("x86_64-bootstrap:", 1)[1].split("\n\n", 1)[0]
        self.assertIn("arch/x86_64/boot/entry.asm", target)
        self.assertIn("arch/x86_64/cpu/exceptions.asm", target)
        self.assertIn("arch/x86_64/mm/physical_memory.asm", target)
        self.assertIn("arch/x86_64/user/probe.asm", target)
        self.assertIn("arch/x86_64/exec/elf64_loader.asm", target)
        self.assertIn("arch/x86_64/proc/user_execution.asm", target)
        self.assertIn("arch/x86_64/proc/cooperative_scheduler.asm", target)
        self.assertIn("-f elf64", target)
        self.assertIn("-m elf_x86_64", target)
        self.assertNotIn("native-image", target)
        self.assertNotIn("kernel", target)

    def test_capability_check_precedes_paging_transition(self):
        source = self.read("arch/x86_64/boot/entry.asm")
        start = source.index("x86_64_bootstrap_start:")
        cpuid_check = source.index("call cpu_has_long_mode", start)
        pae = source.index("mov cr4, eax", cpuid_check)
        lme = source.index("wrmsr", pae)
        paging = source.index("mov cr0, eax", lme)
        transfer = source.index("jmp 0x08:long_mode_entry", paging)
        self.assertLess(cpuid_check, pae)
        self.assertLess(pae, lme)
        self.assertLess(lme, paging)
        self.assertLess(paging, transfer)
        self.assertIn("test edx, CPUID_LONG_MODE_BIT", source)
        self.assertIn("test edx, CPUID_NX_BIT", source)
        self.assertLess(source.index("call cpu_has_long_mode"), source.index("wrmsr"))

    def test_page_tables_and_serial_polling_are_fixed(self):
        source = self.read("arch/x86_64/boot/entry.asm")
        self.assertIn("mov ecx, (5 * 4096) / 4", source)
        self.assertEqual(source.count("resb 4096"), 5)
        self.assertIn("resb 16384", source)
        self.assertIn("SERIAL_TX_POLLS     equ 65536", source)
        self.assertEqual(source.count("mov ecx, SERIAL_TX_POLLS"), 2)
        linker = self.read("config/x86_64_bootstrap.ld")
        self.assertIn("_x86_64_bootstrap_end <= 0x00200000", linker)
        for boundary in ("text", "rodata", "data", "bss"):
            self.assertIn(f"_{boundary}_start", linker)
            self.assertIn(f"_{boundary}_end", linker)
        self.assertGreaterEqual(linker.count("ALIGN(4096)"), 8)

    def test_higher_half_map_is_fixed_and_low_transition_is_revoked(self):
        source = self.read("arch/x86_64/boot/entry.asm")
        self.assertIn("HIGHER_HALF_BASE     equ 0xFFFFFFFF80000000", source)
        self.assertIn("mov dword [pml4_table + (511 * 8)]", source)
        self.assertIn("mov dword [pdpt_table + (510 * 8)]", source)
        self.assertIn("mov dword [low_page_directory], PAGE_2M_FLAGS", source)
        self.assertIn("higher_half_entry:", source)
        switch = source.index("lea rsp, [rel bootstrap_stack_top]")
        revoke = source.index("mov qword [rel pml4_table], 0", switch)
        flush = source.index("mov cr3, rax", revoke)
        marker = source.index("higher_half_paging_message", flush)
        self.assertLess(switch, revoke)
        self.assertLess(revoke, flush)
        self.assertLess(flush, marker)

    def test_final_page_permissions_are_wx_exclusive(self):
        source = self.read("arch/x86_64/boot/entry.asm")
        self.assertIn("CR0_WP_BIT          equ (1 << 16)", source)
        self.assertIn("EFER_NXE_BIT        equ (1 << 11)", source)
        self.assertIn("PAGE_NX_HIGH        equ 0x80000000", source)
        self.assertIn("PAGE_HW_AD_MASK     equ 0x060", source)
        self.assertIn("or eax, CR0_PG_BIT | CR0_WP_BIT", source)
        self.assertIn("or eax, EFER_LME_BIT | EFER_NXE_BIT", source)
        self.assertRegex(
            source,
            re.compile(
                r"mov esi, _text_start.*?mov ebx, PAGE_PRESENT\s+"
                r"xor ecx, ecx.*?call map_high_pages32",
                re.DOTALL,
            ),
        )
        for start in ("_rodata_start", "_data_start", "_bss_start"):
            block = source[source.index(f"mov esi, {start}"):]
            self.assertIn("mov ecx, PAGE_NX_HIGH", block.split("call map_high_pages32", 1)[0])
        self.assertIn("and r8d, ~PAGE_HW_AD_MASK", source)

    def test_multiboot_memory_map_parser_is_fail_closed_and_bounded(self):
        source = self.read("arch/x86_64/mm/physical_memory.asm")
        self.assertIn("MULTIBOOT_BOOT_MAGIC equ 0x2BADB002", source)
        self.assertIn("MULTIBOOT_MMAP_FLAG equ (1 << 6)", source)
        self.assertIn("MAX_MMAP_BYTES      equ 4096", source)
        self.assertIn("MAX_MMAP_ENTRIES    equ 128", source)
        self.assertIn("MAX_MODULES         equ 32", source)
        self.assertIn("MMAP_ENTRY_MIN_SIZE equ 20", source)
        self.assertIn("parse_usable_pass32:", source)
        self.assertIn("parse_reserved_pass32:", source)
        self.assertIn(".reserved_alignment:", source)
        self.assertIn("jc physical_memory_fail32", source)
        self.assertIn("cmp eax, MAX_MMAP_BYTES", source)
        self.assertIn("cmp ebp, MAX_MMAP_ENTRIES", source)
        self.assertIn("cmp ecx, MAX_MODULES", source)

    def test_physical_frames_and_direct_map_have_fixed_authority(self):
        source = self.read("arch/x86_64/mm/physical_memory.asm")
        self.assertIn("MANAGED_LIMIT       equ 0x04000000", source)
        self.assertIn("FRAME_SIZE          equ 4096", source)
        self.assertIn("FRAME_COUNT         equ 16384", source)
        self.assertIn("FRAME_BITMAP_BYTES  equ 2048", source)
        self.assertIn("DIRECT_MAP_BASE     equ 0xFFFF800000000000", source)
        self.assertIn("DIRECT_PT_COUNT     equ 32", source)
        self.assertEqual(source.count("resb FRAME_BITMAP_BYTES"), 2)
        self.assertIn("resb DIRECT_PT_COUNT * 4096", source)
        self.assertIn("mov dword [pml4_table + (256 * 8) + 4], PAGE_NX_HIGH", source)
        self.assertIn("mov dword [direct_page_tables + eax * 8 + 4], PAGE_NX_HIGH", source)
        self.assertIn("bt dword [rel usable_bitmap], ecx", source)
        self.assertIn("bts dword [rel allocation_bitmap], ecx", source)
        self.assertIn("btr dword [rel allocation_bitmap], r8d", source)

    def test_physical_allocator_selftest_is_bounded_and_negative(self):
        source = self.read("arch/x86_64/mm/physical_memory.asm")
        runner = self.read("scripts/run_qemu_x86_64_boot.py")
        self.assertIn("x86_64_physical_memory_selftest64:", source)
        self.assertIn("cmp ecx, FRAME_COUNT", source)
        self.assertGreaterEqual(source.count("call physical_frame_alloc64"), 4)
        self.assertGreaterEqual(source.count("call physical_frame_free64"), 5)
        self.assertIn("test rdi, FRAME_SIZE - 1", source)
        self.assertIn("REIST_X86_64_PHYSICAL_MEMORY_OK", source)
        self.assertIn("REIST_X86_64_PHYSICAL_MEMORY_OK", runner)

    def test_elf64_probe_is_independently_linked_with_wx_segments(self):
        probe = self.read("arch/x86_64/user/probe.asm")
        linker = self.read("config/x86_64_user_probe.ld")
        self.assertIn("BITS 64", probe)
        self.assertIn("global _start", probe)
        self.assertIn("OUTPUT_FORMAT(elf64-x86-64)", linker)
        self.assertIn("ENTRY(_start)", linker)
        self.assertIn("text PT_LOAD FLAGS(5)", linker)
        self.assertIn("data PT_LOAD FLAGS(6)", linker)
        self.assertIn("ASSERT(_probe_end <= 0x00408000", linker)

    def test_elf64_loader_validates_standard_headers_before_allocation(self):
        source = self.read("arch/x86_64/exec/elf64_loader.asm")
        allocation = source.index("call physical_frame_alloc64")
        for validation in (
            "ELF_MAGIC           equ 0x464C457F",
            "ELFCLASS64          equ 2",
            "ELFDATA2LSB         equ 1",
            "ET_EXEC             equ 2",
            "EM_X86_64           equ 62",
            "ELF64_EHDR_SIZE     equ 64",
            "ELF64_PHDR_SIZE     equ 56",
            "MAX_PROGRAM_HEADERS equ 4",
            "MAX_LOAD_SEGMENTS   equ 2",
            "USER_PAGE_COUNT     equ 8",
        ):
            self.assertIn(validation, source)
            self.assertLess(source.index(validation), allocation)
        self.assertIn("jc elf64_load_fail", source[:allocation])
        self.assertIn("cmp rax, rdx", source[:allocation])
        self.assertIn("and edx, PF_W | PF_X", source[:allocation])

    def test_elf64_staging_is_bounded_verified_and_fully_cleaned(self):
        source = self.read("arch/x86_64/exec/elf64_loader.asm")
        runner = self.read("scripts/run_qemu_x86_64_boot.py")
        self.assertIn("incbin USER_PROBE_PATH", source)
        self.assertIn("cmp ecx, USER_PAGE_COUNT", source)
        self.assertIn("call elf_user_direct_pointer64", source)
        self.assertIn("call physical_frame_free64", source)
        self.assertIn("call physical_free_frame_count64", source)
        init = source.index("mov byte [rel elf_load_active], 0")
        load = source.index("call x86_64_elf64_load64")
        self.assertLess(init, load)
        marker = source.index("REIST_X86_64_ELF64_LOAD_OK")
        cleanup = source.index("call x86_64_elf64_release64")
        self.assertLess(cleanup, marker)
        self.assertIn("REIST_X86_64_ELF64_LOAD_OK", runner)

    def test_user_page_tables_are_private_fixed_and_wx(self):
        source = self.read("arch/x86_64/proc/user_execution.asm")
        self.assertIn("USER_PAGE_COUNT           equ 8", source)
        self.assertIn("USER_STACK_BASE           equ 0x00408000", source)
        self.assertIn("USER_STACK_TOP            equ 0x00409000", source)
        self.assertEqual(source.count("resb PAGE_SIZE"), 4)
        self.assertIn("mov qword [rel user_pml4 + (256 * 8)], rax", source)
        self.assertIn("mov qword [rel user_pml4 + (511 * 8)], rax", source)
        self.assertIn("or eax, PAGE_PRESENT | PAGE_WRITE | PAGE_USER", source)
        self.assertIn("and eax, PF_W | PF_X", source)
        self.assertIn("PAGE_HW_A_MASK            equ 0x020", source)
        self.assertIn("PAGE_HW_AD_MASK           equ 0x060", source)
        self.assertIn("and rdx, ~PAGE_HW_A_MASK", source)
        self.assertIn("and rdx, ~PAGE_HW_AD_MASK", source)
        self.assertIn("mov rdx, PAGE_NX", source)
        verify = source.index("call verify_user_context64")
        transfer = source.index("iretq")
        self.assertLess(verify, transfer)

    def test_ring3_syscall_uses_kernel_stack_and_exit_only(self):
        source = self.read("arch/x86_64/proc/user_execution.asm")
        probe = self.read("arch/x86_64/user/probe.asm")
        self.assertIn("USER_DATA_SELECTOR        equ 0x2B", source)
        self.assertIn("USER_CODE_SELECTOR        equ 0x33", source)
        self.assertIn("REIST_SYS_EXIT            equ 9", source)
        self.assertIn("mov eax, 9", probe)
        self.assertIn("mov edi, 100", probe)
        self.assertIn("push rdi", probe)
        self.assertIn("pop rdi", probe)
        self.assertIn("syscall", probe)
        self.assertIn("IA32_LSTAR", source)
        self.assertIn("IA32_KERNEL_GS_BASE", source)
        entry = source.index("x86_64_syscall_entry64:")
        stack = source.index("mov rsp, qword [gs:SYSCALL_CONTEXT_KERNEL_RSP]", entry)
        request = source.index("observed_syscall_number", stack)
        self.assertLess(stack, request)
        self.assertGreaterEqual(source.count("swapgs"), 2)
        self.assertNotRegex(source, re.compile(r"^\s*sysretq\s*$", re.MULTILINE))

    def test_cpl3_ud2_is_contained_without_widening_kernel_resume(self):
        exception = self.read("arch/x86_64/cpu/exceptions.asm")
        source = self.read("arch/x86_64/proc/user_execution.asm")
        probe = self.read("arch/x86_64/user/probe.asm")
        self.assertIn("probe_fault:", probe)
        self.assertGreaterEqual(probe.count("ud2"), 2)
        route = exception.index("call x86_64_user_exception64")
        kernel = exception.index(".kernel_exception:", route)
        self.assertLess(route, kernel)
        self.assertIn("cmp qword [rdi + EXCEPTION_FRAME_VECTOR], 6", source)
        self.assertIn("cmp qword [rdi + EXCEPTION_FRAME_ERROR], 0", source)
        self.assertIn("cmp qword [rdi + EXCEPTION_FRAME_CS], USER_CODE_SELECTOR", source)
        self.assertIn("cmp qword [rdi + EXCEPTION_FRAME_SS], USER_DATA_SELECTOR", source)
        self.assertIn("test rax, RFLAGS_RF", source)
        self.assertIn("test rax, RFLAGS_FAULT_FORBIDDEN", source)
        self.assertIn("call x86_64_elf64_address_flags64", source)
        self.assertIn("jmp exception_fatal", exception[route:kernel])

    def test_user_execution_cleanup_precedes_runtime_success(self):
        source = self.read("arch/x86_64/proc/user_execution.asm")
        runner = self.read("scripts/run_qemu_x86_64_boot.py")
        cleanup = source.index("call user_execution_cleanup64")
        marker = source.index("REIST_X86_64_USER_EXECUTION_OK")
        self.assertLess(cleanup, marker)
        self.assertIn("call disable_syscall_state64", source)
        self.assertIn("mov cr3, rax", source)
        self.assertIn("call physical_frame_free64", source)
        self.assertIn("call x86_64_elf64_release64", source)
        self.assertIn("call physical_free_frame_count64", source)
        self.assertIn("REIST_X86_64_USER_EXECUTION_OK", runner)
        self.assertIn("REIST_X86_64_USER_EXECUTION_ERROR", runner)

    def test_cooperative_scheduler_has_two_fixed_generation_scoped_slots(self):
        source = self.read("arch/x86_64/proc/cooperative_scheduler.asm")
        makefile = self.read("Makefile")
        self.assertIn("TASK_COUNT                 equ 2", source)
        self.assertIn("TASK_RECORD_SIZE           equ 256", source)
        self.assertIn("TASK_A_GENERATION          equ 1", source)
        self.assertIn("TASK_B_GENERATION          equ 2", source)
        for state in ("TASK_FREE", "TASK_READY", "TASK_RUNNING", "TASK_FAULTED", "TASK_EXITED"):
            self.assertIn(state, source)
        self.assertIn("resb TASK_COUNT * TASK_RECORD_SIZE", source)
        self.assertEqual(source.count("resb PAGE_SIZE"), 8)
        self.assertIn("cmp edi, TASK_COUNT", source)
        self.assertIn("cmp ebx, TASK_COUNT", source)
        self.assertIn("X86_64_PROCESS_SCHEDULER_OBJ", makefile)
        target = makefile.split("x86_64-bootstrap:", 1)[1].split("\n\n", 1)[0]
        self.assertIn("$(X86_64_PROCESS_SCHEDULER_OBJ)", target)

    def test_scheduler_clones_writable_pages_and_shares_only_rx(self):
        source = self.read("arch/x86_64/proc/cooperative_scheduler.asm")
        probe = self.read("arch/x86_64/user/probe.asm")
        self.assertIn("TASK_PRIVATE_FRAMES", source)
        self.assertIn("test r15d, PF_W", source)
        self.assertIn("call physical_frame_alloc64", source)
        self.assertIn("rep movsq", source)
        self.assertIn(".share_rx:", source)
        self.assertIn("test r15d, PF_X", source)
        self.assertIn("scheduler_verify_isolation64:", source)
        self.assertIn("PROBE_DATA_PAGE_INDEX      equ 1", source)
        self.assertIn("0xA11A11A11A11A11A", probe)
        self.assertIn("0xB22B22B22B22B22B", probe)
        self.assertGreaterEqual(probe.count("cmp qword [rel probe_data], rax"), 3)
        self.assertIn("scheduler_isolation_failure:", probe)
        self.assertIn("int3", probe)

    def test_scheduler_syscall_path_is_cooperative_and_bounded(self):
        source = self.read("arch/x86_64/proc/cooperative_scheduler.asm")
        probe = self.read("arch/x86_64/user/probe.asm")
        self.assertIn("REIST_SYS_EXIT             equ 9", source)
        self.assertIn("REIST_SYS_YIELD            equ 40", source)
        self.assertIn("scheduler_syscall_entry64:", source)
        entry = source.index("scheduler_syscall_entry64:")
        stack = source.index("mov rsp, qword [gs:SYSCALL_CONTEXT_KERNEL_RSP]", entry)
        request = source.index("mov qword [rel syscall_rax], rax", stack)
        self.assertLess(stack, request)
        self.assertIn("scheduler_handle_yield64:", source)
        self.assertIn("cmp eax, 3", source)
        self.assertIn("cmp edi, TASK_COUNT", source)
        self.assertIn("mov eax, 40", probe)
        self.assertNotRegex(source, re.compile(r"^\s*sysretq\s*$", re.MULTILINE))

    def test_scheduler_contains_b_fault_and_resumes_a(self):
        source = self.read("arch/x86_64/proc/cooperative_scheduler.asm")
        exception = self.read("arch/x86_64/cpu/exceptions.asm")
        probe = self.read("arch/x86_64/user/probe.asm")
        self.assertIn("x86_64_scheduler_user_exception64:", source)
        self.assertIn("cmp dword [rel scheduler_current_slot], 1", source)
        self.assertIn("cmp qword [rdi + EXCEPTION_FRAME_VECTOR], 6", source)
        self.assertIn("PROBE_FAULT_POINTER_OFFSET equ 8", source)
        self.assertIn("cmp rax, qword [rdi + EXCEPTION_FRAME_RIP]", source)
        self.assertIn("mov qword [r12 + TASK_STATE], TASK_FAULTED", source)
        self.assertIn("call scheduler_reap_terminal64", source)
        self.assertIn("xor edi, edi\n    jmp scheduler_enter_task64", source)
        self.assertIn("dq scheduler_fault", probe)
        self.assertIn("call x86_64_scheduler_user_exception64", exception)

    def test_scheduler_cleanup_and_marker_are_runtime_ordered(self):
        source = self.read("arch/x86_64/proc/cooperative_scheduler.asm")
        entry = self.read("arch/x86_64/boot/entry.asm")
        runner = self.read("scripts/run_qemu_x86_64_boot.py")
        self.assertIn("scheduler_verify_final_events64:", source)
        self.assertIn("cmp byte [rel scheduler_event_count], 14", source)
        cleanup = source.index("call scheduler_cleanup_common64")
        marker = source.index("REIST_X86_64_PROCESS_SCHEDULER_OK")
        self.assertLess(cleanup, marker)
        self.assertIn("call scheduler_disable_syscalls64", source)
        self.assertIn("call x86_64_exception_set_rsp0", source)
        self.assertIn("call x86_64_elf64_release64", source)
        self.assertIn("call physical_free_frame_count64", source)
        self.assertIn("call x86_64_process_scheduler_selftest64", entry)
        self.assertIn("REIST_X86_64_PROCESS_SCHEDULER_OK", runner)
        self.assertIn("REIST_X86_64_PROCESS_SCHEDULER_ERROR", runner)

    def test_success_marker_is_reachable_only_in_64_bit_section(self):
        source = self.read("arch/x86_64/boot/entry.asm")
        bits64 = source.index("BITS 64")
        entry = source.index("long_mode_entry:", bits64)
        state_checks = source.index("test eax, EFER_LMA_BIT", entry)
        success_load = source.index("lea rsi, [rel success_message]", state_checks)
        self.assertLess(bits64, entry)
        self.assertLess(entry, state_checks)
        self.assertLess(state_checks, success_load)
        self.assertEqual(source.count("REIST_X86_64_LONG_MODE_BOOT_OK"), 1)
        self.assertEqual(source.count("REIST_X86_64_HIGHER_HALF_PAGING_OK"), 1)

    def test_runner_is_single_cpu_memory_and_deadline_bounded(self):
        runner = self.read("scripts/run_qemu_x86_64_boot.py")
        self.assertIn('"-m", "32M"', runner)
        self.assertIn('"-smp", "1"', runner)
        self.assertIn("1.0 <= args.timeout <= 10.0", runner)
        self.assertIn("process.terminate()", runner)
        self.assertIn("process.kill()", runner)
        self.assertNotRegex(runner, re.compile(r"qemu-system-i386"))

    def test_exception_idt_tss_and_double_fault_ist_are_fixed(self):
        source = self.read("arch/x86_64/cpu/exceptions.asm")
        self.assertIn("EXCEPTION_VECTOR_COUNT equ 32", source)
        self.assertIn("IDT_GATE_SIZE         equ 16", source)
        self.assertIn("TSS64_SIZE            equ 104", source)
        self.assertIn("resb TSS64_SIZE", source)
        self.assertIn("resb 16384", source)
        self.assertEqual(source.count("IDT_GATE_PRESENT_INT"), 2)
        self.assertIn("cmp ecx, DOUBLE_FAULT_VECTOR", source)
        self.assertIn("mov byte [rdi + 4], TSS_IST1", source)
        self.assertLess(source.index("ltr ax"), source.index("lidt [rel exception_idt_pointer]"))

    def test_all_exception_frames_are_normalized_and_registers_preserved(self):
        source = self.read("arch/x86_64/cpu/exceptions.asm")
        error_vectors = {8, 10, 11, 12, 13, 14, 17, 21, 29, 30}
        emitted_error = {
            int(value) for value in re.findall(
                r"^EXCEPTION_ERR\s+(\d+)\s*$", source, re.MULTILINE
            )
        }
        emitted_plain = {
            int(value) for value in re.findall(
                r"^EXCEPTION_NOERR\s+(\d+)\s*$", source, re.MULTILINE
            )
        }
        self.assertEqual(emitted_error, error_vectors)
        self.assertEqual(emitted_plain, set(range(32)) - error_vectors)
        self.assertIn("EXCEPTION_FRAME_VECTOR equ (15 * 8)", source)
        for register in (
            "rax", "rbx", "rcx", "rdx", "rbp", "rsi", "rdi",
            "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
        ):
            self.assertIn(f"push {register}", source)
            self.assertIn(f"pop {register}", source)

    def test_ud2_and_nx_probes_require_exact_frames_and_runner_order(self):
        entry = self.read("arch/x86_64/boot/entry.asm")
        source = self.read("arch/x86_64/cpu/exceptions.asm")
        runner = self.read("scripts/run_qemu_x86_64_boot.py")
        self.assertIn("x86_64_ud2_probe:", entry)
        self.assertIn("ud2", entry)
        self.assertIn("x86_64_ud2_resume:", entry)
        self.assertIn("cmp qword [rsp + EXCEPTION_FRAME_VECTOR], 6", source)
        self.assertIn("cmp qword [rsp + EXCEPTION_FRAME_ERROR], 0", source)
        self.assertIn("cmp rax, rdx", source)
        self.assertIn("mov [rsp + EXCEPTION_FRAME_RIP], rdx", source)
        self.assertIn("x86_64_nx_probe_target:", entry)
        self.assertIn("x86_64_nx_resume:", entry)
        self.assertIn("cmp qword [rsp + EXCEPTION_FRAME_VECTOR], 14", source)
        self.assertIn("cmp qword [rsp + EXCEPTION_FRAME_ERROR], 0x11", source)
        self.assertIn("mov rax, cr2", source)
        self.assertIn("REIST_X86_64_PAGING_NX_OK", source)
        self.assertIn("REIST_X86_64_EXCEPTION_FATAL vector=", source)
        self.assertIn("REQUIRED_MARKERS", runner)
        self.assertIn("REIST_X86_64_HIGHER_HALF_PAGING_OK", runner)
        self.assertIn("REIST_X86_64_PAGING_NX_OK", runner)
        self.assertIn("REIST_X86_64_PHYSICAL_MEMORY_OK", runner)
        self.assertIn("REIST_X86_64_ELF64_LOAD_OK", runner)
        self.assertIn("REIST_X86_64_USER_EXECUTION_OK", runner)
        self.assertIn("REIST_X86_64_PROCESS_SCHEDULER_OK", runner)
        self.assertIn("REIST_X86_64_EXCEPTION_RECOVERY_OK", runner)
        self.assertIn("positions != sorted(positions)", runner)

    def test_documentation_rejects_complete_system_claim(self):
        contract = self.read("docs/architecture/X86_64_BOOTSTRAP.md")
        self.assertIn("kein vollstaendiger REIST-Kernel", contract)
        self.assertIn("produktive i386-Build", contract)
        self.assertIn("REIST_X86_64_LONG_MODE_BOOT_OK", contract)
        self.assertIn("Higher-Half", contract)
        self.assertIn("R8.1d", contract)


if __name__ == "__main__":
    unittest.main()
