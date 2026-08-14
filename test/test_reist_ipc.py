"""Contracts for the bounded S0.3 IPC and capability foundation."""

from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def c_block(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise AssertionError(f"unterminated function: {signature}")


def integer_macro(source: str, name: str) -> int:
    match = re.search(
        rf"(?m)^#define\s+{re.escape(name)}\s+(\d+)(?:U|UL|ULL)?\b",
        source,
    )
    if match is None:
        raise AssertionError(f"missing integer macro {name}")
    return int(match.group(1))


class ReistIpcContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = read("include/kernel/ipc.h")
        cls.source = read("kernel/ipc/ipc.c")
        cls.process_h = read("kernel/proc/process.h")
        cls.process = read("kernel/proc/process.c")
        cls.scheduler = read("kernel/sched/scheduler.c")
        cls.syscall_h = read("lib/libc/stdlib.h")
        cls.syscalls = read("kernel/syscall/syscall_table.c")
        cls.sdk_h = read("userspace/sdk/include/x86os.h")
        cls.sdk = read("userspace/sdk/x86os.c")

    def test_fixed_limits_and_versioned_message_are_public(self) -> None:
        endpoints = integer_macro(self.header, "IPC_MAX_ENDPOINTS")
        capabilities = integer_macro(
            self.header, "IPC_MAX_CAPABILITIES_PER_PROCESS"
        )
        depth = integer_macro(self.header, "IPC_QUEUE_DEPTH")
        self.assertGreaterEqual(endpoints, capabilities)
        self.assertLessEqual(endpoints, 64)
        self.assertGreaterEqual(capabilities, 2)
        self.assertLessEqual(capabilities, 32)
        self.assertGreaterEqual(depth, 2)
        self.assertLessEqual(depth, 16)
        self.assertEqual(integer_macro(self.header, "IPC_MAX_MESSAGE_SIZE"), 128)
        self.assertIn("IPC_MESSAGE_VERSION", self.header)
        self.assertRegex(
            self.header,
            r"typedef\s+struct\s*\{[^}]*uint32_t\s+version\s*;"
            r"[^}]*uint32_t\s+struct_size\s*;"
            r"[^}]*uint32_t\s+length\s*;"
            r"[^}]*payload\s*\[\s*IPC_MAX_MESSAGE_SIZE\s*\]",
        )
        self.assertIn("X86OS_IPC_MAX_MESSAGE_SIZE 128U", self.sdk_h)
        self.assertIn("X86OS_IPC_MESSAGE_VERSION", self.sdk_h)

    def test_ipc_storage_is_static_and_allocator_free(self) -> None:
        self.assertRegex(
            self.source,
            r"static\s+ipc_endpoint_t\s+\w+\s*\[\s*IPC_MAX_ENDPOINTS\s*\]",
        )
        self.assertRegex(
            self.process_h,
            r"ipc_capabilit\w*\s+\w+\s*"
            r"\[\s*IPC_MAX_CAPABILITIES_PER_PROCESS\s*\]",
        )
        self.assertNotRegex(
            self.source,
            r"\b(?:k_malloc|k_free|malloc|free|realloc)\s*\(",
        )
        self.assertNotIn("#include \"mm/kmalloc.h\"", self.source)

    def test_handle_resolution_checks_slot_generation_owner_and_rights(self) -> None:
        for right in ("IPC_RIGHT_SEND", "IPC_RIGHT_RECEIVE", "IPC_RIGHT_CONTROL"):
            self.assertIn(right, self.header)
        self.assertIn("generation", self.source)
        self.assertRegex(
            self.source,
            r"(?:decode|handle).*generation|generation.*(?:decode|handle)",
        )
        resolver = c_block(self.source, "static int resolve_capability(")
        self.assertIn("generation", resolver)
        self.assertRegex(resolver, r"rights\s*&\s*required_rights")
        self.assertRegex(resolver, r"(?:pid|owner).*(?:pid|owner)")
        close = c_block(self.source, "int ipc_close(")
        self.assertIn("IPC_RIGHT_CONTROL", close)
        send = c_block(self.source, "int ipc_send_timeout(")
        receive = c_block(self.source, "int ipc_receive_timeout(")
        self.assertIn("IPC_RIGHT_SEND", send)
        self.assertIn("IPC_RIGHT_RECEIVE", receive)

    def test_queues_are_bounded_and_block_without_polling(self) -> None:
        self.assertRegex(
            self.source,
            r"ipc_message_t\s+\w+\s*\[\s*IPC_QUEUE_DEPTH\s*\]",
        )
        send = c_block(self.source, "int ipc_send_timeout(")
        receive = c_block(self.source, "int ipc_receive_timeout(")
        for operation in (send, receive):
            self.assertIn("wait_queue_block_until_locked(", operation)
            self.assertNotIn("scheduler_yield(", operation)
            self.assertNotRegex(operation, r"\b(?:hlt|pause)\b")
        self.assertIn("IPC_QUEUE_DEPTH", send)
        self.assertIn("IPC_MAX_MESSAGE_SIZE", send)
        self.assertIn("wait_queue_wake_one_locked(", send)
        self.assertIn("wait_queue_wake_one_locked(", receive)

    def test_delegation_is_explicit_attenuated_and_generation_scoped(self) -> None:
        delegate = c_block(self.source, "int ipc_delegate(")
        self.assertIn("rights == 0U", delegate)
        self.assertIn("IPC_RIGHT_CONTROL", delegate)
        self.assertRegex(delegate, r"rights\s*&\s*source_record->rights")
        self.assertIn("target->generation", self.source)
        self.assertNotIn("ipc_inherit(", self.source)
        self.assertNotIn("ipc_inherit(", self.process)
        process_delegate = c_block(self.process, "int process_ipc_delegate(")
        self.assertIn("scheduler_preempt_disable()", process_delegate)
        self.assertIn("scheduler_preempt_enable()", process_delegate)
        self.assertIn("process_list[index].generation", process_delegate)

    def test_exit_cleanup_is_generation_scoped_and_wakes_both_directions(self) -> None:
        cleanup = c_block(self.source, "void ipc_process_cleanup(")
        self.assertIn("generation", cleanup)
        self.assertGreaterEqual(cleanup.count("wait_queue_wake_all_locked("), 2)
        terminate = c_block(self.scheduler, "void scheduler_terminate_task(")
        exit_path = c_block(self.scheduler, "void task_exit_status(")
        for path in (terminate, exit_path):
            self.assertIn("ipc_process_cleanup(", path)
            self.assertRegex(
                path,
                r"ipc_process_cleanup\s*\([^;]*(?:generation|process_generation)",
            )

    def test_syscall_numbers_are_append_only_and_sdk_wrapped(self) -> None:
        calls = {
            "IPC_CREATE": 49,
            "IPC_SEND": 50,
            "IPC_RECEIVE": 51,
            "IPC_CLOSE": 52,
            "IPC_SEND_TIMEOUT": 53,
            "IPC_RECEIVE_TIMEOUT": 54,
            "IPC_DELEGATE": 55,
        }
        for name, number in calls.items():
            self.assertRegex(
                self.syscall_h,
                rf"(?m)^#define\s+SYS_{name}\s+{number}\b",
            )
            self.assertRegex(
                self.sdk_h,
                rf"\bX86OS_SYS_{name}\s*=\s*{number}\b",
            )
            self.assertIn(f"case SYS_{name}:", self.syscalls)
            self.assertIn(f"X86OS_SYS_{name}", self.sdk)
        for declaration in (
            "int x86os_ipc_create(",
            "int x86os_ipc_send(",
            "int x86os_ipc_receive(",
            "int x86os_ipc_close(",
            "int x86os_ipc_send_timeout(",
            "int x86os_ipc_receive_timeout(",
            "int x86os_ipc_delegate(",
        ):
            self.assertIn(declaration, self.sdk_h)

    def test_deadlines_are_finite_monotonic_and_nonblocking(self) -> None:
        self.assertIn("IPC_DEFAULT_TIMEOUT_MS", self.header)
        for signature in ("int ipc_send_timeout(", "int ipc_receive_timeout("):
            operation = c_block(self.source, signature)
            self.assertIn("pit_monotonic_ms()", operation)
            self.assertIn("UINT64_MAX", operation)
            self.assertIn("IPC_EAGAIN", operation)
            self.assertIn("IPC_ETIMEDOUT", operation)
            self.assertIn("wait_queue_block_until_locked(", operation)
            self.assertNotIn("scheduler_yield(", operation)

    def test_syscalls_validate_user_messages_before_blocking(self) -> None:
        send = c_block(self.syscalls, "static int syscall_ipc_send(")
        receive = c_block(self.syscalls, "static int syscall_ipc_receive(")
        self.assertIn("copy_from_user_space", send)
        self.assertLess(
            send.index("copy_from_user_space"),
            send.index("ipc_send(", send.index("{")),
        )
        self.assertIn("copy_to_user_space", receive)
        self.assertLess(
            receive.index("ipc_receive(", receive.index("{")),
            receive.index("copy_to_user_space"),
        )

    def test_metadata_is_redundant_and_corruption_fails_closed(self) -> None:
        self.assertIn("critical_object_t ipc_endpoint_integrity", self.source)
        self.assertIn("critical_object_t ipc_capability_integrity", self.source)
        self.assertIn("ipc_message_integrity", self.source)
        self.assertIn("IPC_MESSAGE_CHUNKS 3U", self.source)
        self.assertIn("quarantine_endpoint_locked", self.source)
        self.assertIn("IPC_EINTEGRITY", self.header)
        self.assertIn("ipc_fault_inject(", self.header)

    def test_real_guest_exercises_rights_blocking_stale_and_cleanup(self) -> None:
        guest = read("examples/userspace/guest_test.c")
        for contract in (
            "x86os_ipc_create(&handle)",
            "x86os_ipc_send(handle, &message)",
            "x86os_ipc_receive(handle, &message)",
            "x86os_ipc_close(handle)",
            "x86os_ipc_delegate(handle, pid",
            'spawn_ipc_child("IPC_ECHO", handle)',
            'spawn_ipc_child("IPC_WAIT_CLOSE", handle)',
            'spawn_ipc_child("IPC_EXIT", handle)',
            "process_state_for_pid(child) != X86OS_PROCESS_WAITING",
            "handle == stale",
            "TEST_STAGE IPC_OK",
        ):
            self.assertIn(contract, guest)

    def test_host_fifo_rights_generation_quota_and_cleanup(self) -> None:
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / (
                "ipc-test.exe" if os.name == "nt" else "ipc-test"
            )
            compile_result = subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-DREIST_HOST_TEST",
                    "-I",
                    str(ROOT),
                    str(ROOT / "kernel/ipc/ipc.c"),
                    str(ROOT / "kernel/init/critical_object.c"),
                    str(ROOT / "kernel/sched/wait_queue.c"),
                    str(ROOT / "test/test_ipc_host.c"),
                    "-o",
                    str(executable),
                ],
                cwd=ROOT,
                check=False,
                capture_output=True,
                text=True,
                timeout=30,
            )
            self.assertEqual(
                compile_result.returncode,
                0,
                msg=compile_result.stdout + compile_result.stderr,
            )
            result = subprocess.run(
                [str(executable)], cwd=ROOT, capture_output=True, timeout=10
            )
            self.assertEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
