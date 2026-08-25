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
        self.assertIn("NV902D_SET_DST_FORMAT 0x0200U", source)
        self.assertIn("NV902D_RENDER_SOLID_PRIM_MODE 0x0580U", source)
        self.assertIn("NV902D_SET_PIXELS_FROM_MEMORY_DST_X0 0x08B0U", source)
        self.assertNotIn("malloc", source)

    def test_profile_is_exact_and_irqless(self):
        source = (ROOT / "kernel/init/video_device_profile.c").read_text(
            encoding="utf-8")
        self.assertIn("NVIDIA_VENDOR_ID 0x10DEU", source)
        self.assertIn("NVIDIA_GK208_DEVICE_ID 0x1280U", source)
        self.assertIn("device->class_code != VMWARE_DISPLAY_CLASS", source)
        self.assertIn("device->subclass_code != DISPLAY_VGA_SUBCLASS", source)
        self.assertIn("DEVICE_DOMAIN_PROFILE_MEDIATED_IO", source)
        self.assertNotIn("DEVICE_DOMAIN_PROFILE_MEDIATED_DMA", source)

    def test_kernel_probe_is_passive_and_bounded(self):
        source = (ROOT / "drivers/video/display_control.c").read_text(
            encoding="utf-8")
        start = source.index("static void prepare_nvidia_gk208")
        end = source.index("static pci_device_t *find_vmware", start)
        probe = source[start:end]
        for marker in (
                "NVIDIA_PMC_BOOT_0", "NVIDIA_PMC_ENABLE",
                "NVIDIA_PFIFO_INTR", "NVIDIA_PTIMER_TIME_0",
                "NVIDIA_PTIMER_TIME_1", "NVIDIA_PGRAPH_INTR"):
            self.assertIn(marker, probe)
        self.assertIn("bar0.size_low < NVIDIA_PROBE_MAP_BYTES", probe)
        self.assertIn("bar0.size_low > NVIDIA_BAR_MAX_BYTES", probe)
        self.assertNotIn("pci_enable_device", probe)
        self.assertNotIn("pci_set_bus_master", probe)
        self.assertNotIn("NVIDIA_WRITE", probe)
        self.assertNotIn("= value", probe)

    def test_engine_preflight_is_live_read_only_and_bounded(self):
        display = (ROOT / "drivers/video/display_control.c").read_text(
            encoding="utf-8")
        driver = (ROOT / "userspace/drivers/video/nvidia_gk208.c").read_text(
            encoding="utf-8")
        self.assertIn("DISPLAY_DRIVER_ENGINE_PREFLIGHT", display)
        self.assertIn("nvidia_read_live_probe", display)
        self.assertIn("NVIDIA_PREFLIGHT_DELAY_MS 1U", driver)
        self.assertIn("x86os_sleep_ms(NVIDIA_PREFLIGHT_DELAY_MS)", driver)
        self.assertIn("nvidia_gk208_timer_after", driver)
        self.assertNotIn("x86os_device_open_region", driver)
        self.assertNotIn("x86os_device_bind_dma", driver)
        self.assertNotIn("x86os_device_bind_irq", driver)

    def test_driver_never_advertises_unproven_acceleration(self):
        driver = (ROOT / "userspace/drivers/video/nvidia_gk208.c").read_text(
            encoding="utf-8")
        display = (ROOT / "drivers/video/display_control.c").read_text(
            encoding="utf-8")
        self.assertIn("response->capabilities = 0U", driver)
        self.assertIn("response->status = -95", driver)
        self.assertIn("request->capabilities = 0U", display)
        self.assertIn("DISPLAY_DRIVER_PROBE", display)
        self.assertNotIn("x86os_device_open_region", driver)
        self.assertNotIn("x86os_device_bind_dma", driver)
        self.assertNotIn("x86os_device_bind_irq", driver)
        self.assertIn("reist_nvidia_gk208_command_self_test", driver)

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
        self.assertLess(profile.index("VIDEO_DEVICE_BACKEND_VMWARE_SVGA2"),
                        profile.index("VIDEO_DEVICE_BACKEND_NVIDIA_GK208"))

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
