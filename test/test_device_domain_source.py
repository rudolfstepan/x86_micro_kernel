import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class DeviceDomainTests(unittest.TestCase):
    def test_acpi_dmar_parser(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "acpi-iommu-test.exe"
            subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-DREIST_HOST_TEST", "-I", str(ROOT),
                 str(ROOT / "arch/x86/platform/acpi.c"),
                 str(ROOT / "test/test_acpi_iommu_host.c"),
                 "-o", str(executable)],
                check=True, capture_output=True, text=True, timeout=30,
            )
            result = subprocess.run([str(executable)], timeout=10)
            self.assertEqual(result.returncode, 0)

    def test_host_lifecycle(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "device-domain-test.exe"
            subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-DREIST_HOST_TEST", "-I", str(ROOT),
                 str(ROOT / "kernel/init/device_domain.c"),
                 str(ROOT / "test/test_device_domain_host.c"),
                 "-o", str(executable)],
                check=True, capture_output=True, text=True, timeout=30,
            )
            result = subprocess.run([str(executable)], timeout=10)
            self.assertEqual(result.returncode, 0)

    def test_contract_is_fixed_generation_scoped_and_device_agnostic(self):
        header = (ROOT / "include/kernel/device_domain.h").read_text(
            encoding="utf-8")
        source = (ROOT / "kernel/init/device_domain.c").read_text(
            encoding="utf-8")
        self.assertIn("DEVICE_DOMAIN_MAX_DEVICES 16U", header)
        self.assertIn("DEVICE_DOMAIN_MAX_GROUPS 8U", header)
        self.assertIn("DEVICE_DOMAIN_PROFILE_GROUP_ISOLATED", header)
        self.assertIn("DEVICE_DOMAIN_PROFILE_LEGACY_INTX_PIC", header)
        self.assertIn("platform_iommu_ready", source)
        self.assertIn("group->owner_generation", source)
        self.assertIn("DEVICE_DOMAIN_HANDLE_GENERATION_MAX", header)
        self.assertIn("device->generation == DEVICE_DOMAIN_HANDLE_GENERATION_MAX",
                      source)
        self.assertIn("DEVICE_DOMAIN_MAX_RESOURCES 128U", header)
        self.assertIn("device_domain_open_region", source)
        self.assertIn("publish_resource", source)
        self.assertIn("retire_device_resources", source)
        self.assertIn("platform_ops.set_bus_master(device->pci_location, false)",
                      source)
        self.assertIn("device_domain_recover_owner", source)
        self.assertIn("platform_ops.monotonic_ms() >= deadline_ms", source)
        self.assertIn("__sync_lock_test_and_set", source)
        self.assertIn("bool device_domain_bootstrap(void)", source)
        self.assertIn("device_domain_init(&ops, false)", source)
        self.assertIn("inventory.dmar_valid", source)
        self.assertNotIn("x86_acpi_iommu_inventory(&inventory)", source)
        self.assertIn("safe physical-memory ACPI snapshot", source)
        self.assertIn(".direct_assignment_ready = iommu_ready ? 1U : 0U",
                      source)
        self.assertIn("ipc_capability_validate_owner", source)
        self.assertIn("device_domain_irq_capture", source)
        self.assertIn("device_domain_poll", source)
        self.assertIn("hold_pic_line", source)
        self.assertIn("pic_mask_location", source)
        self.assertIn("pic_unmask_location", source)
        self.assertIn("pic_fallback_allowed", source)
        self.assertIn("if (!masked && pic_fallback_allowed)", source)
        self.assertIn("DEVICE_DOMAIN_IRQ_TIMEOUT_MS", header)
        self.assertIn("DEVICE_DOMAIN_IRQ_WINDOW_MS 100U", header)
        self.assertIn("DEVICE_DOMAIN_IRQ_WINDOW_LIMIT 128U", header)
        self.assertIn("irq_window_admit(device, now_ms)", source)
        self.assertIn("increment_saturating(&device->irq_storm_count)", source)
        admission = source.index("if (!irq_window_admit(device, now_ms)")
        notification = source.index("ipc_send_kernel_to_owner", admission)
        self.assertLess(admission, notification)
        self.assertIn("(void)fence_slot(device);", source[admission:notification])
        self.assertIn("dma_pool_storage", source)
        self.assertIn("DEVICE_DOMAIN_DMA_POOL_BYTES", header)
        self.assertIn("DEVICE_DOMAIN_DMA_LARGE_POOL_BYTES", header)
        self.assertIn("DEVICE_DOMAIN_PROFILE_LARGE_DMA_POOL", header)
        self.assertIn("DEVICE_DOMAIN_CONTROL_DMA_POOL_STATS = 18U", header)
        self.assertIn(
            "DEVICE_DOMAIN_CONTROL_DMA_RELOCATE_AND_SEAL = 19U", header)
        self.assertIn(
            "DEVICE_DOMAIN_CONTROL_DMA_VM_PAGE_MODE = 20U", header)
        self.assertIn(
            "DEVICE_DOMAIN_CONTROL_GR_FIRMWARE_UPLOAD = 21U", header)
        self.assertIn(
            "DEVICE_DOMAIN_CONTROL_GR_PREREQUISITES = 22U", header)
        self.assertIn("DEVICE_DOMAIN_CONTROL_GR_EXECUTE = 23U", header)
        self.assertIn(
            "DEVICE_DOMAIN_CONTROL_GR_CONTEXT_MEMORY = 24U", header)
        self.assertIn(
            "DEVICE_DOMAIN_CONTROL_GR_GOLDEN_CONTEXT = 25U", header)
        self.assertIn("device_domain_dma_pool_stats_t", header)
        self.assertIn("capacity_rejections", header)
        self.assertIn("device_domain_dma_pool_stats", source)
        self.assertIn("device_domain_dma_write", source)
        self.assertIn("device_domain_dma_read", source)
        self.assertIn("device_domain_install_dma_relocation_policy", source)
        self.assertIn("device_domain_dma_relocate_and_seal", source)
        self.assertIn("device_domain_install_dma_vm_page_mode_policy", source)
        self.assertIn("device_domain_dma_vm_page_mode", source)
        self.assertIn("restore_dma_vm_page_mode", source)
        self.assertIn("device_domain_install_gr_firmware_policy", source)
        self.assertIn("device_domain_gr_firmware_upload", source)
        self.assertIn("gr_firmware_crc32", source)
        self.assertIn("gr_firmware_scrub_complete", source)
        self.assertIn("gr_firmware_upload_image", source)
        self.assertIn("reset_gr_firmware_state", source)
        self.assertIn("device_domain_install_gr_prerequisite_policy", source)
        self.assertIn("device_domain_gr_prerequisites", source)
        self.assertIn("gr_execution_image_valid", source)
        self.assertIn("gr_build_prerequisite_plan", source)
        self.assertIn("clear_gr_prerequisite_state", source)
        self.assertIn("device_domain_gr_execute", source)
        self.assertIn("gr_initialize_ltc", source)
        self.assertIn("gr_execute_operations", source)
        self.assertIn("gr_execution_rollback", source)
        self.assertIn("device_domain_gr_context_memory", source)
        self.assertIn("gr_build_context_memory_plan", source)
        self.assertIn("device_domain_gr_golden_context", source)
        self.assertIn("gr_golden_execute_transaction", source)
        self.assertIn("gr_golden_install_vm", source)
        self.assertIn("gr_golden_clear_vm", source)
        self.assertIn("gr_golden_table_valid", source)
        self.assertIn("gr_execution_rollback(device, status)", source)
        self.assertIn("GK208_GR_TEMP_INSTANCE_BYTES 0x00001000U", source)
        self.assertIn("GK208_GR_TEMP_PGD_BYTES 0x00010000U", source)
        self.assertIn("GK208_GR_TEMP_PGT_BYTES 0x00040000U", source)
        self.assertIn("REIST_GK208_GR_CONTEXT_CRC32", source)
        self.assertIn("REIST_GK208_GR_ICMD_CRC32", source)
        self.assertIn("REIST_GK208_GR_MTHD_CRC32", source)
        self.assertIn("GK208_GR_GOLDEN_CB_RESERVED 0x00080000U", source)
        self.assertIn("GK208_GR_ATTRIB_TOTAL_MAX 0x00000B23U", source)
        self.assertIn("scheduler_sleep_ms(1U)", source)
        self.assertGreaterEqual(
            source.count("device->gr_prerequisite_active == 0U"), 2)
        self.assertIn("pool->sealed == 0U", source)
        self.assertIn("verified & ~selected->writable_mask", source)
        disable = source.index(
            "platform_ops.set_bus_master(device->pci_location, false)",
            source.index("static bool fence_slot"))
        firmware_reset = source.index(
            "reset_gr_firmware_state(device)", disable)
        restore = source.index("restore_dma_vm_page_mode(device)", disable)
        self.assertLess(disable, firmware_reset)
        self.assertLess(firmware_reset, restore)
        self.assertLess(disable, restore)
        self.assertIn("if (pool->sealed != 0U) return -16;", source)
        self.assertIn("current != 0U", source)
        self.assertIn("pool->sealed = 1U", source)
        self.assertIn(
            "device->profile.flags & DEVICE_DOMAIN_PROFILE_MEDIATED_DMA",
            source)
        self.assertIn(".capacity = pool->capacity", source)
        self.assertIn("length > pool->capacity - offset", source)
        self.assertIn("DEVICE_DOMAIN_MAX_REGION_RULES 32U", header)
        self.assertIn(
            "DEVICE_DOMAIN_MAX_REGION_BYTES (8U * 1024U * 1024U)", header)
        self.assertIn("device_domain_install_region_policy", source)
        self.assertIn("region_policy_aperture", source)
        self.assertIn("regions[region].length_low = aperture", source)
        self.assertIn("region.length_low = aperture", source)
        self.assertIn("device_domain_region_read", source)
        self.assertIn("device_domain_region_write", source)
        self.assertIn("device_domain_region_bind_dma", source)
        self.assertIn("DEVICE_DOMAIN_REGION_RULE_DMA_ADDRESS", header)
        self.assertIn("DEVICE_DOMAIN_REGION_RULE_DMA_DESCRIPTOR_ADDRESS",
                      header)
        self.assertIn("DEVICE_DOMAIN_DMA_DATA_OFFSET", header)
        self.assertIn("device_domain_dma_descriptor_set", source)
        self.assertIn("device_domain_deactivate", source)
        self.assertIn("device->state = DEVICE_DOMAIN_DMA_BOUND", source)
        self.assertIn("request->buffer_offset < DEVICE_DOMAIN_DMA_DATA_OFFSET",
                      source)
        self.assertIn("map_requested", source)
        self.assertIn("platform_ops.write_dma_address", source)
        self.assertNotIn("k_malloc", source)
        self.assertNotIn("HDA", source)
        self.assertNotIn("while (", source)

    def test_architecture_names_external_reference_models_and_limits(self):
        model = (ROOT / "docs/architecture/USERSPACE_DRIVER_MODEL.md").read_text(
            encoding="utf-8")
        resilience = (ROOT /
            "docs/architecture/RESILIENCE_AND_DEGRADATION_CONTRACT.md").read_text(
                encoding="utf-8")
        for term in ("VFIO", "IOMMUFD", "Fuchsia", "Isolationgruppe",
                     "Bus Mastering", "UNSUPPORTED"):
            self.assertIn(term, model)
        self.assertIn("Ring 3 allein begrenzt beliebiges DMA nicht", resilience)

    def test_pci_configuration_and_fencing_updates_are_verified(self):
        header = (ROOT / "drivers/bus/pci.h").read_text(encoding="utf-8")
        source = (ROOT / "drivers/bus/pci.c").read_text(encoding="utf-8")
        self.assertIn("PCI_COMMAND_INTERRUPT_DISABLE", header)
        self.assertIn("pci_set_bus_master_verified", header)
        self.assertIn("pci_set_intx_disabled_verified", header)
        self.assertIn("PCI_OWNER_DRIVER_DOMAIN", header)
        self.assertIn("pci_claim_for_driver_domain", header)
        self.assertIn("pci_update_command_verified", source)
        self.assertIn("pci_read_dword_locked", source)
        self.assertIn("uint32_t flags = irq_save();", source)
        self.assertIn("uint16_t readback", source)
        self.assertIn("irq_restore(flags);", source)
        self.assertIn("if (dev->owner != PCI_OWNER_UNBOUND) continue;", source)
        self.assertIn("if (result == 0) dev->owner = PCI_OWNER_KERNEL;", source)
        self.assertIn("pci_describe_bar", source)
        self.assertIn("pci_write_word_locked", source)
        self.assertIn("restored_command != original_command", source)
        self.assertIn("pci_function_reset_verified", source)
        self.assertIn("PCI_PCIE_DEVICE_CAP_FLR", header)
        self.assertIn("deadline_ms - now_ms < 100U", source)

    def test_driver_process_profile_is_default_deny_and_supervisor_only(self):
        header = (ROOT / "kernel/proc/process.h").read_text(encoding="utf-8")
        process = (ROOT / "kernel/proc/process.c").read_text(encoding="utf-8")
        supervisor = (ROOT / "kernel/init/supervisor.c").read_text(
            encoding="utf-8")
        self.assertIn("PROCESS_DOMAIN_DRIVER = 6", header)
        self.assertIn("if (kind == PROCESS_DOMAIN_DRIVER)", process)
        profile = process[process.index("if (kind == PROCESS_DOMAIN_DRIVER)"):]
        profile = profile[:profile.index("if (kind != PROCESS_DOMAIN_PROBE)")]
        for syscall in ("SYS_IPC_CREATE", "SYS_IPC_SEND_TIMEOUT",
                        "SYS_MONOTONIC_MS", "SYS_DEVICE_CONTROL",
                        "SYS_DISPLAY_CONTROL"):
            self.assertIn(syscall, profile)
        for syscall in ("SYS_OPEN", "SYS_WRITE", "SYS_NETWORK_CONTROL"):
            self.assertNotIn(syscall, profile)
        self.assertIn("DISPLAY_CONTROL_DRIVER_COMMAND", (
            ROOT / "kernel/syscall/syscall_table.c").read_text(
                encoding="utf-8"))
        self.assertIn("domain_kind != PROCESS_DOMAIN_DRIVER", supervisor)

    def test_task_exit_fences_devices_before_capability_cleanup(self):
        scheduler = (ROOT / "kernel/sched/scheduler.c").read_text(
            encoding="utf-8")
        self.assertGreaterEqual(
            scheduler.count("device_domain_process_cleanup("), 2)
        for block in scheduler.split("device_domain_process_cleanup(")[1:]:
            self.assertIn("ipc_process_cleanup(", block[:300])

    def test_device_control_is_appended_versioned_and_driver_only(self):
        libc = (ROOT / "lib/libc/stdlib.h").read_text(encoding="utf-8")
        syscall = (ROOT / "kernel/syscall/syscall_table.c").read_text(
            encoding="utf-8")
        sdk_header = (ROOT / "userspace/sdk/include/x86os.h").read_text(
            encoding="utf-8")
        sdk_source = (ROOT / "userspace/sdk/x86os.c").read_text(
            encoding="utf-8")
        self.assertIn("#define SYS_DEVICE_CONTROL 113", libc)
        self.assertIn("Syscall 113: Driver resources", syscall)
        self.assertIn("process->domain_profile.kind != PROCESS_DOMAIN_DRIVER",
                      syscall)
        self.assertIn("device_output_accessible", syscall)
        self.assertIn("X86OS_SYS_DEVICE_CONTROL = 113", sdk_header)
        self.assertIn("X86OS_DEVICE_ABI_VERSION 1U", sdk_header)
        for wrapper in ("x86os_device_open_region", "x86os_device_bind_irq",
                        "x86os_device_bind_dma", "x86os_device_activate",
                        "x86os_device_resource_status",
                        "x86os_device_irq_complete", "x86os_device_dma_info",
                        "x86os_device_dma_pool_stats",
                        "x86os_device_dma_relocate_and_seal",
                        "x86os_device_dma_vm_page_mode",
                        "x86os_device_gr_firmware_upload",
                        "x86os_device_gr_prerequisites",
                        "x86os_device_gr_execute",
                        "x86os_device_gr_context_memory",
                        "x86os_device_gr_golden_context",
                        "x86os_device_dma_write", "x86os_device_dma_read",
                        "x86os_device_region_read",
                        "x86os_device_region_write",
                        "x86os_device_region_bind_dma"):
            self.assertIn(wrapper, sdk_source)
        for command in ("DEVICE_DOMAIN_CONTROL_REGION_READ",
                        "DEVICE_DOMAIN_CONTROL_REGION_WRITE",
                        "DEVICE_DOMAIN_CONTROL_REGION_BIND_DMA",
                        "DEVICE_DOMAIN_CONTROL_DMA_POOL_STATS"):
            self.assertIn(command, syscall)
        self.assertIn(
            "DEVICE_DOMAIN_CONTROL_DMA_RELOCATE_AND_SEAL", syscall)
        self.assertIn("DEVICE_DOMAIN_CONTROL_DMA_VM_PAGE_MODE", syscall)
        self.assertIn("DEVICE_DOMAIN_CONTROL_GR_FIRMWARE_UPLOAD", syscall)
        self.assertIn("DEVICE_DOMAIN_CONTROL_GR_PREREQUISITES", syscall)
        self.assertIn("DEVICE_DOMAIN_CONTROL_GR_EXECUTE", syscall)
        self.assertIn("DEVICE_DOMAIN_CONTROL_GR_CONTEXT_MEMORY", syscall)
        self.assertIn("DEVICE_DOMAIN_CONTROL_GR_GOLDEN_CONTEXT", syscall)
        self.assertIn("x86os_device_driver_bootstrap", sdk_source)
        self.assertIn("x86os_device_driver_report", sdk_source)
        self.assertIn("supervisor_device_driver_output_allowed", syscall)

    def test_hda_publishes_dma_pool_diagnostic_through_supervisor(self):
        hda = (ROOT / "userspace/drivers/audio/hda_driver.c").read_text(
            encoding="utf-8")
        runner = (ROOT / "scripts/run_qemu_pci_audio.py").read_text(
            encoding="utf-8")
        self.assertIn("HDA_DMA_POOL_DIAGNOSTIC_TAG 0xD0000000U", hda)
        self.assertIn("X86OS_DEVICE_DRIVER_REPORT_DIAGNOSTIC", hda)
        self.assertNotIn('x86os_puts("REIST_DMA_POOL', hda)
        self.assertIn(
            "REIST_DRIVER DIAGNOSTIC name=hda-ring3 value=D0114000",
            runner)

    def test_supervisor_owns_driver_restart_and_self_test(self):
        supervisor = (ROOT / "kernel/init/supervisor.c").read_text(
            encoding="utf-8")
        for mechanism in (
                "supervisor_start_device_driver", "driver_fence_apply",
                "device_domain_recover_owner", "driver_spawn_next",
                "supervisor_device_driver_bootstrap",
                "supervisor_device_driver_report", "driver_monitor_processes",
                "PROCESS_DOMAIN_DRIVER"):
            self.assertIn(mechanism, supervisor)


if __name__ == "__main__":
    unittest.main()
