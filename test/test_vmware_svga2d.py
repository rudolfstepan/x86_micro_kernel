import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class VmwareSvga2dTests(unittest.TestCase):

    def test_driver_fence_still_disables_device_owned_scanout(self):
        supervisor = (ROOT / "kernel/init/supervisor.c").read_text(
            encoding="utf-8")
        start = supervisor.index("static bool driver_fence_until(")
        end = supervisor.index("static bool driver_fence_apply(", start)
        fence = supervisor[start:end]
        self.assertIn(
            'bool owns_device_scanout = strcmp(runtime->name, '
            '"svga2d-ring3") == 0;', fence)
        self.assertIn(
            "if (owns_device_scanout && display_control_graphics_active() &&",
            fence)
        self.assertIn("display_control_deactivate() != 0", fence)
        self.assertLess(fence.index("display_control_deactivate()"),
                        fence.index("device_domain_mark_mediated_io_quiesced("))

    def test_irqless_device_domain_lifecycle(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "vmware-svga2d-domain-test.exe"
            subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-DREIST_HOST_TEST", "-I", str(ROOT),
                 str(ROOT / "kernel/init/device_domain.c"),
                 str(ROOT / "test/test_vmware_svga2d_host.c"),
                 "-o", str(executable)],
                check=True, capture_output=True, text=True, timeout=30,
            )
            result = subprocess.run([str(executable)], timeout=10)
            self.assertEqual(result.returncode, 0)

    def test_protocol_is_fixed_and_command_set_is_bounded(self):
        protocol = (ROOT / "userspace/video/include/reist/svga2d.h").read_text(
            encoding="utf-8")
        display = (ROOT / "drivers/video/display_control.c").read_text(
            encoding="utf-8")
        for command in ("REIST_SVGA2D_RECT_FILL", "REIST_SVGA2D_RECT_COPY",
                        "REIST_SVGA2D_INFO"):
            self.assertIn(command, protocol)
        self.assertIn("sizeof(reist_svga2d_message_t) == 64U", protocol)
        self.assertIn("SVGA_CMD_RECT_FILL 2U", display)
        self.assertIn("SVGA_CMD_RECT_COPY 3U", display)
        self.assertIn("display_control_driver_command", display)
        self.assertNotIn("SVGA_3D_CMD", display)
        self.assertNotIn("GMR", display)

    def test_exact_device_profile_has_no_dma_authority(self):
        profile = (ROOT / "kernel/init/video_device_profile.c").read_text(
            encoding="utf-8")
        header = (ROOT / "include/kernel/device_domain.h").read_text(
            encoding="utf-8")
        self.assertIn("0x15ADU", profile)
        self.assertIn("0x0405U", profile)
        self.assertIn("0x0710U", profile)
        self.assertIn("DEVICE_DOMAIN_PROFILE_MEDIATED_IO", profile)
        self.assertNotIn("DEVICE_DOMAIN_PROFILE_MEDIATED_DMA", profile)
        self.assertIn("DEVICE_DOMAIN_PROFILE_MEDIATED_IO", header)

    def test_irqless_profile_is_quiesced_before_generation_recovery(self):
        domain = (ROOT / "kernel/init/device_domain.c").read_text(
            encoding="utf-8")
        supervisor = (ROOT / "kernel/init/supervisor.c").read_text(
            encoding="utf-8")
        self.assertIn("profile_is_irqless_mediated_io", domain)
        self.assertIn("device_domain_mark_mediated_io_quiesced", domain)
        deactivate = supervisor.index("display_control_deactivate()")
        quiesce = supervisor.index("device_domain_mark_mediated_io_quiesced")
        recover = supervisor.index("device_domain_recover_owner", quiesce)
        self.assertLess(deactivate, quiesce)
        self.assertLess(quiesce, recover)

    def test_driver_is_supervised_and_all_waits_are_timed(self):
        driver = (ROOT / "userspace/drivers/video/vmware_svga2d.c").read_text(
            encoding="utf-8")
        kernel = (ROOT / "kernel/init/kernel.c").read_text(encoding="utf-8")
        supervisor = (ROOT / "kernel/init/supervisor.c").read_text(
            encoding="utf-8")
        self.assertIn("x86os_device_driver_bootstrap", driver)
        self.assertIn("X86OS_DEVICE_DRIVER_REPORT_SELF_TEST", driver)
        self.assertIn("x86os_ipc_receive_timeout", driver)
        self.assertIn("SVGA2D_WAIT_DEADLINE_MS", driver)
        self.assertNotIn("while (1)", driver)
        self.assertIn('"svga2d-ring3"', kernel)
        self.assertIn("REIST_SERVICE_DISPLAY_DRIVER", supervisor)
        self.assertIn('strcmp(client->image_path, "/usr/gui/bin/desktop.prg")',
                      supervisor)

    def test_desktop_authority_uses_canonical_executable_identity(self):
        process_h = (ROOT / "kernel/proc/process.h").read_text(encoding="utf-8")
        process = (ROOT / "kernel/proc/process.c").read_text(encoding="utf-8")
        supervisor = (ROOT / "kernel/init/supervisor.c").read_text(
            encoding="utf-8")
        syscalls = (ROOT / "kernel/syscall/syscall_table.c").read_text(
            encoding="utf-8")
        self.assertIn("char image_path[PROCESS_PATH_MAX]", process_h)
        self.assertIn("strcpy(process->image_path, filename)", process)
        self.assertIn(
            'strcmp(client->image_path, "/usr/gui/bin/desktop.prg")',
            supervisor)
        self.assertIn(
            'strcmp(desktop->image_path, "/usr/gui/bin/desktop.prg")',
            syscalls)
        self.assertNotIn(
            'strcmp(client->name, "/usr/gui/bin/desktop.prg")', supervisor)

    def test_boot_self_test_restores_vga_before_ready(self):
        driver = (ROOT / "userspace/drivers/video/vmware_svga2d.c").read_text(
            encoding="utf-8")
        display = (ROOT / "drivers/video/display_control.c").read_text(
            encoding="utf-8")
        self_test = driver.index("self_test.operation = REIST_SVGA2D_RECT_COPY")
        deactivate = driver.index("status = deactivate(driver)", self_test)
        ready = driver.index("X86OS_DEVICE_DRIVER_REPORT_SELF_TEST", deactivate)
        self.assertLess(self_test, deactivate)
        self.assertLess(deactivate, ready)
        self.assertIn("SVGA_REG_ENABLE, 0U", display)
        self.assertIn("SVGA2D_INACTIVE", display)

    def test_desktop_releases_the_generation_scoped_driver(self):
        desktop = (ROOT / "userspace/gui/compositor/desktop.c").read_text(
            encoding="utf-8")
        helper = desktop[
            desktop.index("static int desktop_display_deactivate"):
            desktop.index("enum {", desktop.index(
                "static int desktop_display_deactivate"))
        ]
        self.assertIn("REIST_SVGA2D_DEACTIVATE", helper)
        self.assertIn("desktop_svga2d_transact", helper)
        self.assertIn("x86os_display_deactivate", helper)
        self.assertIn("desktop_display_deactivate()", desktop)

    def test_desktop_reconnects_after_a_stale_driver_generation(self):
        desktop = (ROOT / "userspace/gui/compositor/desktop.c").read_text(
            encoding="utf-8")
        self.assertIn("DESKTOP_SVGA2D_CONNECT_ATTEMPTS 3U", desktop)
        self.assertIn("DESKTOP_SVGA2D_RETRY_MS 50U", desktop)
        self.assertIn("x86os_ipc_release(desktop_svga2d_endpoint)", desktop)
        helper = desktop[
            desktop.index("static int desktop_svga2d_activate_bounded"):
            desktop.index("static int desktop_svga2d_rect_copy")
        ]
        self.assertIn("desktop_svga2d_forget_endpoint()", helper)
        self.assertIn("x86os_sleep_ms(DESKTOP_SVGA2D_RETRY_MS)", helper)
        self.assertIn("desktop_svga2d_activate_bounded()", desktop)
        transact = desktop[
            desktop.index("static int desktop_svga2d_transact"):
            desktop.index("static int desktop_svga2d_connect")
        ]
        self.assertIn("ipc.length != sizeof(*wire)) {", transact)
        self.assertIn("response.flags != REIST_SVGA2D_FLAG_RESPONSE) {",
                      transact)

    def test_geometry_is_validated_before_fifo_publication(self):
        display = (ROOT / "drivers/video/display_control.c").read_text(
            encoding="utf-8")
        validator = display.index("static bool vmware_rect_valid")
        command = display.index("int display_control_driver_command")
        publish = display.index("vmware_fifo_write_batch", command)
        self.assertLess(validator, command)
        self.assertLess(command, publish)
        helper = display[validator:command]
        self.assertIn("width <= vmware_width - x", helper)
        self.assertIn("height <= vmware_height - y", helper)
        self.assertIn("vmware_rect_valid(request->source_x", display[command:publish])
        self.assertIn("vmware_fifo_write_batch(commands, command_count)",
                      display[command:])

    def test_desktop_retains_software_fallback(self):
        desktop = (ROOT / "userspace/gui/compositor/desktop.c").read_text(
            encoding="utf-8")
        self.assertIn("desktop_svga2d_rect_copy", desktop)
        self.assertIn("x86os_display_frame_mark_accelerated", desktop)
        self.assertIn("render_desktop(display, manager, explorer", desktop)

    def test_generic_activation_cannot_bypass_supervised_driver(self):
        display = (ROOT / "drivers/video/display_control.c").read_text(
            encoding="utf-8")
        self.assertIn("vmware_supervised", display)
        self.assertIn("VMware activation requires", display)
        self.assertIn("result = -13", display)

    def test_qemu_and_vmware_runtime_profiles_are_bounded(self):
        qemu = (ROOT / "scripts/run_qemu_smoke.py").read_text(encoding="utf-8")
        runtime = (ROOT / "scripts/test-reist-runtime.ps1").read_text(
            encoding="utf-8")
        vmware = (ROOT / "scripts/run_vmware_svga2d.ps1").read_text(
            encoding="utf-8")
        lifecycle = (ROOT / "scripts/run_qemu_runtime_desktop.py").read_text(
            encoding="utf-8")
        for marker in ("SVGA2D_ACTIVE", "SVGA2D_RECT_COPY_OK",
                       "SVGA2D_READY"):
            self.assertIn(marker, qemu)
            self.assertIn(marker, vmware)
        self.assertIn('"--vmware-vga"', qemu)
        self.assertIn("'vmware-svga2d'", runtime)
        self.assertIn("'vmware-svga2d-lifecycle'", runtime)
        self.assertIn("require_svga2d_console_lifecycle", lifecycle)
        for marker in ("SVGA2D_INACTIVE", "SVGA2D_READY"):
            self.assertIn(marker, vmware)
            self.assertIn(marker, lifecycle)
        self.assertIn("TimeoutSeconds = 60", vmware)


if __name__ == "__main__":
    unittest.main()
