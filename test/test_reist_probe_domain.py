import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def function(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise AssertionError(f"unterminated function: {signature}")


class ReistProbeDomainContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.process_h = read("kernel/proc/process.h")
        cls.process = read("kernel/proc/process.c")
        cls.syscalls = read("kernel/syscall/syscall_table.c")
        cls.supervisor = read("kernel/init/supervisor.c")

    def test_profile_is_versioned_fixed_and_attached_before_publication(self):
        self.assertIn("PROCESS_DOMAIN_PROFILE_VERSION 1U", self.process_h)
        self.assertIn("PROCESS_DOMAIN_SYSCALL_LIMIT 56U", self.process_h)
        for field in ("version", "struct_size", "kind", "allowed_syscalls"):
            self.assertIn(field, self.process_h)
        self.assertRegex(
            self.process_h,
            r"process_domain_profile_t\s+domain_profile\s*;",
        )
        claim = function(self.process, "static int claim_process_slot(")
        self.assertLess(
            claim.index("initialize_domain_profile("),
            claim.index("process->is_running = true"),
        )

    def test_probe_is_default_deny_and_compatibility_is_explicit(self):
        initialize = function(self.process, "static bool initialize_domain_profile(")
        self.assertIn("PROCESS_DOMAIN_COMPATIBILITY", initialize)
        self.assertIn("PROCESS_DOMAIN_PROBE", initialize)
        self.assertIn("PROCESS_DOMAIN_SYSCALL_LIMIT", initialize)
        probe = initialize[initialize.index("probe_syscalls") :]
        for allowed in (
            "SYS_EXIT", "SYS_GETPID", "SYS_YIELD", "SYS_SLEEP_MS",
            "SYS_MONOTONIC_MS", "SYS_IPC_SEND", "SYS_IPC_RECEIVE",
            "SYS_IPC_CLOSE", "SYS_IPC_SEND_TIMEOUT",
            "SYS_IPC_RECEIVE_TIMEOUT",
        ):
            self.assertIn(allowed, probe)
        for denied in (
            "SYS_KILL", "SYS_SPAWN", "SYS_OPEN", "SYS_CREATE",
            "SYS_WRITE", "SYS_FILL_RECT", "SYS_DRAW_TEXT",
            "SYS_PROCESS_INFO", "SYS_IPC_DELEGATE",
        ):
            self.assertNotIn(denied, probe)

    def test_dispatch_authorizes_before_any_syscall_side_effect(self):
        handler = function(self.syscalls, "void syscall_handler(")
        authorization = handler.index("process_syscall_allowed(")
        self.assertLess(authorization, handler.index("switch (syscall_index)"))
        self.assertLess(authorization, handler.index("Invalid syscall index"))
        denied = handler[authorization:handler.index("// Validate syscall index")]
        self.assertIn("regs->eax = (uint32_t)-13", denied)
        self.assertIn("return;", denied)
        self.assertIn("(regs->cs & 3U) == 3U", handler)
        self.assertIn("authority_process == NULL", handler)

    def test_kill_is_parent_and_generation_scoped(self):
        terminate = function(self.process, "int process_terminate_authorized(")
        self.assertIn("target->parent_pid == requester->pid", terminate)
        self.assertIn(
            "target->parent_generation == requester->generation", terminate
        )
        kill = function(self.syscalls, "static int syscall_kill(")
        self.assertIn("process_terminate_authorized(caller, pid)", kill)
        self.assertNotIn("process_terminate(pid)", kill)

    def test_supervisor_uses_explicit_probe_profile(self):
        spawn = function(self.supervisor, "int supervisor_spawn_service(")
        self.assertIn("domain_kind != PROCESS_DOMAIN_PROBE", spawn)
        self.assertIn("process_spawn_supervised", spawn)
        process_spawn = function(self.process, "int process_spawn_supervised(")
        self.assertIn("domain_kind", process_spawn)
        self.assertIn("true, domain_kind", process_spawn)


if __name__ == "__main__":
    unittest.main()
