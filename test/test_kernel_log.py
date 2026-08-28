"""Behavior and integration tests for the bounded in-memory kernel log."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class KernelLogTests(unittest.TestCase):
    def test_bounded_ring_host_behavior(self) -> None:
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory(prefix="reist-klog-") as temp:
            executable = Path(temp) / "kernel-log-test.exe"
            subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-I.", "test/test_kernel_log_host.c",
                 "kernel/init/kernel_log_buffer.c", "-o", str(executable)],
                cwd=ROOT, check=True, capture_output=True, text=True)
            subprocess.run([str(executable)], cwd=ROOT, check=True,
                           capture_output=True, text=True, timeout=5)

    def test_ring_is_fixed_and_console_capture_is_kernel_only(self) -> None:
        header = (ROOT / "include/kernel/kernel_log.h").read_text(
            encoding="utf-8")
        ring = (ROOT / "kernel/init/kernel_log_buffer.c").read_text(
            encoding="utf-8")
        stdio = (ROOT / "lib/libc/stdio.c").read_text(encoding="utf-8")
        self.assertIn("KERNEL_LOG_CAPACITY 32768U", header)
        self.assertIn("kernel_log_slot_t slots[KERNEL_LOG_CAPACITY]", ring)
        self.assertIn("__atomic_compare_exchange_n", ring)
        self.assertNotIn("malloc", ring)
        self.assertNotIn("vfs_", ring)
        putchar = stdio[stdio.index("void putchar(char c)"):
                        stdio.index("// Function to print", stdio.index(
                            "void putchar(char c)"))]
        self.assertIn("if (is_kernel_context())", putchar)
        self.assertIn("kernel_log_capture_char(c);", putchar)
        self.assertLess(putchar.index("kernel_log_capture_char(c);"),
                        putchar.index("display_putchar(c);"))

    def test_append_only_read_abi_and_paged_dmesg(self) -> None:
        common = (ROOT / "include/reist/abi/syscall.h").read_text(
            encoding="utf-8")
        syscall = (ROOT / "kernel/syscall/syscall_table.c").read_text(
            encoding="utf-8")
        sdk = (ROOT / "userspace/sdk/include/x86os.h").read_text(
            encoding="utf-8")
        dmesg = (ROOT / "userspace/programs/dmesg.c").read_text(
            encoding="utf-8")
        self.assertIn("X(KERNEL_LOG_READ, KERNEL_LOG_READ, 125U)", common)
        self.assertIn("syscall_kernel_log_read", syscall)
        self.assertIn("KERNEL_LOG_READ_MAX 256U", syscall)
        self.assertIn("x86os_kernel_log_read_t", sdk)
        self.assertIn("DMESG_PAGE_LINES 22U", dmesg)
        self.assertIn('"--no-pager"', dmesg)
        self.assertIn('"--More-- [Space/Enter/Q]"', dmesg)
        self.assertIn("X86OS_KERNEL_LOG_READ_FROM_OLDEST", dmesg)
        self.assertIn('"dmesg: kernel log read failed code="', dmesg)

    def test_compatibility_domain_authorizes_appended_log_reader(self) -> None:
        process_h = (ROOT / "kernel/proc/process.h").read_text(
            encoding="utf-8")
        process_c = (ROOT / "kernel/proc/process.c").read_text(
            encoding="utf-8")
        self.assertIn("PROCESS_DOMAIN_SYSCALL_LIMIT 126U", process_h)
        self.assertIn(
            "for (uint32_t index = 0; index < PROCESS_DOMAIN_SYSCALL_LIMIT; ++index)",
            process_c,
        )

    def test_dmesg_is_packaged_in_both_image_layouts(self) -> None:
        programs = (ROOT / "scripts/build_system_programs.py").read_text(
            encoding="utf-8")
        windows = (ROOT / "scripts/build-windows.ps1").read_text(
            encoding="utf-8")
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        self.assertIn('"DMESG.PRG": ROOT / "userspace/programs/dmesg.c"',
                      programs)
        self.assertIn("'sbin/dmesg.prg' = 'DMESG.PRG'", windows)
        self.assertIn("sbin/dmesg.prg=$(SYSTEM_PROGRAM_DIR)/DMESG.PRG",
                      makefile)


if __name__ == "__main__":
    unittest.main()
