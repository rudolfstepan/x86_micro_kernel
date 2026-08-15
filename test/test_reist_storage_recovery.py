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
        self.assertIn("media_identity_matches(resource)", service)
        self.assertIn("RESOURCE_REINTEGRATED_", service)
        self.assertIn("STORAGE_MEDIA_PROBE_MAX_MS", service)
        self.assertIn("canonical_model_prefix", service)
        self.assertIn("control.probe_cursor + count", service)

    def test_all_media_writes_report_uncertain_completion_fail_closed(self):
        safety = read("kernel/init/storage_safety.c")
        ata = read("drivers/block/ata.c")
        fdd = read("drivers/block/fdd.c")
        header = read("include/kernel/storage_safety.h")
        self.assertIn("storage_write_begin(uint32_t resource", header)
        self.assertIn("storage_write_end(bool durable_commit)", header)
        self.assertIn("storage_service_report_media_failure(resource, true)",
                      safety)
        self.assertIn("filesystem_fence_mutations();", safety)
        self.assertIn("storage_write_end(result)", ata)
        self.assertIn("storage_write_end(result)", fdd)

    def test_all_media_recovery_contract_is_documented_fail_closed(self):
        contract = read("docs/architecture/HIGH_ASSURANCE_CORE_CONTRACT.md")
        architecture = read("docs/architecture/REIST_ARCHITECTURE.md")
        roadmap = read("docs/development/OS_GAP_ANALYSIS_AND_ROADMAP.md")
        for medium in ("FDD", "SATA/NVMe", "USB-Massenspeicher", "Flash"):
            self.assertIn(medium, contract)
        self.assertIn("Ein Schreibvorgang wird niemals blind wiederholt", contract)
        self.assertIn("höchstens `ONLINE_RO`", contract)
        self.assertIn("S0.3c-6e", architecture)
        self.assertIn("S0.3c-6f", architecture)
        self.assertIn("- [x] S0.3c-6e", roadmap)
        self.assertIn("- [ ] S0.3c-6f", roadmap)

    def test_fdd_disconnect_is_reported_and_requalification_resets_controller(self):
        fdd = read("drivers/block/fdd.c")
        header = read("drivers/block/fdd.h")
        service = read("kernel/init/storage_service.c")
        read_path = fdd[fdd.index("bool fdc_read_sectors("):
                        fdd.index("bool fdc_read_sector(")]
        self.assertIn("storage_service_resource_available", read_path)
        self.assertIn("storage_service_report_io_failure", read_path)
        self.assertIn("attempted && !result", read_path)
        recovery = fdd[fdd.index("bool fdc_requalify_drive("):
                       fdd.index("// =============================================================", fdd.index("bool fdc_requalify_drive("))]
        self.assertLess(recovery.index("fdc_reset_controller_impl()"),
                        recovery.index("fdc_calibrate_drive_impl"))
        self.assertIn("fdc_read_sector_recovery", header)
        self.assertIn("fdc_requalify_drive", header)
        self.assertIn("fdc_requalify_drive(drive->fdd_drive_no)", service)
        self.assertIn("fdc_read_sector_recovery", service)

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
            RUNNER.REIST_STORAGE_REINTEGRATION_MARKER,
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

    def test_power_loss_gate_requires_post_recovery_storage_self_test(self):
        journal = read("scripts/test_journal_recovery.py")
        self.assertIn('"--persistent"', journal)
        self.assertIn('"--expect-reist-probe"', journal)
        self.assertIn('"--expect-storage-self-test"', journal)
        lines = [
            RUNNER.BOOT_MARKER,
            RUNNER.REIST_STORAGE_READY_MARKER,
            RUNNER.REIST_STORAGE_SELF_TEST_MARKER,
            RUNNER.TEST_MARKER,
            RUNNER.SHELL_PROMPT,
        ]
        self.assertIsNone(RUNNER.validate(
            "\n".join(lines) + "\n", expect_storage_self_test=True))
        del lines[2]
        self.assertIsNotNone(RUNNER.validate(
            "\n".join(lines) + "\n", expect_storage_self_test=True))

    def test_storage_control_redundant_copy_repair_is_serialized(self):
        service = read("kernel/init/storage_service.c")
        for function in ("control_read", "control_write"):
            start = service.index(f"static int {function}")
            end = service.index("\n}", start)
            body = service[start:end]
            self.assertLess(body.index("irq_save()"),
                            body.index("critical_object_"))
            self.assertGreater(body.index("irq_restore(flags)"),
                               body.index("critical_object_"))

    def test_supervisor_poll_cannot_race_explicit_service_start(self):
        service = read("kernel/init/storage_service.c")
        self.assertIn("static volatile bool service_starting", service)
        self.assertIn("static volatile bool service_started", service)
        poll = service[service.index("void storage_service_poll"):]
        self.assertIn("!service_started || service_starting", poll)
        start = service[service.index("bool storage_service_start"):
                        service.index("int storage_service_bind")]
        self.assertLess(start.index("service_starting = true"),
                        start.index("spawn_service"))
        self.assertLess(start.index("service_started = result"),
                        start.index("service_starting = false"))


if __name__ == "__main__":
    unittest.main()
