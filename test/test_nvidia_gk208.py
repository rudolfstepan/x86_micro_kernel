import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class NvidiaGk208BringupTests(unittest.TestCase):
    def test_fermi_twod_command_contract(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "nvidia-gk208-2d-test.exe"
            subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-I", str(ROOT),
                 str(ROOT / "userspace/video/lib/nvidia_gk208_2d.c"),
                 str(ROOT / "test/test_nvidia_gk208_2d_host.c"),
                 "-o", str(executable)],
                check=True, capture_output=True, text=True, timeout=30,
            )
            result = subprocess.run([str(executable)], timeout=10)
            self.assertEqual(result.returncode, 0)

        header = (ROOT /
                  "userspace/video/include/reist/nvidia_gk208_2d.h").read_text(
                      encoding="utf-8")
        source = (ROOT /
                  "userspace/video/lib/nvidia_gk208_2d.c").read_text(
                      encoding="utf-8")
        self.assertIn("REIST_NVIDIA_GK208_FERMI_TWOD_A 0x0000902DU", header)
        self.assertIn("REIST_NVIDIA_GK208_PUSHBUF_WORD_CAPACITY 64U", header)
        self.assertIn(
            "REIST_NVIDIA_GK208_SUBMISSION_WORD_CAPACITY 72U", header)
        self.assertIn("NV902D_SET_DST_FORMAT 0x0200U", source)
        self.assertIn("NV902D_RENDER_SOLID_PRIM_MODE 0x0580U", source)
        self.assertIn("NV902D_SET_PIXELS_FROM_MEMORY_DST_X0 0x08B0U", source)
        self.assertIn("NV906F_SEMAPHOREA 0x00000010U", source)
        self.assertIn(
            "NV906F_SEMAPHORED_RELEASE_SIZE_4BYTE (1U << 24U)", source)
        self.assertIn("reist_nvidia_gk208_prepare_submission", source)
        self.assertIn("reist_nvidia_gk208_validate_submission", source)
        self.assertIn("submission->word_count << 10U", source)
        self.assertIn("REIST_NVIDIA_GK208_DMA_POOL_BYTES", header)
        self.assertIn("REIST_NVIDIA_GK208_DMA_GPFIFO_OFFSET", header)
        self.assertIn("reist_nvidia_gk208_prepare_dma_staging", source)
        self.assertIn("reist_nvidia_gk208_validate_dma_staging", source)
        self.assertIn("REIST_NVIDIA_GK208_DMA_RAMFC_OFFSET", header)
        self.assertIn("REIST_NVIDIA_GK208_DMA_USERD_OFFSET", header)
        self.assertIn("REIST_NVIDIA_GK208_DMA_RUNLIST_OFFSET", header)
        self.assertIn("REIST_NVIDIA_GK208_CHANNEL_LIMIT 1024U", header)
        self.assertIn("reist_nvidia_gk208_prepare_channel_image", source)
        self.assertIn("reist_nvidia_gk208_validate_channel_image", source)
        self.assertIn("REIST_NVIDIA_GK208_DMA_PGD_OFFSET", header)
        self.assertIn("REIST_NVIDIA_GK208_DMA_PGT_OFFSET", header)
        self.assertIn("REIST_NVIDIA_GK208_VM_LIMIT", header)
        self.assertIn("REIST_NVIDIA_GK208_DEFAULT_FB_PAGE_SHIFT", header)
        self.assertIn("REIST_NVIDIA_GK208_SEAL_RELOCATION_COUNT 6U", header)
        self.assertIn("reist_nvidia_gk208_prepare_vm_plan", source)
        self.assertIn("reist_nvidia_gk208_validate_vm_plan", source)
        self.assertIn("NVIDIA_GK208_GPFIFO_LIMIT2 9U", source)
        self.assertIn("case 0x10U: return 0x0000FACEU", source)
        self.assertIn("case 0xF8U: return 0x10003080U", source)
        self.assertIn("case 0xFCU: return 0x10000010U", source)
        self.assertNotIn("malloc", source)

    def test_gr_firmware_contract_is_pinned_immutable_and_passive(self):
        header = (ROOT /
                  "userspace/video/include/reist/nvidia_gk208_2d.h").read_text(
                      encoding="utf-8")
        source = (ROOT /
                  "userspace/video/lib/nvidia_gk208_2d.c").read_text(
                      encoding="utf-8")
        data = (ROOT /
                "userspace/video/lib/nvidia_gk208_firmware_data.h").read_text(
                    encoding="utf-8")
        driver = (ROOT /
                  "userspace/drivers/video/nvidia_gk208.c").read_text(
                      encoding="utf-8")

        self.assertIn("SPDX-License-Identifier: MIT", data)
        self.assertIn(
            "45c13f3f9e3bb15fd89ff2864c6f627a3b4b4229", data)
        for image in ("gk208_grhub_data", "gk208_grhub_code",
                      "gk208_grgpc_data", "gk208_grgpc_code"):
            self.assertIn(f"static const uint32_t {image}[]", data)
            self.assertNotIn(f"static uint32_t {image}[]", data)
        for value in ("193U", "640U", "27U", "384U",
                      "0x599287F1U", "0x761F1915U",
                      "0xF7976F94U", "0xF70A347FU", "1244U"):
            self.assertIn(value, header)
        self.assertIn("reist_nvidia_gk208_gr_firmware_manifest", source)
        self.assertIn("reist_nvidia_gk208_gr_firmware_word", source)
        self.assertIn("reist_nvidia_gk208_gr_firmware_self_test", source)
        firmware = source[source.index("static int gr_firmware_image") :]
        self.assertNotIn("malloc", firmware)
        self.assertNotIn("fopen", firmware)
        self.assertNotIn("x86os_", firmware)
        self.assertNotIn("volatile", firmware)

        self.assertIn("gr_firmware_contract_self_test", driver)
        self.assertIn("NVIDIA_DIAGNOSTIC_GR_FIRMWARE", driver)
        self.assertLess(
            driver.index("gr_firmware_contract_self_test(driver)"),
            driver.index("open_dma_pool(driver)"))
        driver_contract = driver[
            driver.index("static int gr_firmware_contract_self_test") :
            driver.index("static int open_dma_pool")]
        self.assertNotIn("x86os_device_dma_", driver_contract)
        self.assertNotIn("x86os_device_region_", driver_contract)
        self.assertNotIn("x86os_device_bind_irq", driver_contract)

    def test_profile_is_exact_and_irqless(self):
        source = (ROOT / "kernel/init/video_device_profile.c").read_text(
            encoding="utf-8")
        self.assertIn("NVIDIA_VENDOR_ID 0x10DEU", source)
        self.assertIn("NVIDIA_GK208_DEVICE_ID 0x1280U", source)
        self.assertIn("device->class_code != VMWARE_DISPLAY_CLASS", source)
        self.assertIn("device->subclass_code != DISPLAY_VGA_SUBCLASS", source)
        self.assertIn("DEVICE_DOMAIN_PROFILE_MEDIATED_IO", source)
        self.assertIn(
            "backend == VIDEO_DEVICE_BACKEND_NVIDIA_GK208", source)
        self.assertIn("DEVICE_DOMAIN_PROFILE_MEDIATED_DMA", source)
        self.assertIn("DEVICE_DOMAIN_PROFILE_LARGE_DMA_POOL", source)
        self.assertIn("NVIDIA_GPC31_TPC_COUNT 0x005FA608U", source)
        self.assertIn("(NVIDIA_GPC31_TPC_COUNT + sizeof(uint32_t))", source)
        self.assertIn(
            ".readable_bytes = {NVIDIA_BAR0_READABLE_BYTES}", source)
        self.assertIn(".rule_count = 0U", source)
        self.assertIn("device_domain_install_region_policy", source)
        self.assertIn("install_nvidia_dma_relocation_policy", source)
        self.assertIn("device_domain_install_dma_relocation_policy", source)
        self.assertIn(".policy_count = 2U", source)
        self.assertIn("page_shifts[2] = {16U, 17U}", source)
        self.assertIn("NVIDIA_FB_PAGE_MODE_REGISTER 0x00100C80U", source)
        self.assertIn("NVIDIA_FB_PAGE_MODE_MASK 0x00000001U", source)
        self.assertIn("install_nvidia_dma_vm_page_mode_policy", source)
        self.assertIn("device_domain_install_dma_vm_page_mode_policy", source)
        self.assertIn("NVIDIA_GR_FIRMWARE_POLICY_ID 1U", source)
        self.assertIn("NVIDIA_FECS_BASE 0x00409000U", source)
        self.assertIn("NVIDIA_GPCCS_BASE 0x0041A000U", source)
        self.assertIn("install_nvidia_gr_firmware_policy", source)
        self.assertIn("device_domain_install_gr_firmware_policy", source)
        self.assertIn("install_nvidia_gr_prerequisite_policy", source)
        self.assertIn("device_domain_install_gr_prerequisite_policy", source)
        self.assertIn("VBE_RUNTIME_INFO_ADDRESS", source)
        self.assertIn("vbe.framebuffer_address - selected.base_low", source)
        self.assertIn(".fault_buffer_bytes = NVIDIA_GR_FAULT_BUFFER_BYTES",
                      source)
        for offset in ("0x00070000U", "0x00070400U",
                       "0x00071000U", "0x00071400U"):
            self.assertIn(offset, source)
        page_mode = source[source.index(
            "static int install_nvidia_dma_vm_page_mode_policy") :]
        page_mode = page_mode[:page_mode.index("static int register_profile")]
        self.assertIn(".policy_id = 16U", page_mode)
        self.assertIn(".value = NVIDIA_FB_PAGE_MODE_MASK", page_mode)
        self.assertIn(".policy_id = 17U", page_mode)
        self.assertIn(".value = 0U", page_mode)

    def test_gr_plan_tables_topology_and_start_contract_are_complete(self):
        generator = (ROOT /
            "scripts/generate_nvidia_gk208_gr_tables.py").read_text(
                encoding="utf-8")
        data = (ROOT /
            "userspace/video/lib/nvidia_gk208_gr_tables.h").read_text(
                encoding="utf-8")
        header = (ROOT /
            "userspace/video/include/reist/nvidia_gk208_2d.h").read_text(
                encoding="utf-8")
        library = (ROOT /
            "userspace/video/lib/nvidia_gk208_2d.c").read_text(
                encoding="utf-8")
        driver = (ROOT /
            "userspace/drivers/video/nvidia_gk208.c").read_text(
                encoding="utf-8")
        pinned = "45c13f3f9e3bb15fd89ff2864c6f627a3b4b4229"
        self.assertIn(pinned, generator)
        self.assertIn(pinned, data)
        self.assertIn("Source arrays are MIT licensed", data)
        self.assertIn("REIST_GK208_GR_MMIO_TUPLE_COUNT 115U", data)
        self.assertIn("REIST_GK208_GR_MMIO_PACK_COUNT 30U", data)
        self.assertIn("REIST_GK208_GR_MMIO_CRC32 0xDB583025U", data)
        self.assertIn("REIST_GK208_GR_CONTEXT_TUPLE_COUNT 199U", data)
        self.assertIn("REIST_GK208_GR_CONTEXT_PACK_COUNT 5U", data)
        self.assertIn("REIST_GK208_GR_CONTEXT_CRC32 0xB765ADF0U", data)
        self.assertEqual(data.count("gk208_gr_init_main_0"), 2)
        for pack in ("HUB", "GPC0", "GPC1", "TPC", "PPC"):
            self.assertIn(f"/* {pack} */", data)
        self.assertIn("MMIO_PACK = (", generator)
        self.assertIn("CONTEXT_PACKS = (", generator)
        self.assertIn("tuple(actual_mmio or ()) != MMIO_PACK", generator)
        self.assertIn("REIST_NVIDIA_GK208_BAR0_TOPOLOGY_BYTES 0x005FA60CU",
                      header)
        self.assertIn("REIST_NVIDIA_GK208_MAX_GPCS 32U", header)
        self.assertIn("reist_nvidia_gk208_gr_validate_topology", library)
        self.assertIn("reist_nvidia_gk208_gr_compile_context_plan", library)
        self.assertIn("transfers >= 32U", library)
        self.assertIn("((transfer_count - 1U) << 26U) | address", library)
        self.assertIn("hub_start_offset = 0x00409100U", library)
        self.assertIn("ready_mask = 0x80000000U", library)
        self.assertIn("ready_deadline_ms = 2000U", library)
        self.assertIn("NVIDIA_GR_TOPOLOGY 0x409604U", driver)
        self.assertIn("gr_plan_contract_self_test", driver)
        self.assertIn("NVIDIA_DIAGNOSTIC_GR_PLAN", driver)
        self.assertLess(driver.index("gr_plan_contract_self_test(driver)"),
                        driver.index("open_dma_pool(driver)"))
        self.assertNotIn("x86os_device_region_write", driver)
        plan = library[library.index(
            "int reist_nvidia_gk208_gr_plan_manifest") :]
        self.assertNotIn("x86os_", plan)
        self.assertNotIn("volatile", plan)

    def test_gr_execution_image_is_complete_bounded_and_hardware_inactive(self):
        header = (ROOT /
            "userspace/video/include/reist/nvidia_gk208_2d.h").read_text(
                encoding="utf-8")
        library = (ROOT /
            "userspace/video/lib/nvidia_gk208_2d.c").read_text(
                encoding="utf-8")
        driver = (ROOT /
            "userspace/drivers/video/nvidia_gk208.c").read_text(
                encoding="utf-8")
        for contract in (
                "REIST_NVIDIA_GK208_GR_EXECUTION_OP_CAPACITY 2048U",
                "REIST_NVIDIA_GK208_DMA_GR_EXECUTION_OFFSET 0x00072000U",
                "REIST_NVIDIA_GK208_GR_VRAM_BUFFER_BYTES 0x00020000U",
                "REIST_NVIDIA_GK208_GR_VRAM_BUFFER_ALIGNMENT 0x00020000U",
                "REIST_NVIDIA_GK208_GR_OP_VRAM_OFFSET32",
                "REIST_NVIDIA_GK208_GR_OP_CONTEXT_GROUP",
                "REIST_NVIDIA_GK208_GR_OP_WAIT_IDLE"):
            self.assertIn(contract, header)
        self.assertIn("reist_nvidia_gk208_gr_compile_execution_image",
                      library)
        self.assertIn("reist_nvidia_gk208_gr_validate_execution_image",
                      library)
        self.assertIn("gr_execution_operation_crc32", library)
        self.assertIn("gr_execution_topology_crc32", library)
        self.assertIn("gf100_gr_init()", library)
        self.assertIn("nvkm_ltc_init()", library)
        self.assertIn("0x004188B4U", library)
        self.assertIn("0x004188B8U", library)
        self.assertIn("0x0017EA44U", library)
        self.assertIn("0x00405824U", library)
        self.assertIn("gr_build_tile_map", library)
        self.assertIn("gr_execution_context", library)
        self.assertIn("NVIDIA_DIAGNOSTIC_GR_EXECUTION_IMAGE", driver)
        self.assertIn("gr_execution_image_dma_self_test", driver)
        self.assertLess(
            driver.index("gr_execution_image_dma_self_test(driver)"),
            driver.index("gpu_vm_relocate_and_seal(driver)"))
        image_stage = driver[driver.index(
            "static int gr_execution_image_dma_self_test") :]
        image_stage = image_stage[:image_stage.index(
            "static int channel_image_dma_self_test")]
        self.assertIn("dma_stage_and_verify", image_stage)
        self.assertNotIn("x86os_device_region_write", image_stage)
        self.assertNotIn("x86os_device_activate", image_stage)
        self.assertNotIn("x86os_device_bind_irq", image_stage)
        self.assertNotIn("x86os_device_gr_", image_stage)
        self.assertNotIn("x86os_device_dma_relocate_and_seal", image_stage)

    def test_kernel_only_admits_bar_geometry(self):
        source = (ROOT / "drivers/video/display_control.c").read_text(
            encoding="utf-8")
        start = source.index("static void prepare_nvidia_gk208")
        end = source.index("static pci_device_t *find_vmware", start)
        probe = source[start:end]
        self.assertIn("bar0.size_low < NVIDIA_REQUIRED_BAR_BYTES", probe)
        self.assertIn("bar0.size_low > NVIDIA_BAR_MAX_BYTES", probe)
        self.assertIn("NVIDIA_GK208_BAR_ADMITTED", probe)
        self.assertNotIn("map_mmio_region", probe)
        self.assertNotIn("volatile", probe)
        self.assertNotIn("NVIDIA_READ", source)
        self.assertNotIn("NVIDIA_PMC_BOOT_0", source)
        self.assertNotIn("pci_enable_device", probe)
        self.assertNotIn("pci_set_bus_master", probe)

    def test_engine_preflight_is_ring3_read_only_and_bounded(self):
        display = (ROOT / "drivers/video/display_control.c").read_text(
            encoding="utf-8")
        driver = (ROOT / "userspace/drivers/video/nvidia_gk208.c").read_text(
            encoding="utf-8")
        self.assertNotIn("nvidia_read_live_probe", display)
        self.assertIn("x86os_device_open_region", driver)
        self.assertIn("X86OS_DEVICE_REGION_DESCRIBE |", driver)
        self.assertIn("X86OS_DEVICE_REGION_ACCESS_READ", driver)
        self.assertIn("x86os_device_region_read", driver)
        self.assertIn("NVIDIA_PROBE_COHERENCE_ATTEMPTS 4U", driver)
        self.assertIn("attempt < NVIDIA_PROBE_COHERENCE_ATTEMPTS", driver)
        self.assertIn("NVIDIA_PREFLIGHT_DELAY_MS 1U", driver)
        self.assertIn("x86os_sleep_ms(NVIDIA_PREFLIGHT_DELAY_MS)", driver)
        self.assertIn("nvidia_gk208_timer_after", driver)
        self.assertNotIn("x86os_device_region_write", driver)
        self.assertNotIn("x86os_device_bind_irq", driver)
        self.assertNotIn("X86OS_DEVICE_REGION_MAP_", driver)
        self.assertNotIn("X86OS_DEVICE_REGION_ACCESS_WRITE", driver)

    def test_driver_never_advertises_unproven_acceleration(self):
        driver = (ROOT / "userspace/drivers/video/nvidia_gk208.c").read_text(
            encoding="utf-8")
        display = (ROOT / "drivers/video/display_control.c").read_text(
            encoding="utf-8")
        self.assertIn("response->capabilities = 0U", driver)
        self.assertIn("response->status = -95", driver)
        self.assertIn("request->capabilities = 0U", display)
        self.assertIn("x86os_device_open_region", driver)
        self.assertIn("x86os_device_bind_dma_direction", driver)
        self.assertIn("x86os_device_dma_write", driver)
        self.assertIn("x86os_device_dma_read", driver)
        self.assertIn("channel_image_dma_self_test", driver)
        self.assertIn("gpu_vm_plan_dma_self_test", driver)
        self.assertIn("gpu_vm_relocate_and_seal", driver)
        self.assertIn("x86os_device_dma_relocate_and_seal", driver)
        self.assertIn("gpu_vm_apply_page_mode", driver)
        self.assertIn("x86os_device_dma_vm_page_mode", driver)
        self.assertIn("gr_firmware_dma_stage_self_test", driver)
        self.assertIn("gpu_gr_firmware_upload", driver)
        self.assertIn("x86os_device_gr_firmware_upload", driver)
        self.assertIn("gpu_gr_prerequisites", driver)
        self.assertIn("x86os_device_gr_prerequisites", driver)
        self.assertLess(driver.index("gpu_vm_relocate_and_seal(driver)"),
                        driver.index("gpu_vm_apply_page_mode(driver)"))
        self.assertLess(driver.index("gr_firmware_dma_stage_self_test(driver)"),
                        driver.index("gpu_vm_relocate_and_seal(driver)"))
        self.assertLess(driver.index("gpu_vm_apply_page_mode(driver)"),
                        driver.index("gpu_gr_firmware_upload(driver)"))
        self.assertLess(driver.index("gpu_vm_relocate_and_seal(driver)"),
                        driver.index("gpu_gr_prerequisites(driver)"))
        self.assertLess(driver.index("gpu_gr_prerequisites(driver)"),
                        driver.index("gpu_vm_apply_page_mode(driver)"))
        self.assertIn("X86OS_DEVICE_DMA_TRANSFER_MAX", driver)
        self.assertNotIn("x86os_device_bind_irq", driver)
        self.assertNotIn("x86os_device_activate", driver)
        self.assertNotIn("x86os_device_region_bind_dma", driver)
        self.assertIn("reist_nvidia_gk208_command_self_test", driver)
        self.assertIn("reist_nvidia_gk208_submission_self_test", driver)
        self.assertIn("reist_nvidia_gk208_dma_staging_self_test", driver)
        self.assertIn("reist_nvidia_gk208_channel_image_self_test", driver)
        self.assertIn("reist_nvidia_gk208_vm_plan_self_test", driver)
        upload = driver[driver.index("static int gpu_gr_firmware_upload") :]
        upload = upload[:upload.index("static int activate")]
        self.assertNotIn("x86os_device_bind_irq", upload)
        self.assertNotIn("x86os_device_activate", upload)
        self.assertNotIn("x86os_device_region_write", upload)

    def test_driver_is_supervised_and_deadlines_are_bounded(self):
        driver = (ROOT / "userspace/drivers/video/nvidia_gk208.c").read_text(
            encoding="utf-8")
        kernel = (ROOT / "kernel/init/kernel.c").read_text(encoding="utf-8")
        supervisor = (ROOT / "kernel/init/supervisor.c").read_text(
            encoding="utf-8")
        self.assertIn("x86os_device_driver_bootstrap", driver)
        self.assertIn("x86os_ipc_receive_timeout", driver)
        self.assertIn("NVIDIA_IPC_TIMEOUT_MS 20U", driver)
        self.assertNotIn("while (1)", driver)
        self.assertIn('"nvidia-gk208-ring3"', kernel)
        self.assertIn('"/libexec/reist/nvidia.prg"', kernel)
        self.assertGreaterEqual(
            supervisor.count('"nvidia-gk208-ring3"'), 5)

    def test_canonical_driver_identity_fits_without_truncation(self):
        header = (ROOT / "include/kernel/supervisor.h").read_text(
            encoding="utf-8")
        supervisor = (ROOT / "kernel/init/supervisor.c").read_text(
            encoding="utf-8")
        host = (ROOT / "test/test_supervisor_host.c").read_text(
            encoding="utf-8")
        self.assertIn("#define SUPERVISOR_NAME_CAPACITY 32U", header)
        self.assertLess(len("nvidia-gk208-ring3"), 32)
        self.assertIn(
            "sizeof(supervisor_descriptor_t) <=\n"
            "                   CRITICAL_OBJECT_MAX_PAYLOAD",
            supervisor)
        copy = supervisor[supervisor.index("static bool copy_driver_string") :]
        copy = copy[:copy.index("static bool driver_spawn_next")]
        self.assertIn("length >= capacity", copy)
        self.assertNotIn("capacity - 1U", copy)
        self.assertIn('supervisor_register("nvidia-gk208-ring3"', host)

    def test_images_package_both_display_drivers(self):
        programs = (ROOT / "scripts/build_system_programs.py").read_text(
            encoding="utf-8")
        windows = (ROOT / "scripts/build-windows.ps1").read_text(
            encoding="utf-8")
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        self.assertIn('"NVIDIA.PRG"', programs)
        self.assertIn("userspace/drivers/video/nvidia_gk208.c", programs)
        self.assertIn("userspace/video/lib/nvidia_gk208_2d.c", programs)
        for source in (windows, makefile):
            self.assertIn("libexec/reist/nvidia.prg", source)
            self.assertIn("NVIDIA.PRG", source)

    def test_vmware_profile_remains_first_and_compatible(self):
        profile = (ROOT / "kernel/init/video_device_profile.c").read_text(
            encoding="utf-8")
        self.assertLess(profile.index("VMWARE_VENDOR_ID"),
                        profile.index("NVIDIA_VENDOR_ID"))
        discovery = profile[profile.index(
            "int video_device_profile_discover") :]
        self.assertLess(
            discovery.index("VIDEO_DEVICE_BACKEND_VMWARE_SVGA2"),
            discovery.index("VIDEO_DEVICE_BACKEND_NVIDIA_GK208"))

    def test_stale_boot_framebuffer_does_not_suppress_vbe_activation(self):
        display = (ROOT / "drivers/video/display_control.c").read_text(
            encoding="utf-8")
        start = display.index("int display_control_activate(void)")
        end = display.index("int display_control_deactivate(void)", start)
        activate = display[start:end]
        self.assertIn(
            "active_backend != DISPLAY_BACKEND_NONE && framebuffer_available()",
            activate)
        self.assertNotIn("if (framebuffer_available()) return 0;", activate)

    def test_desktop_uses_vbe_when_optional_driver_is_missing(self):
        desktop = (ROOT / "userspace/gui/compositor/desktop.c").read_text(
            encoding="utf-8")
        self.assertIn("desktop_activate_with_fallback", desktop)
        self.assertIn("x86os_display_activate()", desktop)
        self.assertIn(
            "activation_status != 0 || display_status != 0", desktop)
        self.assertIn("desktop_display_deactivate", desktop)
        self.assertIn("x86os_display_deactivate()", desktop)

    def test_passive_driver_restart_preserves_kernel_owned_vbe(self):
        supervisor = (ROOT / "kernel/init/supervisor.c").read_text(
            encoding="utf-8")
        start = supervisor.index("static bool driver_fence_until(")
        end = supervisor.index("static bool driver_fence_apply(", start)
        fence = supervisor[start:end]
        self.assertIn(
            'bool owns_device_scanout = strcmp(runtime->name, '
            '"svga2d-ring3") == 0;', fence)
        self.assertIn(
            'strcmp(runtime->name, "nvidia-gk208-ring3") == 0;', fence)
        self.assertIn(
            "if (owns_device_scanout && display_control_graphics_active() &&",
            fence)
        self.assertIn("owns_device_scanout || passive_vbe_client", fence)
        self.assertIn("device_domain_mark_mediated_io_quiesced(", fence)
        self.assertLess(
            fence.index("device_domain_mark_mediated_io_quiesced("),
            fence.index("device_domain_fence("))
        nvidia = fence[fence.index("bool passive_vbe_client") :]
        self.assertNotIn(
            'strcmp(runtime->name, "nvidia-gk208-ring3") == 0) {\n'
            "        if (display_control_graphics_active()",
            nvidia)


if __name__ == "__main__":
    unittest.main()
