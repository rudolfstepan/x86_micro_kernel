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

    def test_page_tables_and_serial_polling_are_fixed(self):
        source = self.read("arch/x86_64/boot/entry.asm")
        self.assertIn("mov ecx, (3 * 4096) / 4", source)
        self.assertEqual(source.count("resb 4096"), 3)
        self.assertIn("resb 16384", source)
        self.assertIn("SERIAL_TX_POLLS     equ 65536", source)
        self.assertEqual(source.count("mov ecx, SERIAL_TX_POLLS"), 2)
        linker = self.read("config/x86_64_bootstrap.ld")
        self.assertIn("_x86_64_bootstrap_end <= 0x00200000", linker)

    def test_success_marker_is_reachable_only_in_64_bit_section(self):
        source = self.read("arch/x86_64/boot/entry.asm")
        bits64 = source.index("BITS 64")
        entry = source.index("long_mode_entry:", bits64)
        state_checks = source.index("test eax, EFER_LMA_BIT", entry)
        success_load = source.index("mov esi, success_message", state_checks)
        self.assertLess(bits64, entry)
        self.assertLess(entry, state_checks)
        self.assertLess(state_checks, success_load)
        self.assertEqual(source.count("REIST_X86_64_LONG_MODE_BOOT_OK"), 1)

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

    def test_ud2_probe_requires_exact_frame_and_runner_requires_order(self):
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
        self.assertIn("REIST_X86_64_EXCEPTION_FATAL vector=", source)
        self.assertIn("REQUIRED_MARKERS", runner)
        self.assertIn("REIST_X86_64_EXCEPTION_RECOVERY_OK", runner)
        self.assertIn("positions != sorted(positions)", runner)

    def test_documentation_rejects_complete_system_claim(self):
        contract = self.read("docs/architecture/X86_64_BOOTSTRAP.md")
        self.assertIn("kein vollstaendiger REIST-Kernel", contract)
        self.assertIn("produktive i386-Build", contract)
        self.assertIn("REIST_X86_64_LONG_MODE_BOOT_OK", contract)


if __name__ == "__main__":
    unittest.main()
