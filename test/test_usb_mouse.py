import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class UsbMouseTests(unittest.TestCase):
    def test_hid_boot_mouse_host_behavior(self) -> None:
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory(prefix="reist-usb-mouse-") as temp:
            executable = Path(temp) / "hid-mouse-test.exe"
            subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-DHID_MOUSE_HOST_TEST", "-I.",
                 "test/test_usb_hid_mouse_host.c",
                 "drivers/usb/hid_mouse.c", "lib/libc/string.c",
                 "-o", str(executable)],
                cwd=ROOT, check=True, capture_output=True, text=True)
            subprocess.run([str(executable)], cwd=ROOT, check=True,
                           capture_output=True, text=True, timeout=5)

    def test_xhci_accepts_mouse_protocol_and_matches_transfer_trbs(self):
        source = (ROOT / "drivers/usb/xhci.c").read_text(encoding="utf-8")
        self.assertIn("config[offset + 7U] == 2U", source)
        self.assertIn("hid_mouse_attach(controller.generation)", source)
        self.assertIn("hid_mouse_report(controller.generation", source)
        self.assertIn("&interrupt_ring[0]", source)
        self.assertIn("status & 0x00FFFFFFU", source)
        self.assertIn("controller.command_index++", source)
        self.assertIn("dcbaa[slot] = xhci_dma32(device_context)", source)
        self.assertIn("controller.context_size", source)
        self.assertIn("command->d[3] = TRB_TYPE(type) | control", source)
        self.assertIn("TRB_TYPE(TRB_SETUP) | TRB_IDT | TRB_CHAIN", source)
        self.assertIn("setup->d[2] = 8U", source)
        self.assertIn("transfer_type |", source)
        self.assertNotIn("setup->d[2] = (transfer_type << 16U) | 8U", source)
        self.assertIn("port_speed == 3U ? 64U : 8U", source)
        self.assertGreaterEqual(
            source.count("XHCI_IMAN_IP | XHCI_IMAN_IE"), 2
        )
        self.assertIn("XHCI_COMPLETION_SHORT_PACKET 13U", source)
        self.assertIn("completion == XHCI_COMPLETION_SHORT_PACKET", source)
        self.assertIn("xhci_queue_interrupt_report", source)
        self.assertIn("#define XHCI_MAX_PORTS          32U", source)
        self.assertIn("max_packet > 64U", source)
        self.assertIn("xHCI HID already active; preserving controller", source)
        self.assertIn("connected=%08X", source)
        self.assertIn("#define XHCI_MAX_HID_CANDIDATES 8U", source)
        self.assertIn("attempts < XHCI_MAX_HID_CANDIDATES", source)
        self.assertIn("xhci_enumerate_root_hid(root_port)", source)
        self.assertIn("keyboard_port", source)
        self.assertIn("xhci_get_diagnostics", source)
        self.assertIn("diagnostics.mouse_reports++", source)
        mouse_choice = source.index("current_protocol == 2U")
        keyboard_choice = source.index("current_protocol == 1U", mouse_choice)
        self.assertLess(mouse_choice, keyboard_choice)

    def test_usbinfo_persists_boot_diagnostics_for_the_shell(self):
        shell = (ROOT / "kernel/shell/command.c").read_text(encoding="utf-8")
        header = (ROOT / "drivers/usb/xhci.h").read_text(encoding="utf-8")
        self.assertIn('{"USBINFO", cmd_usbinfo}', shell)
        self.assertIn("void cmd_usbinfo", shell)
        self.assertIn("xHCI state=%s", shell)
        self.assertIn("transfers=%u mouse=%u rejected=%u", shell)
        self.assertIn("XHCI_DIAG_MOUSE_READY", header)
        self.assertIn("xhci_diagnostics_t", header)

    def test_usbinfo_is_reachable_from_the_normal_userspace_shell(self):
        program = (ROOT / "userspace/programs/usbinfo.c").read_text(
            encoding="utf-8")
        programs = (ROOT / "scripts/build_system_programs.py").read_text(
            encoding="utf-8")
        windows = (ROOT / "scripts/build-windows.ps1").read_text(
            encoding="utf-8")
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        shell = (ROOT / "userspace/bin/shell.c").read_text(encoding="utf-8")
        shell = (ROOT / "userspace/bin/shell.c").read_text(encoding="utf-8")
        sdk_header = (ROOT / "userspace/sdk/include/x86os.h").read_text(
            encoding="utf-8")
        sdk = (ROOT / "userspace/sdk/x86os.c").read_text(encoding="utf-8")
        syscalls = (ROOT / "kernel/syscall/syscall_table.c").read_text(
            encoding="utf-8")
        self.assertIn('"USBINFO.PRG"', programs)
        self.assertIn("'sbin/usbinfo.prg'", windows)
        self.assertIn("sbin/usbinfo.prg=", makefile)
        self.assertIn('"/bin", "/sbin", "/usr/bin"', shell)
        self.assertIn("run_program(argc, argv)", shell)
        self.assertIn('"/bin", "/sbin", "/usr/bin"', shell)
        self.assertIn("run_program(argc, argv)", shell)
        self.assertIn("x86os_usb_diagnostics(&status)", program)
        self.assertIn("X86OS_SYS_USB_DIAGNOSTICS = 112", sdk_header)
        self.assertIn("X86OS_USB_DIAGNOSTICS_VERSION", sdk_header)
        self.assertIn("int x86os_usb_diagnostics", sdk)
        self.assertIn("case SYS_USB_DIAGNOSTICS:", syscalls)
        function_start = syscalls.index("static int syscall_usb_diagnostics")
        function_end = syscalls.index("\n}", function_start)
        function = syscalls[function_start:function_end]
        self.assertLess(function.index("copy_from_user"),
                        function.index("xhci_poll()"))
        self.assertIn("copy_to_user", function)

    def test_xhci_preserves_consumed_event_cycle_bits(self):
        source = (ROOT / "drivers/usb/xhci.c").read_text(encoding="utf-8")
        drain_start = source.index("static bool xhci_drain_events")
        drain_end = source.index("static bool xhci_wait_command", drain_start)
        drain = source[drain_start:drain_end]
        self.assertNotIn("memset(event, 0, sizeof(*event))", drain)
        self.assertIn("controller.event_cycle = !controller.event_cycle", drain)

    def test_desktop_escape_returns_to_parent_shell(self):
        source = (ROOT / "userspace/programs/desktop.c").read_text(
            encoding="utf-8")
        escape = source.index("key == DESKTOP_KEY_ESCAPE")
        branch = source[escape:source.index("\n\n        if (selected", escape)]
        self.assertIn("return 0;", branch)
        self.assertNotIn("launch_app", branch)
        self.assertIn("x86os_display_deactivate()", branch)
        self.assertIn('x86os_puts("DESKTOP_EXIT_OK\\n")', branch)

    def test_desktop_batches_mouse_reports_and_uses_software_pointer(self):
        source = (ROOT / "userspace/programs/desktop.c").read_text(
            encoding="utf-8")
        self.assertIn("mouse_events < 32U", source)
        self.assertIn("x86os_pointer_update(pointer_x, pointer_y, 1U)", source)
        self.assertNotIn("draw_mouse_pointer", source)

    def test_mouse_syscall_is_append_only_and_pointer_checked(self):
        stdlib = (ROOT / "lib/libc/stdlib.h").read_text(encoding="utf-8")
        syscalls = (ROOT / "kernel/syscall/syscall_table.c").read_text(
            encoding="utf-8")
        header = (ROOT / "userspace/sdk/include/x86os.h").read_text(
            encoding="utf-8")
        self.assertIn("SYS_MOUSE_EVENT 110", stdlib)
        self.assertIn("X86OS_SYS_MOUSE_EVENT = 110", header)
        self.assertIn("X86OS_SYS_POINTER_UPDATE = 111", header)
        self.assertIn("SYS_POINTER_UPDATE 111", stdlib)
        self.assertIn("framebuffer_cursor_update", syscalls)
        start = syscalls.index("static int syscall_mouse_event")
        body = syscalls[start:syscalls.index("\n}", start)]
        self.assertLess(body.index("copy_from_user"),
                        body.index("hid_mouse_read_event"))
        self.assertIn("copy_to_user", body)

    def test_vmware_requests_the_actual_xhci_controller_key(self):
        image = (ROOT / "scripts/create_native_boot_image.py").read_text(
            encoding="utf-8")
        self.assertIn('usb_xhci.present = "TRUE"', image)
        self.assertIn('pciBridge4.virtualDev = "pcieRootPort"', image)
        self.assertIn('usb_xhci.pciSlotNumber = "160"', image)
        self.assertIn('usb_xhci:4.deviceType = "hid"', image)
        self.assertIn('mouse.vusb.present = "TRUE"', image)
        self.assertNotIn('\nxhci.present = "TRUE"', image)


if __name__ == "__main__":
    unittest.main()
