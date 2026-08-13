import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class ReistUserStackGuardTests(unittest.TestCase):
    def test_layout_has_two_explicit_non_present_guards(self):
        paging = (ROOT / "arch/x86/mm/paging.h").read_text(encoding="utf-8")
        process = (ROOT / "kernel/proc/process.c").read_text(encoding="utf-8")
        self.assertIn("#define USER_STACK_UPPER_GUARD (USER_TOP - PAGE_SIZE)", paging)
        self.assertIn("#define USER_STACK_LOWER_GUARD (USER_STACK_BOTTOM - PAGE_SIZE)", paging)
        self.assertIn("for (uint32_t address = USER_STACK_BOTTOM;", process)
        self.assertNotIn("map_page(page_directory, USER_STACK_LOWER_GUARD", process)
        self.assertNotIn("map_page(page_directory, USER_STACK_UPPER_GUARD", process)

    def test_guest_fault_targets_guard_and_is_reaped_as_page_fault(self):
        fault = (ROOT / "examples/userspace/fault_stack.c").read_text(encoding="utf-8")
        guest = (ROOT / "examples/userspace/guest_test.c").read_text(encoding="utf-8")
        programs = (ROOT / "scripts/build_system_programs.py").read_text(encoding="utf-8")
        self.assertIn("0xBFFF6000U", fault)
        self.assertIn('wait_for_expected("FAULTSTK.PRG", 142)', guest)
        self.assertIn('"FAULTSTK.PRG"', programs)


if __name__ == "__main__":
    unittest.main()
