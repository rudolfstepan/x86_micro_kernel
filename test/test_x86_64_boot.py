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
