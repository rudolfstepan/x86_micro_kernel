import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class ReistFatalContainmentTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.paging_h = (ROOT / "arch/x86/mm/paging.h").read_text()
        cls.paging_c = (ROOT / "arch/x86/mm/paging.c").read_text()
        cls.scheduler = (ROOT / "kernel/sched/scheduler.c").read_text()
        cls.gdt = (ROOT / "arch/x86/cpu/gdt.c").read_text()
        cls.idt = (ROOT / "arch/x86/cpu/idt.c").read_text()
        cls.isr = (ROOT / "arch/x86/cpu/isr.c").read_text()
        cls.tss = (ROOT / "arch/x86/cpu/tss.c").read_text()
        cls.fatal = (ROOT / "kernel/init/fatal.c").read_text()

    def test_task_stacks_are_mapped_between_two_absent_pages(self):
        self.assertIn("KERNEL_STACK_SLOT_SIZE  (4U * PAGE_SIZE)", self.paging_h)
        self.assertIn("address < KERNEL_STACK_ARENA_BASE + KERNEL_STACK_ARENA_SIZE", self.paging_c)
        self.assertIn("kernel_page_tables[address >> 22]", self.paging_c)
        self.assertIn("stack_base + (uint32_t)mapped * PAGE_SIZE", self.scheduler)
        self.assertIn("base - PAGE_SIZE", self.scheduler)
        self.assertIn("base + STACK_SIZE", self.scheduler)

    def test_guard_arena_is_reserved_before_allocator_initialization(self):
        kernel = (ROOT / "kernel/init/kernel.c").read_text()
        reserve = kernel.index("memory_reserve_region(KERNEL_STACK_ARENA_BASE")
        initialize = kernel.index("initialize_memory_system()")
        self.assertLess(reserve, initialize)

    def test_double_fault_uses_dedicated_task_gate_and_tss(self):
        self.assertIn("struct gdt_entry gdt[7]", self.gdt)
        self.assertIn("tss_get_double_fault_base()", self.gdt)
        self.assertIn("type_attr = 0x85", self.idt)
        self.assertIn("set_idt_task_gate(8, DOUBLE_FAULT_TSS_SELECTOR)", self.isr)
        self.assertIn("double_fault_stack[PAGE_SIZE]", self.tss)
        self.assertRegex(self.tss, r"(?:double_fault_tss\.|fault->)eip")
        self.assertRegex(self.tss, r"(?:double_fault_tss\.|fault->)cr3")
        self.assertIn("ap_double_fault_tss", self.tss)
        self.assertIn("ap_double_fault_stacks", self.tss)

    def test_emergency_path_is_bounded_and_dependency_minimal(self):
        self.assertIn("EMERGENCY_SERIAL_POLL_BUDGET", self.fatal)
        self.assertIn('"REIST_FATAL DOUBLE_FAULT RESET\\n"', self.fatal)
        self.assertIn("cpu_halt_forever()", self.fatal)
        for forbidden in ("printf(", "k_malloc(", "vfs_", "serial_write_char("):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, self.fatal)

    def test_crash_record_has_magic_version_reason_sequence_and_checksum(self):
        header = (ROOT / "include/kernel/fatal.h").read_text()
        for field in ("magic", "version", "reason", "sequence", "checksum"):
            self.assertRegex(header, rf"uint32_t\s+{field}\s*;")
        self.assertIn("record.checksum = crash_checksum(&record)", self.fatal)


if __name__ == "__main__":
    unittest.main()
