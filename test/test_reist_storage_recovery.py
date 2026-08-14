"""Contracts for bounded Ring-3 storage-service crash recovery."""

from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RUNNER_PATH = ROOT / "scripts" / "run_qemu_smoke.py"
SPEC = importlib.util.spec_from_file_location("run_qemu_smoke", RUNNER_PATH)
RUNNER = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(RUNNER)


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


class StorageRecoveryContracts(unittest.TestCase):
    def test_fault_hook_is_test_build_only_and_crashes_after_real_read(self):
        source = read("kernel/syscall/syscall_table.c")
        guard = source.index("#ifdef REIST_STORAGE_FAULT_INJECTION")
        read_position = source.rindex("ata_read_sector", 0, guard)
        crash = source.index("task_exit_status(201);", guard)
        copyout = source.index("copy_to_user_space", crash)
        self.assertLess(read_position, guard)
        self.assertLess(guard, crash)
        self.assertLess(crash, copyout)
        self.assertIn("static bool storage_read_fault_injected", source)

    def test_build_target_uses_isolated_image_and_explicit_expectation(self):
        makefile = read("Makefile")
        self.assertIn("test-smoke-storage-recovery:", makefile)
        self.assertIn("STORAGE_FAULT_INJECTION=1", makefile)
        self.assertIn("OUTPUT_DIR=build/storage-injection", makefile)
        self.assertIn("--expect-storage-recovery", makefile)
        windows = read("scripts/build-windows.ps1")
        runtime = read("scripts/test-reist-runtime.ps1")
        self.assertIn("[switch]$StorageFaultInjection", windows)
        self.assertIn("'STORAGE_FAULT_INJECTION=1'", windows)
        self.assertIn("'storage-recovery'", runtime)

    def test_guest_retries_only_once_after_stale_generation(self):
        guest = read("examples/userspace/guest_test.c")
        self.assertIn("attempt < 2U", guest)
        self.assertIn("collect == -22 && attempt == 0U", guest)
        self.assertIn("TEST_STAGE STORAGE_RESTART_OK", guest)

    def test_runner_requires_ordered_recovery_markers(self):
        lines = [
            RUNNER.BOOT_MARKER,
            RUNNER.REIST_STORAGE_CRASH_MARKER,
            RUNNER.REIST_STORAGE_FAILURE_MARKER,
            RUNNER.REIST_STORAGE_RESTARTED_MARKER,
            RUNNER.REIST_STORAGE_READY_MARKER,
            RUNNER.REIST_STORAGE_RECOVERY_MARKER,
            RUNNER.TEST_MARKER,
            RUNNER.SHELL_PROMPT,
        ]
        transcript = "\n".join(lines) + "\n"
        self.assertIsNone(RUNNER.validate(
            transcript, expect_storage_recovery=True))
        reversed_lines = list(lines)
        reversed_lines[2], reversed_lines[3] = (
            reversed_lines[3], reversed_lines[2])
        self.assertIsNotNone(RUNNER.validate(
            "\n".join(reversed_lines) + "\n",
            expect_storage_recovery=True))

    def test_io_failure_is_retried_then_quarantined_before_future_io(self):
        syscall = read("kernel/syscall/syscall_table.c")
        service = read("kernel/init/storage_service.c")
        self.assertIn("attempt < 2U && !read_ok", syscall)
        self.assertIn("storage_service_report_io_failure(resource)", syscall)
        available = syscall.index("storage_service_resource_available(resource)")
        hardware = syscall.index("ata_read_sector", available)
        self.assertLess(available, hardware)
        self.assertIn("critical_object_t protected_control", service)
        self.assertIn("quarantined_resources", service)
        self.assertIn("RESOURCE_QUARANTINED", service)

    def test_io_failure_hook_and_runtime_gate_are_isolated(self):
        makefile = read("Makefile")
        windows = read("scripts/build-windows.ps1")
        runtime = read("scripts/test-reist-runtime.ps1")
        self.assertIn("REIST_STORAGE_IO_FAULT_INJECTION", makefile)
        self.assertIn("test-smoke-storage-io-failure:", makefile)
        self.assertIn("[switch]$StorageIoFaultInjection", windows)
        self.assertIn("'storage-io-failure'", runtime)

    def test_runner_requires_ordered_io_quarantine_markers(self):
        lines = [
            RUNNER.BOOT_MARKER,
            RUNNER.REIST_STORAGE_IO_INJECTION_MARKER,
            RUNNER.REIST_STORAGE_QUARANTINE_MARKER,
            RUNNER.REIST_STORAGE_IO_RECOVERY_MARKER,
            RUNNER.TEST_MARKER,
            RUNNER.SHELL_PROMPT,
        ]
        transcript = "\n".join(lines) + "\n"
        self.assertIsNone(RUNNER.validate(
            transcript, expect_storage_io_failure=True))
        lines[1], lines[2] = lines[2], lines[1]
        self.assertIsNotNone(RUNNER.validate(
            "\n".join(lines) + "\n",
            expect_storage_io_failure=True))


if __name__ == "__main__":
    unittest.main()
