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
        self.assertIn("arch/x86_64/cpu/timer_interrupt.asm", target)
        self.assertIn("arch/x86_64/mm/physical_memory.asm", target)
        self.assertIn("arch/x86_64/user/probe.asm", target)
        self.assertIn("arch/x86_64/exec/elf64_loader.asm", target)
        self.assertIn("arch/x86_64/proc/user_execution.asm", target)
        self.assertIn("arch/x86_64/proc/cooperative_scheduler.asm", target)
        self.assertIn("-f elf64", target)
        self.assertIn("-m elf_x86_64", target)
        self.assertNotIn("native-image", target)
        self.assertNotIn("$(KERNEL_OBJ)", target)

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
        self.assertIn("MANAGED_LIMIT       equ 0x08000000", source)
        self.assertIn("FRAME_SIZE          equ 4096", source)
        self.assertIn("FRAME_COUNT         equ 32768", source)
        self.assertIn("FRAME_BITMAP_BYTES  equ 4096", source)
        self.assertIn("DIRECT_MAP_BASE     equ 0xFFFF800000000000", source)
        self.assertIn("DIRECT_PT_COUNT     equ 64", source)
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
        self.assertIn("cmp r9d, FRAME_COUNT", source)
        self.assertIn("cmp ecx, r9d", source)
        self.assertGreaterEqual(source.count("call physical_frame_alloc64"), 4)
        self.assertGreaterEqual(source.count("call physical_frame_free64"), 5)
        self.assertIn("test rdi, FRAME_SIZE - 1", source)
        self.assertIn("REIST_X86_64_PHYSICAL_MEMORY_OK", source)
        self.assertIn("REIST_X86_64_PHYSICAL_MEMORY_OK", runner)

    def test_physical_allocator_proves_bounded_memory_above_64m(self):
        source = self.read("arch/x86_64/mm/physical_memory.asm")
        runner = self.read("scripts/run_qemu_x86_64_boot.py")
        self.assertIn("HIGH_MEMORY_BASE    equ 0x04000000", source)
        self.assertIn("HIGH_MEMORY_FRAME   equ HIGH_MEMORY_BASE / FRAME_SIZE", source)
        high = source.index("physical_frame_alloc_high_selftest64:")
        shared = source.index("physical_frame_alloc_from_index64:", high)
        bound = source.index("cmp r9d, FRAME_COUNT", shared)
        self.assertLess(high, shared)
        self.assertLess(shared, bound)
        self.assertIn("mov ecx, HIGH_MEMORY_FRAME", source[high:shared])
        self.assertIn("mov r9d, FRAME_COUNT", source[high:shared])
        low = source.index("physical_frame_alloc64:")
        self.assertIn("mov r9d, FRAME_COUNT", source[low:high])
        self.assertIn("call physical_frame_alloc_high_selftest64", source)
        self.assertGreaterEqual(source.count("call physical_frame_free64"), 7)
        self.assertIn("REIST_X86_64_PHYSICAL_MEMORY_128M_OK", source)
        self.assertIn("REIST_X86_64_PHYSICAL_MEMORY_128M_OK", runner)

    def test_high_frame_consumers_share_managed_limit_and_cleanup(self):
        physical = self.read("arch/x86_64/mm/physical_memory.asm")
        execution = self.read("arch/x86_64/proc/user_execution.asm")
        scheduler = self.read("arch/x86_64/proc/cooperative_scheduler.asm")
        entry = self.read("arch/x86_64/boot/entry.asm")
        self.assertIn("mov ecx, dword [rel allocation_test_floor]", physical)
        self.assertIn("mov r9d, FRAME_COUNT", physical)
        self.assertIn("mov dword [rel allocation_test_floor], HIGH_MEMORY_FRAME", physical)
        self.assertIn("MANAGED_LIMIT             equ 0x08000000", execution)
        self.assertIn("MANAGED_LIMIT              equ 0x08000000", scheduler)
        high = entry.index("call physical_frame_test_high_window64")
        load = entry.index("call x86_64_elf64_loader_selftest64", high)
        process = entry.index("call x86_64_user_execution_selftest64", load)
        marker = entry.index("high_frame_consumers_message", process)
        self.assertLess(high, load)
        self.assertLess(load, process)
        self.assertLess(process, marker)
        self.assertIn("call physical_frame_test_window_clear64", execution)
        self.assertIn("REIST_X86_64_HIGH_FRAME_CONSUMERS_OK", entry)
        self.assertIn("REIST_X86_64_HIGH_FRAME_CONSUMERS_OK", self.read("scripts/run_qemu_x86_64_boot.py"))

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

    def test_ring3_shell_is_independently_linked_fixed_and_bounded(self):
        shell = self.read("arch/x86_64/user/shell.c")
        linker = self.read("config/x86_64_user_shell.ld")
        makefile = self.read("Makefile")
        for syscall in (
            "REIST_SYS_EXIT 9ULL", "REIST_SYS_READ 15ULL",
            "REIST_SYS_WRITE 20ULL", "REIST_SYS_YIELD 40ULL",
        ):
            self.assertIn(syscall, shell)
        self.assertIn("SHELL_COMMAND_CAPACITY 16U", shell)
        self.assertIn("SHELL_POLL_LIMIT 67108864U", shell)
        self.assertIn("while (polls < SHELL_POLL_LIMIT)", shell)
        self.assertNotRegex(shell, re.compile(r"while\s*\(\s*1\s*\)"))
        for command in ('"HELP"', '"INFO"', '"EXIT"'):
            self.assertIn(command, shell)
        self.assertIn("OUTPUT_FORMAT(elf64-x86-64)", linker)
        self.assertIn("text PT_LOAD FILEHDR PHDRS FLAGS(5)", linker)
        self.assertNotIn("data PT_LOAD", linker)
        self.assertIn("ASSERT(SIZEOF(.data) == 0", linker)
        self.assertIn("ASSERT(SIZEOF(.bss) == 0", linker)
        self.assertIn("ASSERT(_shell_end <= 0x00401000", linker)
        self.assertIn("shell_u8 command[SHELL_COMMAND_CAPACITY]", shell)
        self.assertIn("X86_64_USER_CFLAGS", makefile)
        self.assertIn("arch/x86_64/user/shell.c", makefile)
        self.assertIn("config/x86_64_user_shell.ld", makefile)
        self.assertIn("USER_SHELL_PATH", makefile)

    def test_elf64_shell_selection_is_inactive_only_and_cleaned(self):
        loader = self.read("arch/x86_64/exec/elf64_loader.asm")
        execution = self.read("arch/x86_64/proc/user_execution.asm")
        selector = loader.index("x86_64_elf64_select_image64:")
        publish = loader.index("mov byte [rel elf_image_selector], dil", selector)
        self.assertLess(loader.index("cmp byte [rel elf_load_active], 0", selector), publish)
        self.assertLess(loader.index("cmp edi, ELF_IMAGE_SHELL", selector), publish)
        self.assertIn("incbin USER_PROBE_PATH", loader)
        self.assertIn("incbin USER_SHELL_PATH", loader)
        cleanup = execution.index("user_execution_cleanup64:")
        self.assertIn("call x86_64_elf64_release64", execution[cleanup:])
        self.assertIn("call x86_64_elf64_select_image64", execution[cleanup:])
        self.assertIn("call physical_free_frame_count64", execution[cleanup:])

    def test_ring3_shell_syscalls_validate_before_effect_and_return_with_iretq(self):
        source = self.read("arch/x86_64/proc/user_execution.asm")
        self.assertIn("SHELL_IO_MAX              equ 64", source)
        self.assertIn("REIST_EAGAIN              equ -11", source)
        self.assertIn("REIST_ENOSYS              equ -38", source)
        dispatch = source.index("shell_syscall_dispatch64:")
        read = source.index("shell_handle_read64:", dispatch)
        write = source.index("shell_handle_write64:", read)
        read_validation = source.index("call validate_shell_buffer64", read, write)
        read_effect = source.index("mov byte [rdi], al", read, write)
        self.assertLess(read_validation, read_effect)
        write_end = source.index("shell_handle_yield64:", write)
        write_validation = source.index("call validate_shell_buffer64", write, write_end)
        write_effect = source.index("call serial_putc64", write, write_end)
        self.assertLess(write_validation, write_effect)
        validator = source.index("validate_shell_buffer64:")
        validator_end = source.index("shell_return_to_user64:", validator)
        stack_validator = source[validator:validator_end]
        self.assertIn("cmp r8, USER_STACK_BASE", stack_validator)
        self.assertIn("cmp r9, USER_STACK_TOP", stack_validator)
        self.assertIn("cmp r12d, PF_W", stack_validator)
        self.assertIn("call verify_user_context64", stack_validator)
        return_path = source.index("shell_return_to_user64:")
        self.assertIn("iretq", source[return_path:source.index("user_syscall_rejected64:")])
        self.assertNotRegex(source, re.compile(r"^\s*sysretq?\s*$", re.MULTILINE))

    def test_ring3_shell_runtime_dialogue_is_fixed_ordered_and_bounded(self):
        entry = self.read("arch/x86_64/boot/entry.asm")
        runner = self.read("scripts/run_qemu_x86_64_boot.py")
        c_marker = entry.index("lea rsi, [rel c_core_handoff_message]")
        shell_call = entry.index("call x86_64_c_control_handoff64", c_marker)
        final_marker = entry.index("lea rsi, [rel ring3_shell_message]", shell_call)
        self.assertLess(c_marker, shell_call)
        self.assertLess(shell_call, final_marker)
        self.assertNotIn("call x86_64_user_shell64", entry)
        self.assertIn('SUCCESS = "REIST_X86_64_RING3_SHELL_OK"', runner)
        self.assertIn('"-serial", "stdio"', runner)
        self.assertIn('process.stdin.write(b"INFO\\n")', runner)
        self.assertIn('process.stdin.write(b"EXIT\\n")', runner)
        self.assertIn("queue.Queue", runner)
        self.assertIn("reader.join(timeout=1.0)", runner)
        self.assertIn("log.write_bytes(captured_bytes)", runner)
        for marker in (
            "REIST_X86_64_RING3_SHELL_READY",
            "REIST_X86_64_RING3_SHELL_INFO_OK",
            "REIST_X86_64_RING3_SHELL_EXIT_OK",
            "REIST_X86_64_SCHEDULED_SHELL_OK",
            "REIST_X86_64_RING3_SHELL_ERROR",
        ):
            self.assertIn(marker, runner)

    def test_ring3_shell_uses_generation_scoped_scheduler_slot(self):
        source = self.read("arch/x86_64/proc/cooperative_scheduler.asm")
        shell = source.index("x86_64_process_shell64:")
        dispatch = source.index("call scheduler_runqueue_dispatch64", shell)
        self.assertIn("SCHEDULER_MODE_SHELL", source)
        self.assertIn("TASK_SHELL_GENERATION", source)
        self.assertIn("call x86_64_elf64_select_image64", source[shell:dispatch])
        self.assertIn("call scheduler_build_task64", source[shell:dispatch])
        self.assertIn("mov qword [r12 + TASK_STATE], TASK_READY", source[shell:dispatch])
        self.assertIn("call scheduler_runqueue_enqueue64", source[shell:dispatch])
        self.assertIn("scheduler_handle_shell_read64:", source)
        self.assertIn("scheduler_handle_shell_write64:", source)
        self.assertIn("scheduler_shell_resume64:", source)
        self.assertIn("call scheduler_save_syscall_context64", source)
        self.assertIn("jmp scheduler_runqueue_dispatch64", source)
        read = source.index("scheduler_handle_shell_read64:")
        write = source.index("scheduler_handle_shell_write64:", read)
        self.assertLess(source.index("call scheduler_validate_shell_buffer64", read, write),
                        source.index("mov byte [rdi], al", read, write))
        write_end = source.index("scheduler_handle_shell_yield64:", write)
        self.assertLess(source.index("call scheduler_validate_shell_buffer64", write, write_end),
                        source.index("call serial_putc64", write, write_end))
        exit_handler = source.index("scheduler_handle_shell_exit64:")
        cleanup = source.index("call scheduler_cleanup_common64", exit_handler)
        marker = source.index("REIST_X86_64_SCHEDULED_SHELL_OK", cleanup)
        self.assertLess(exit_handler, cleanup)
        self.assertLess(cleanup, marker)

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
        self.assertIn("TASK_SLOT_CAPACITY         equ 4", source)
        self.assertIn("resb TASK_SLOT_CAPACITY * TASK_RECORD_SIZE", source)
        self.assertIn("scheduler_enable_user_breakpoint64:", source)
        self.assertIn("scheduler_disable_user_breakpoint64:", source)
        self.assertIn("mov byte [rax + (3 * 16) + 5], 0xEE", source)
        self.assertEqual(source.count("resb PAGE_SIZE"), 16)
        self.assertIn("cmp edi, TASK_COUNT", source)
        self.assertIn("cmp ebx, TASK_SLOT_CAPACITY", source)
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
        self.assertIn('"-m", "128M"', runner)
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
        self.assertEqual(emitted_plain, set(range(33)) - error_vectors)
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
        self.assertIn("REIST_X86_64_TIMER_IRQ_OK", runner)
        self.assertIn("REIST_X86_64_EXCEPTION_RECOVERY_OK", runner)
        self.assertIn("positions != sorted(positions)", runner)

    def test_maskable_timer_irq_is_fixed_bounded_and_restored(self):
        exception = self.read("arch/x86_64/cpu/exceptions.asm")
        timer = self.read("arch/x86_64/cpu/timer_interrupt.asm")
        runner = self.read("scripts/run_qemu_x86_64_boot.py")
        self.assertIn("IDT_VECTOR_COUNT       equ 33", exception)
        self.assertIn("EXCEPTION_NOERR 32", exception)
        self.assertIn("call x86_64_timer_interrupt64", exception)
        self.assertIn("resb IDT_VECTOR_COUNT * IDT_GATE_SIZE", exception)
        self.assertIn("PIC_MASTER_VECTOR         equ 0x20", timer)
        self.assertIn("PIC_SLAVE_VECTOR          equ 0x28", timer)
        self.assertIn("PIC_MASTER_IRQ0_ONLY      equ 0xFE", timer)
        self.assertIn("PIT_INPUT_HZ              equ 1193182", timer)
        self.assertIn("PIT_TARGET_HZ             equ 100", timer)
        self.assertIn("PIT_DIVISOR               equ 11932", timer)
        self.assertIn("TIMER_EXPECTED_TICKS      equ 3", timer)
        self.assertIn("TSC_DEADLINE_CYCLES       equ 3000000000", timer)
        self.assertIn("rdtsc", timer)
        self.assertIn("cmp rax, qword [rel timer_deadline]", timer)
        self.assertNotIn("hlt", timer.lower())
        self.assertIn("cmp qword [rdi + EXCEPTION_FRAME_VECTOR], TIMER_VECTOR", timer)
        self.assertIn("cmp qword [rdi + EXCEPTION_FRAME_CS], KERNEL_CODE_SELECTOR", timer)
        self.assertIn("out PIC1_COMMAND, al", timer)
        self.assertIn("timer_saved_master_mask", timer)
        self.assertIn("timer_saved_slave_mask", timer)
        self.assertIn("REIST_X86_64_TIMER_IRQ_OK", runner)
        self.assertIn("REIST_X86_64_TIMER_IRQ_ERROR", runner)

    def test_cpl3_timer_preemption_is_generation_scoped_and_bounded(self):
        scheduler = self.read("arch/x86_64/proc/cooperative_scheduler.asm")
        timer = self.read("arch/x86_64/cpu/timer_interrupt.asm")
        probe = self.read("arch/x86_64/user/probe.asm")
        entry = self.read("arch/x86_64/boot/entry.asm")
        runner = self.read("scripts/run_qemu_x86_64_boot.py")
        self.assertIn("TASK_PREEMPTED             equ 5", scheduler)
        self.assertIn("SCHEDULER_MODE_PREEMPTION  equ 2", scheduler)
        self.assertIn("RFLAGS_PREEMPT_FORBIDDEN   equ 0x00000000003F7D00", scheduler)
        self.assertIn("x86_64_process_preemption_selftest64:", scheduler)
        self.assertIn("x86_64_scheduler_timer_validate64:", scheduler)
        self.assertIn("x86_64_scheduler_timer_preempt64:", scheduler)
        validate = timer.index("call x86_64_scheduler_timer_validate64")
        eoi = timer.index("out PIC1_COMMAND, al", validate)
        mutate = timer.index("call x86_64_scheduler_timer_preempt64", eoi)
        self.assertLess(validate, eoi)
        self.assertLess(eoi, mutate)
        self.assertIn("x86_64_timer_preemption_arm64:", timer)
        self.assertIn("x86_64_timer_preemption_cancel64:", timer)
        self.assertIn("x86_64_timer_preemption_disarm64:", timer)
        force_cleanup = scheduler.index("scheduler_force_cleanup64:")
        self.assertIn(
            "call x86_64_timer_preemption_cancel64",
            scheduler[force_cleanup:],
        )
        self.assertIn("preempt_task_a:", probe)
        self.assertIn("preempt_task_b:", probe)
        self.assertIn(".bounded_cpu_loop:", probe)
        self.assertIn("rdtsc", probe)
        self.assertIn("mov edi, 102", probe)
        self.assertIn("call x86_64_process_preemption_selftest64", entry)
        self.assertIn("REIST_X86_64_TIMER_PREEMPTION_OK", runner)
        self.assertIn("REIST_X86_64_TIMER_PREEMPTION_ERROR", runner)

    def test_recurring_quantum_switch_saves_complete_bounded_context(self):
        scheduler = self.read("arch/x86_64/proc/cooperative_scheduler.asm")
        timer = self.read("arch/x86_64/cpu/timer_interrupt.asm")
        probe = self.read("arch/x86_64/user/probe.asm")
        entry = self.read("arch/x86_64/boot/entry.asm")
        runner = self.read("scripts/run_qemu_x86_64_boot.py")
        self.assertIn("SCHEDULER_MODE_QUANTUM     equ 3", scheduler)
        self.assertIn("QUANTUM_EXPECTED_TICKS    equ 4", timer)
        self.assertIn("x86_64_process_quantum_selftest64:", scheduler)
        self.assertIn("x86_64_scheduler_quantum_validate64:", scheduler)
        self.assertIn("x86_64_scheduler_quantum_switch64:", scheduler)
        self.assertIn("x86_64_scheduler_timer_abort64:", scheduler)
        self.assertIn("call x86_64_scheduler_timer_abort64", timer)
        quantum = timer.index(".quantum:")
        validate = timer.index("call x86_64_scheduler_quantum_validate64", quantum)
        eoi = timer.index("out PIC1_COMMAND, al", validate)
        switch = timer.index("call x86_64_scheduler_quantum_switch64", eoi)
        self.assertLess(validate, eoi)
        self.assertLess(eoi, switch)
        for register in (
            "RAX", "RBX", "RCX", "RDX", "RBP", "RSI", "RDI", "R8",
            "R9", "R10", "R11", "R12", "R13", "R14", "R15",
        ):
            self.assertIn(f"EXCEPTION_FRAME_{register}", scheduler)
            self.assertIn(f"TASK_{register}", scheduler)
        self.assertIn("scheduler_quantum_events:", scheduler)
        self.assertIn("quantum_task_a:", probe)
        self.assertIn("quantum_task_b:", probe)
        self.assertIn("mov edi, 103", probe)
        self.assertIn("call x86_64_process_quantum_selftest64", entry)
        self.assertIn("REIST_X86_64_QUANTUM_SWITCH_OK", runner)
        self.assertIn("REIST_X86_64_QUANTUM_SWITCH_ERROR", runner)

    def test_generation_scoped_runqueue_lifecycle_is_fixed_and_stale_safe(self):
        scheduler = self.read("arch/x86_64/proc/cooperative_scheduler.asm")
        probe = self.read("arch/x86_64/user/probe.asm")
        entry = self.read("arch/x86_64/boot/entry.asm")
        runner = self.read("scripts/run_qemu_x86_64_boot.py")
        self.assertIn("TASK_SLOT_CAPACITY         equ 4", scheduler)
        self.assertIn("RUNQUEUE_CAPACITY          equ 4", scheduler)
        self.assertIn("SCHEDULER_MODE_RUNQUEUE    equ 4", scheduler)
        self.assertIn("x86_64_process_runqueue_selftest64:", scheduler)
        self.assertIn("scheduler_runqueue_enqueue64:", scheduler)
        self.assertIn("scheduler_runqueue_dequeue64:", scheduler)
        self.assertIn("scheduler_runqueue_dispatch64:", scheduler)
        enqueue = scheduler.index("scheduler_runqueue_enqueue64:")
        publish = scheduler.index("mov qword [rdx + rcx * 8], rax", enqueue)
        for validation in (
            "cmp qword [r12 + TASK_GENERATION], rsi",
            "cmp qword [r12 + TASK_STATE], TASK_READY",
            "cmp byte [rdx + rdi], 0",
            "cmp ecx, RUNQUEUE_CAPACITY",
        ):
            self.assertLess(scheduler.index(validation, enqueue), publish)
        self.assertIn("scheduler_verify_initial_runqueue64:", scheduler)
        self.assertIn("scheduler_runqueue_events:", scheduler)
        self.assertIn("runqueue_task_0:", probe)
        self.assertIn("runqueue_task_1:", probe)
        self.assertIn("runqueue_task_2:", probe)
        self.assertIn("runqueue_task_3:", probe)
        self.assertIn("mov edi, 110", probe)
        self.assertIn("mov edi, 111", probe)
        self.assertIn("mov edi, 112", probe)
        self.assertIn("call x86_64_process_runqueue_selftest64", entry)
        self.assertIn("REIST_X86_64_RUNQUEUE_LIFECYCLE_OK", runner)
        self.assertIn("REIST_X86_64_RUNQUEUE_LIFECYCLE_ERROR", runner)

    def test_deadline_sleep_is_fixed_generation_scoped_and_bounded(self):
        scheduler = self.read("arch/x86_64/proc/cooperative_scheduler.asm")
        timer = self.read("arch/x86_64/cpu/timer_interrupt.asm")
        probe = self.read("arch/x86_64/user/probe.asm")
        entry = self.read("arch/x86_64/boot/entry.asm")
        runner = self.read("scripts/run_qemu_x86_64_boot.py")
        self.assertIn("TASK_BLOCKED               equ 6", scheduler)
        self.assertIn("SCHEDULER_MODE_SLEEP       equ 5", scheduler)
        self.assertIn("REIST_SYS_SLEEP_MS         equ 41", scheduler)
        self.assertIn("REIST_SYS_MONOTONIC_MS     equ 42", scheduler)
        self.assertIn("scheduler_deadline_entries:", scheduler)
        self.assertIn("resb TASK_SLOT_CAPACITY * 16", scheduler)
        self.assertIn("scheduler_deadline_insert64:", scheduler)
        insert = scheduler.index("scheduler_deadline_insert64:")
        publish = scheduler.index("mov qword [r10], rdx", insert)
        for validation in (
            "test rdx, rdx",
            "cmp rdx, DEADLINE_TICK_LIMIT",
            "cmp qword [r12 + TASK_GENERATION], rsi",
            "cmp qword [r12 + TASK_STATE], TASK_RUNNING",
            "cmp byte [r8 + rdi], 0",
            "cmp ecx, TASK_SLOT_CAPACITY",
        ):
            self.assertLess(scheduler.index(validation, insert), publish)
        self.assertIn("x86_64_scheduler_deadline_tick64:", scheduler)
        self.assertIn("cmp ebx, TASK_SLOT_CAPACITY", scheduler)
        self.assertIn("scheduler_sleep_events:", scheduler)
        self.assertIn("dq 30, 10, 20, 0", scheduler)
        self.assertIn("SLEEP_MAX_TICKS           equ 8", timer)
        self.assertIn("x86_64_timer_sleep_arm64:", timer)
        self.assertIn("x86_64_timer_sleep_disarm64:", timer)
        self.assertIn("sleep_task_0:", probe)
        self.assertIn("sleep_task_1:", probe)
        self.assertIn("sleep_task_2:", probe)
        self.assertIn("sleep_task_3:", probe)
        for status in range(120, 124):
            self.assertIn(f"mov edi, {status}", probe)
        self.assertIn("call x86_64_process_deadline_sleep_selftest64", entry)
        self.assertIn("REIST_X86_64_DEADLINE_SLEEP_OK", runner)
        self.assertIn("REIST_X86_64_DEADLINE_SLEEP_ERROR", runner)

    def test_spawn_wait_lifecycle_is_dynamic_bounded_and_generation_safe(self):
        scheduler = self.read("arch/x86_64/proc/cooperative_scheduler.asm")
        probe = self.read("arch/x86_64/user/probe.asm")
        entry = self.read("arch/x86_64/boot/entry.asm")
        runner = self.read("scripts/run_qemu_x86_64_boot.py")
        self.assertIn("SCHEDULER_MODE_DYNAMIC     equ 6", scheduler)
        self.assertIn("REIST_SYS_GETPID           equ 22", scheduler)
        self.assertIn("REIST_SYS_SPAWN            equ 23", scheduler)
        self.assertIn("REIST_SYS_WAIT             equ 24", scheduler)
        self.assertIn("TASK_DYNAMIC_CHILD_GEN1    equ 31", scheduler)
        self.assertIn("TASK_DYNAMIC_CHILD_GEN2    equ 32", scheduler)
        self.assertIn("x86_64_process_spawn_wait_selftest64:", scheduler)
        self.assertIn("scheduler_validate_child_path64:", scheduler)
        self.assertIn("CHILD_PATH_CAPACITY        equ 16", scheduler)
        spawn = scheduler.index("scheduler_handle_spawn64:")
        validate = scheduler.index("call scheduler_validate_child_path64", spawn)
        build = scheduler.index("call scheduler_build_task64", spawn)
        publish = scheduler.index(
            "mov byte [rel scheduler_dynamic_child_active], 1", spawn
        )
        self.assertLess(validate, build)
        self.assertLess(build, publish)
        wait = scheduler.index("scheduler_handle_wait64:")
        translate = scheduler.index(
            "call scheduler_translate_status_pointer64", wait
        )
        block = scheduler.index("mov qword [r12 + TASK_STATE], TASK_WAITING", wait)
        self.assertLess(translate, block)
        self.assertIn("scheduler_dynamic_wait_child_generation", scheduler)
        self.assertIn("scheduler_dynamic_events:", scheduler)
        self.assertIn("dynamic_parent:", probe)
        self.assertIn("dynamic_child:", probe)
        self.assertIn('db "/probe/child", 0', probe)
        self.assertIn("mov edi, 130", probe)
        self.assertIn("mov edi, 77", probe)
        self.assertIn("call x86_64_process_spawn_wait_selftest64", entry)
        self.assertIn("REIST_X86_64_SPAWN_WAIT_OK", runner)
        self.assertIn("REIST_X86_64_SPAWN_WAIT_ERROR", runner)

    def test_c_core_handoff_is_packed_versioned_and_fixed(self):
        header = self.read("arch/x86_64/kernel/bootstrap_core.h")
        self.assertIn("REIST_X86_64_HANDOFF_VERSION 1U", header)
        self.assertIn("REIST_X86_64_HANDOFF_SIZE 128U", header)
        self.assertIn("struct __attribute__((packed))", header)
        self.assertIn("reist_u64 kernel_cr3;", header)
        self.assertIn("reist_u64 kernel_pml4;", header)
        self.assertIn("reist_u64 direct_map_base;", header)
        self.assertIn("reist_u64 lifecycle_capabilities;", header)
        self.assertIn("reist_u8 reserved[16];", header)
        self.assertIn("_Static_assert(sizeof(", header)

    def test_c_core_validates_before_mutation_and_cleans_authority(self):
        source = self.read("arch/x86_64/kernel/bootstrap_core.c")
        validate = source.index("if (!validate_handoff(handoff))")
        data_mutation = source.index("x86_64_c_bss_state[0] =", validate)
        callback = source.index("x86_64_c_serial_write64", data_mutation)
        cleanup = source.index("clear_owned_state();", callback)
        zero_handoff = source.index("zero_bytes((volatile reist_u8 *)handoff", cleanup)
        self.assertLess(validate, data_mutation)
        self.assertLess(data_mutation, callback)
        self.assertLess(callback, cleanup)
        self.assertLess(cleanup, zero_handoff)
        self.assertIn("REIST_COPY_BOUND REIST_X86_64_HANDOFF_SIZE", source)
        self.assertIn("index < REIST_COPY_BOUND", source)
        self.assertIn("index < REIST_STATE_WORDS", source)
        self.assertIn("handoff->reserved[index]", source)
        self.assertNotRegex(source, re.compile(r"\b(?:memcpy|memset|malloc|free)\s*\("))

    def test_c_kernel_control_handoff_is_fixed_validated_and_cleared(self):
        header = self.read("arch/x86_64/kernel/bootstrap_core.h")
        source = self.read("arch/x86_64/kernel/bootstrap_core.c")
        self.assertIn("REIST_X86_64_CONTROL_VERSION 1U", header)
        self.assertIn("REIST_X86_64_CONTROL_SIZE 64U", header)
        self.assertIn("struct __attribute__((packed)) reist_x86_64_control_v1", header)
        self.assertIn("reist_u64 request_generation;", header)
        self.assertIn("reist_u64 service_id;", header)
        self.assertIn("reist_u8 reserved[16];", header)
        self.assertIn("sizeof(struct reist_x86_64_control_v1)", header)
        control = source.index("x86_64_c_control_entry(")
        validate = source.index("validate_control(control)", control)
        callback = source.index("x86_64_c_process_shell64", validate)
        cleanup = source.index("zero_bytes((volatile reist_u8 *)control", callback)
        marker = source.index("c_kernel_control_message", cleanup)
        self.assertLess(validate, callback)
        self.assertLess(callback, cleanup)
        self.assertLess(cleanup, marker)
        self.assertIn("REIST_CONTROL_COPY_BOUND REIST_X86_64_CONTROL_SIZE", source)
        self.assertIn("control->reserved[index]", source)

    def test_c_kernel_owns_scheduled_shell_boot_control(self):
        entry = self.read("arch/x86_64/boot/entry.asm")
        makefile = self.read("Makefile")
        runner = self.read("scripts/run_qemu_x86_64_boot.py")
        main_start = entry.index("x86_64_nx_resume:")
        handoff_function = entry.index("x86_64_c_core_handoff64:", main_start)
        main = entry[main_start:handoff_function]
        core_marker = main.index("lea rsi, [rel c_core_handoff_message]")
        control_call = main.index("call x86_64_c_control_handoff64", core_marker)
        final_marker = main.index("lea rsi, [rel ring3_shell_message]", control_call)
        self.assertLess(core_marker, control_call)
        self.assertLess(control_call, final_marker)
        self.assertNotIn("call x86_64_process_shell64", main)
        adapter = entry.index("x86_64_c_process_shell64:")
        adapter_end = entry.index("section .text", adapter)
        adapter_source = entry[adapter:adapter_end]
        self.assertIn("cmp rdi, C_CONTROL_GENERATION", adapter_source)
        self.assertIn("cmp byte [rel c_control_active], 0", adapter_source)
        self.assertIn("call x86_64_process_shell64", adapter_source)
        self.assertIn("push rbx", adapter_source)
        self.assertIn("push r15", adapter_source)
        self.assertIn("sub rsp, 8", adapter_source)
        self.assertIn("mov byte [rel c_control_active], 0", adapter_source)
        self.assertIn("--defsym=x86_64_c_process_shell64=0xFFFFFFFF80184100", makefile)
        self.assertIn("--defsym=x86_64_c_control_handoff=0xFFFFFFFF801880A0", makefile)
        self.assertIn("REIST_X86_64_C_KERNEL_CONTROL_OK", runner)

    def test_sysv_c_entry_uses_dedicated_aligned_stack_after_old_markers(self):
        source = self.read("arch/x86_64/boot/entry.asm")
        recovery = source.index("lea rsi, [rel exception_recovery_message]")
        handoff = source.index("call x86_64_c_core_handoff64", recovery)
        final = source.index("lea rsi, [rel c_core_handoff_message]", handoff)
        self.assertLess(recovery, handoff)
        self.assertLess(handoff, final)
        self.assertIn("lea rsp, [rel c_core_stack_top]", source)
        self.assertIn("and rsp, -16", source)
        self.assertIn("call x86_64_c_core_entry", source)
        self.assertIn("mov edi, x86_64_c_bss_state", source)
        self.assertIn("mov ecx, C_HANDOFF_QWORDS", source)
        self.assertIn("x86_64_c_serial_write64:", source)
        self.assertIn("cmp rsi, 64", source)
        self.assertIn("resb C_HANDOFF_SIZE", source)

    def test_c_core_build_rejects_hosted_runtime_and_wx(self):
        makefile = self.read("Makefile")
        script = self.read("scripts/build-x86_64-bootstrap.ps1")
        linker = self.read("config/x86_64_bootstrap.ld")
        target = makefile.split("x86_64-bootstrap:", 1)[1].split("\n\n", 1)[0]
        for flag in (
            "-target x86_64-freestanding-none", "-ffreestanding",
            "-fno-builtin", "-fno-stack-protector", "-mno-red-zone",
            "-fno-unwind-tables", "-fno-pic", "-mcmodel=kernel",
        ):
            self.assertIn(flag, makefile)
        self.assertIn("arch/x86_64/kernel/bootstrap_core.c", target)
        self.assertIn("$(X86_64_C_CORE_OBJ)", target)
        self.assertIn("OUTPUT_FORMAT(elf32-i386)", linker)
        self.assertIn("reist-x86_64-c-core.elf", makefile)
        self.assertIn("--section-start=.text=0xFFFFFFFF80185000", target)
        self.assertIn("--only-section=.text", target)
        self.assertIn("incbin C_CORE_TEXT_PATH", self.read("arch/x86_64/boot/entry.asm"))
        self.assertIn(".c_core_text 0x00185000", linker)
        self.assertIn("SIZEOF(.init_array) == 0", linker)
        self.assertIn("SIZEOF(.eh_frame) == 0", linker)
        self.assertIn("writable-executable PT_LOAD", script)
        self.assertIn("retains an undefined symbol", script)
        self.assertIn("retains relocation section", script)
        self.assertIn("__stack_chk", script)

    def test_c_core_runtime_requires_callback_cleanup_and_final_marker(self):
        entry = self.read("arch/x86_64/boot/entry.asm")
        runner = self.read("scripts/run_qemu_x86_64_boot.py")
        self.assertIn('"REIST_X86_64_C_CORE_HANDOFF_OK"', runner)
        self.assertIn('SUCCESS = "REIST_X86_64_RING3_SHELL_OK"', runner)
        self.assertIn("REIST_X86_64_C_CALLBACK_OK", runner)
        self.assertIn("REIST_X86_64_C_KERNEL_CONTROL_OK", runner)
        self.assertIn("REIST_X86_64_EXCEPTION_RECOVERY_OK", runner)
        self.assertIn("REIST_X86_64_C_CORE_HANDOFF_ERROR", runner)
        handoff_start = entry.index("x86_64_c_core_handoff64:")
        handoff_end = entry.index("verify_high_page64:", handoff_start)
        handoff = entry[handoff_start:handoff_end]
        data_zero = handoff.index(".verify_data_zero:")
        bss_zero = handoff.index(".verify_bss_zero:", data_zero)
        success_return = handoff.index("mov eax, 1", bss_zero)
        fail_cleanup = handoff.index("lea rdi, [rel x86_64_c_handoff]", success_return)
        self.assertLess(data_zero, bss_zero)
        self.assertLess(bss_zero, success_return)
        self.assertIn("lea rdi, [rel x86_64_c_data_state]", handoff[fail_cleanup:])
        self.assertIn("lea rdi, [rel x86_64_c_bss_state]", handoff[fail_cleanup:])

    def test_documentation_rejects_complete_system_claim(self):
        contract = self.read("docs/architecture/X86_64_BOOTSTRAP.md")
        self.assertIn("kein vollstaendiger REIST-Kernel", contract)
        self.assertIn("produktive i386-Build", contract)
        self.assertIn("REIST_X86_64_LONG_MODE_BOOT_OK", contract)
        self.assertIn("Higher-Half", contract)
        self.assertIn("R8.1d", contract)


if __name__ == "__main__":
    unittest.main()
