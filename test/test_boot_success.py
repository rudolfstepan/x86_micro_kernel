import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class BootSuccessContractTests(unittest.TestCase):
    def read(self, relative: str) -> str:
        return (ROOT / relative).read_text(encoding="utf-8")

    def test_stage2_publishes_validated_handoff_and_handles_confirmed_b(self):
        stage2 = self.read("arch/x86/boot/bios/stage2_bios.asm")
        self.assertIn("BOOT_HEALTH_ADDRESS", stage2)
        self.assertIn("call publish_boot_health_handoff", stage2)
        self.assertLess(stage2.index("call rsa_pss_verify"),
                        stage2.index("call publish_boot_health_handoff"))
        self.assertLess(stage2.index("call parse_elf_header"),
                        stage2.index("call publish_boot_health_handoff"))
        self.assertIn("BOOT_CONTROL_ACTIVE_B", stage2)
        self.assertIn("BOOT_CONTROL_CONFIRMED_B_ROLLBACK_A", stage2)
        self.assertIn("mov byte [boot_control_selected + BOOT_CONTROL_ACTIVE], BOOT_CONTROL_SLOT_A", stage2)
        self.assertIn("call persist_boot_control", stage2)

    def test_kernel_handoff_is_read_only_generation_scoped_and_ready_gated(self):
        header = self.read("include/kernel/boot_health.h")
        source = self.read("kernel/init/boot_health.c")
        syscall = self.read("kernel/syscall/syscall_table.c")
        process = self.read("kernel/proc/process.c")
        kernel = self.read("kernel/init/kernel.c")
        self.assertIn("BOOT_HEALTH_HANDOFF_ADDRESS 0x00004E00U", header)
        self.assertIn("boot_health_capture", source)
        self.assertIn("boot_health_mark_system_ready", source)
        self.assertIn("BOOT_HEALTH_PARTITION_TYPE 0xDAU", source)
        self.assertIn("partition_type != BOOT_HEALTH_PARTITION_TYPE", source)
        self.assertIn("matches != 1U", source)
        self.assertNotRegex(source, r"block_device_(?:write|flush)")
        self.assertIn("storage_service_authorized", syscall)
        self.assertIn("SYS_BOOT_STATUS", syscall)
        self.assertIn("SYS_BOOT_STATUS", process)
        self.assertLess(kernel.index("printf(\"BOOT_OK\\n\")"),
                        kernel.index("boot_health_mark_system_ready()"))

    def test_public_abi_is_append_only_and_fixed_size(self):
        libc = self.read("lib/libc/stdlib.h")
        sdk_h = self.read("userspace/sdk/include/x86os.h")
        sdk_c = self.read("userspace/sdk/x86os.c")
        self.assertIn("#define SYS_BOOT_STATUS 117", libc)
        self.assertIn("X86OS_SYS_BOOT_STATUS = 117", sdk_h)
        self.assertIn("X86OS_BOOT_STATUS_VERSION 1U", sdk_h)
        self.assertIn("sizeof(boot_health_status_t) == 40U", self.read(
            "include/kernel/boot_health.h"))
        self.assertIn("_Static_assert(sizeof(x86os_boot_status_t) == 40U", sdk_c)
        self.assertIn("int x86os_boot_status(x86os_boot_status_t *status)", sdk_c)

    def test_ring3_confirmation_revalidates_and_commits_both_copies(self):
        service = self.read("userspace/programs/storage_service.c")
        transaction = service[service.index("static int boot_control_reconcile"):
                              service.index("int main(")]
        confirm = service[service.index("int boot_confirm_pending("):
                          service.index("int main(")]
        self.assertIn("x86os_boot_status", confirm)
        self.assertIn("boot_control_read_pair", confirm)
        self.assertIn("boot_control_reconcile", transaction)
        self.assertIn("BOOT_CONTROL_PRIMARY_LBA", transaction)
        self.assertIn("BOOT_CONTROL_SECONDARY_LBA", transaction)
        self.assertIn("status->sequence != selected.selected.sequence", confirm)
        self.assertIn("x86os_storage_block_flush", transaction)
        self.assertGreaterEqual(confirm.count("boot_control_write_copy("), 2)
        self.assertIn("BOOT_CONTROL_SLOT_B", confirm)
        self.assertIn("BOOT_CONTROL_SLOT_NONE", confirm)
        self.assertRegex(confirm, re.compile(r"successful_mask\s*\|=\s*0x02U"))
        self.assertIn("static uint8_t boot_control_sectors", service)
        self.assertIn("__attribute__((noinline))", confirm)
        self.assertNotIn("x86os_puts", confirm)

    def test_runtime_gate_is_bounded_and_persistent(self):
        runtime = self.read("scripts/test-reist-runtime.ps1")
        runner = self.read("scripts/run_qemu_boot_success.py")
        self.assertIn("'boot-success'", runtime)
        self.assertIn("--timeout", runner)
        self.assertNotIn('"*** USER PROCESS EXCEPTION ***"', runner)
        self.assertIn("resilience probe deliberately executes UD2", runner)
        self.assertIn("BOOT_CONTROL_ACTIVE_B", runner)
        self.assertIn("BOOT_CONTROL_CONFIRMED_B_ROLLBACK_A", runner)
        self.assertIn("validate_boot_image", runner)


if __name__ == "__main__":
    unittest.main()
