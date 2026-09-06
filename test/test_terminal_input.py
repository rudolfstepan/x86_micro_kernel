"""Execute the foreground mediator and real keyboard dequeue on a host."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from build_user_program import find_zig
sys.path.insert(0, str(ROOT / "test"))
from test_memory_r12 import function_block


class TerminalInputTests(unittest.TestCase):
    def test_foreground_lifecycle_and_pre_dequeue_admission(self):
        module = (ROOT / "kernel/proc/terminal_input.c").read_text()
        production = "\n".join(line for line in module.splitlines()
                               if not line.startswith("#include"))
        keyboard = (ROOT / "drivers/char/kb.c").read_text()
        production += "\nstatic char input_queue_pop_locked(int pid, uint32_t generation) " + function_block(
            keyboard, "static char input_queue_pop_locked(")
        for signature in ("char input_queue_pop(void)", "char getchar_nonblocking(void)",
                          "char getchar(void)"):
            name = signature.replace("getchar_nonblocking", "test_getchar_nonblocking").replace(
                "char getchar(", "char test_getchar(")
            production += "\n" + name + " " + function_block(keyboard, signature[:-5])
        process = (ROOT / "kernel/proc/process.c").read_text()
        production += "\nint process_terminal_input(Process *caller,const reist_terminal_input_request_t *request) " + function_block(
            process, "int process_terminal_input(")
        production += "\nbool process_begin_exit(Process *process,uint32_t generation) " + function_block(
            process, "bool process_begin_exit(")
        guest = (ROOT / "userspace/programs/guest_test.c").read_text()
        production += "\nstatic int test_unicode_raster(void) " + function_block(
            guest, "static int test_unicode_raster(")
        fixture = (ROOT / "test/test_terminal_input_host.c").read_text()
        with tempfile.TemporaryDirectory(prefix="reist-terminal-host-") as tmp:
            folder = Path(tmp)
            source = folder / "host.c"
            source.write_text(fixture.replace("/* PRODUCTION */", production))
            executable = folder / "host.exe"
            env = os.environ.copy()
            env["ZIG_GLOBAL_CACHE_DIR"] = str(ROOT / "build/codex-agent/terminal/zig-global")
            env["ZIG_LOCAL_CACHE_DIR"] = str(folder / "cache")
            result = subprocess.run([str(find_zig()), "cc", "-std=c11", "-O1",
                "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-UNDEBUG",
                "-I", str(ROOT), str(source), "-o", str(executable)],
                cwd=ROOT, env=env, capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(executable)], cwd=ROOT,
                capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("TERMINAL_INPUT_HOST_OK", result.stdout)

    def test_all_terminal_aliases_share_admission_and_fixed_wait_node(self):
        keyboard = (ROOT / "drivers/char/kb.c").read_text()
        syscall = (ROOT / "kernel/syscall/syscall_table.c").read_text()
        self.assertIn("while (getchar() != '\\n')", keyboard)
        read = function_block(syscall, "static int syscall_read(")
        self.assertLess(read.index("user_range_accessible"), read.index("getchar_nonblocking()"))
        self.assertIn("PROCESS_DESCRIPTOR_TERMINAL_INPUT", read)
        blocking = function_block(keyboard, "char getchar(")
        self.assertIn("wait_queue_block_until_spinlocked(", blocking)
        self.assertIn("&input_waiters, TASK_BLOCK_WAITING, deadline_ms", blocking)
        self.assertIn("input_queue_pop_locked(pid, generation)", blocking)
        terminate = function_block((ROOT / "kernel/proc/process.c").read_text(), "int process_terminate(")
        self.assertLess(terminate.index("terminal_input_process_cleanup"), terminate.index("process_list[i].terminating = true"))
        self.assertLess(terminate.index("terminal_input_process_cleanup"), terminate.index("scheduler_terminate_task"))


if __name__ == "__main__":
    unittest.main()
