import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class UserspaceFileSyscallSourceTests(unittest.TestCase):
    def test_open_accepts_nonempty_copied_paths(self):
        source = (ROOT / "kernel/syscall/syscall_table.c").read_text(
            encoding="utf-8"
        )
        self.assertRegex(
            source,
            re.compile(
                r"copy_string_from_user\(path, sizeof\(path\), user_path\)\s*"
                r"<\s*0"
            ),
        )

    def test_read_validates_userspace_destination(self):
        source = (ROOT / "kernel/syscall/syscall_table.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("user_range_accessible", source)
        self.assertIn("copy_to_user", source)

    def test_relative_paths_use_the_process_working_directory(self):
        source = (ROOT / "kernel/proc/process.c").read_text(encoding="utf-8")
        self.assertIn("resolve_process_path", source)
        self.assertIn("process->working_directory", source)

    def test_process_exit_closes_open_descriptors(self):
        source = (ROOT / "kernel/sched/scheduler.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("process_close_all_files", source)

    def test_ctrl_c_terminates_blocked_userspace_input(self):
        source = (ROOT / "kernel/syscall/syscall_table.c").read_text(
            encoding="utf-8"
        )
        getchar_case = source[source.index("case SYS_TERMINAL_GETCHAR:") :]
        getchar_case = getchar_case[:getchar_case.index("break;")]
        self.assertIn("result == 0x03U", getchar_case)
        self.assertIn("task_exit();", getchar_case)


if __name__ == "__main__":
    unittest.main()
