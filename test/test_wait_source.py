import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def extract_block(source: str, opening_brace: int) -> str:
    """Return a C block including its braces."""
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


class WaitWakeupSourceRegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.syscalls = (ROOT / "kernel/syscall/syscall_table.c").read_text(
            encoding="utf-8"
        )
        cls.process = (ROOT / "kernel/proc/process.c").read_text(
            encoding="utf-8"
        )
        cls.scheduler = (ROOT / "kernel/sched/scheduler.c").read_text(
            encoding="utf-8"
        )
        cls.scheduler_header = (
            ROOT / "kernel/sched/scheduler.h"
        ).read_text(encoding="utf-8")
        cls.wait_function = function_block(
            cls.syscalls, "static int syscall_wait("
        )

    def test_status_check_and_queue_registration_are_atomic(self):
        wait = self.wait_function
        irq_save = wait.index("uint32_t flags = process_table_lock_irqsave();")
        status_check = wait.index("process_wait_status_locked(")
        wait_registration = wait.index("wait_queue_block_until_spinlocked(")

        self.assertLess(irq_save, status_check)
        self.assertLess(status_check, wait_registration)

        # Returning error/completion branches restore IF correctly.  Remove
        # those branches before checking the still-running-child path, which
        # must reach queue insertion with the original lock held.
        critical_path = wait[status_check:wait_registration]
        for condition in (r"if\s*\(result\s*<\s*0\)",
                          r"if\s*\(result\s*>\s*0\)",
                          r"if\s*\(wait_queue\s*==\s*NULL\)"):
            match = re.search(condition + r"\s*\{", critical_path)
            self.assertIsNotNone(match)
            branch = extract_block(
                critical_path, critical_path.index("{", match.start())
            )
            critical_path = critical_path.replace(branch, "")
        self.assertNotIn("process_table_unlock_irqrestore(", critical_path)

    def test_wait_blocks_on_the_child_owned_queue(self):
        wait = self.wait_function
        self.assertRegex(
            wait,
            r"wait_queue_t\s*\*\s*wait_queue\s*=\s*NULL\s*;",
        )
        self.assertRegex(
            wait,
            r"process_wait_status_locked\s*\([^;]*&wait_queue\s*\)",
        )
        self.assertIn("wait_queue == NULL", wait)
        self.assertIn(
            "wait_queue_block_until_spinlocked( wait_queue, "
            "TASK_BLOCK_WAITING, UINT64_MAX, process_table_lock_ref(), "
            "flags)",
            re.sub(r"\s+", " ", wait),
        )

    def test_locked_status_helper_returns_the_exit_queue_without_relocking(self):
        status = function_block(
            self.process, "int process_wait_status_locked("
        )
        self.assertNotIn("irq_save(", status)
        self.assertIn("process_table_lock_is_owned()", status)
        self.assertIn("*wait_queue = &child->exit_waiters;", status)
        self.assertLess(
            status.index("*wait_queue = &child->exit_waiters;"),
            status.index("return 0;"),
        )

    def test_exit_and_kill_wake_every_waiter(self):
        exit_block = function_block(self.scheduler, "void task_exit_status(")
        terminate = function_block(
            self.scheduler, "void scheduler_terminate_task("
        )
        expected = "wait_queue_wake_all_task_locked("
        self.assertIn(expected, exit_block)
        self.assertIn("exit_waiters", exit_block)
        self.assertIn(expected, terminate)
        self.assertIn("exit_waiters", terminate)

    def test_tasks_use_one_intrusive_node_instead_of_pid_coupling(self):
        task_match = re.search(
            r"typedef\s+struct\s+task\s*\{(?P<body>.*?)\}\s*task_t\s*;",
            self.scheduler_header,
            flags=re.DOTALL,
        )
        self.assertIsNotNone(task_match)
        body = task_match.group("body")
        self.assertIn("wait_queue_node_t wait_node;", body)
        self.assertNotIn("wait_pid", body)


if __name__ == "__main__":
    unittest.main()
