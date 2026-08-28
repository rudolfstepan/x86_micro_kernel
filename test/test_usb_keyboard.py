import shutil
import subprocess
import tempfile
import unittest
import os
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class UsbKeyboardTests(unittest.TestCase):
    def test_hid_boot_keyboard_host_behavior(self) -> None:
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory(prefix="reist-usb-hid-") as temp:
            executable = Path(temp) / "hid-keyboard-test.exe"
            command = [
                compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-DREIST_USB_HID_HOST_TEST", "-I.",
                "test/test_usb_hid_keyboard_host.c",
                "drivers/usb/hid_kb.c", "-o", str(executable),
            ]
            if os.name != "nt":
                command.append("-pthread")
            subprocess.run(
                command,
                cwd=ROOT, check=True, capture_output=True, text=True)
            subprocess.run([str(executable)], cwd=ROOT, check=True,
                           capture_output=True, text=True, timeout=5)

    def test_xhci_contract_is_bounded_and_does_not_publish_partial_devices(self):
        source = (ROOT / "drivers/usb/xhci.c").read_text(encoding="utf-8")
        self.assertIn("XHCI_POLL_LIMIT", source)
        self.assertIn("pit_monotonic_ms", source)
        self.assertIn("xhci_legacy_handoff", source)
        self.assertIn("pci_set_bus_master(dev->bus, dev->slot, dev->function, 1U)", source)
        self.assertIn("TRB_ENABLE_SLOT", source)
        self.assertIn("TRB_CONFIGURE_ENDPOINT", source)
        self.assertIn("hid_keyboard_attach(hid->generation)", source)
        self.assertIn("hid_keyboard_report(hid->generation", source)
        self.assertIn("diagnostics.keyboard_reports++", source)
        self.assertIn("diagnostics.rejected_keyboard_reports++", source)
        self.assertIn("XHCI_HID_KEYBOARD_MASK", source)
        self.assertIn("XHCI_HID_MOUSE_MASK", source)
        self.assertIn("lowest-numbered boot interface", source)
        set_protocol = source.index("0x21U, 0x0BU, 0U, interface")
        set_idle = source.index("0x21U, 0x0AU, 10U << 8U, interface")
        self.assertLess(set_protocol, set_idle)
        self.assertIn("xhci_release_candidate", source)
        self.assertIn("static uint8_t xhci_periodic_interval", source)
        self.assertIn("memcpy(saved_slot, xhci_device_context(hid)", source)
        self.assertIn("((uint32_t)hid->endpoint_id << 27U)", source)
        self.assertIn(
            "ep[4] = (uint32_t)packet | "
            "(((uint32_t)packet >> 8U) << 16U)", source)
        self.assertNotIn(
            "memcpy(saved_slot, input_context + controller.context_size",
            source)
        self.assertIn("return -1", source)

    def test_hid_parser_rejects_rollover_and_stale_generations(self):
        source = (ROOT / "drivers/usb/hid_kb.c").read_text(encoding="utf-8")
        self.assertIn("active_generation", source)
        self.assertIn("report[1] != 0U", source)
        self.assertIn("report[index] >= 1U && report[index] <= 3U", source)
        self.assertIn("generation != active_generation", source)
        self.assertIn("hid_keyboard_detach", source)
        self.assertIn("keyboard_state_lock", source)
        self.assertIn("hid_sync_lock_acquire", source)
        self.assertIn("hid_sync_lock_release", source)

    def test_blocking_console_input_polls_xhci_before_queue_lock(self):
        source = (ROOT / "drivers/char/kb.c").read_text(encoding="utf-8")
        start = source.index("char getchar(void)")
        end = source.index("char getchar_nonblocking(void)", start)
        blocking = source[start:end]
        self.assertIn("xhci_poll();", blocking)
        self.assertLess(blocking.index("xhci_poll();"),
                        blocking.index("uint32_t flags = irq_save();"))
        self.assertIn("KEYBOARD_POLL_INTERVAL_MS", blocking)

    def test_userspace_shell_prints_usb_keyboard_state_without_input(self):
        source = (ROOT / "userspace/bin/shell.c").read_text(encoding="utf-8")
        self.assertIn("static void show_usb_keyboard_startup(void)", source)
        self.assertIn("x86os_usb_diagnostics(&status)", source)
        self.assertIn("status.keyboard_port", source)
        self.assertIn("status.keyboard_reports", source)
        self.assertIn("usb_failure_name(status.failure_stage)", source)
        self.assertIn("status.configuration_length", source)
        self.assertIn("status.control_request_type", source)
        self.assertIn("status.control_request", source)
        self.assertIn("status.control_value", source)
        self.assertIn("status.control_index", source)
        self.assertIn("status.control_length", source)
        self.assertIn("status.control_completion", source)
        self.assertIn("status.control_residual", source)
        self.assertIn("status.control_event_stage", source)
        self.assertIn("status.control_flags", source)
        main = source[source.index("int main("):]
        self.assertLess(main.index("show_usb_keyboard_startup();"),
                        main.index("for (;;)"))

    def test_mandatory_hid_control_requests_remain_fail_closed(self):
        source = (ROOT / "drivers/usb/xhci.c").read_text(encoding="utf-8")
        start = source.index("static bool xhci_configure_boot_hid")
        end = source.index("static bool xhci_start_controller", start)
        configure = source[start:end]
        self.assertIn(
            "!xhci_control(hid, 0x00U, 9U, configuration, 0U, 0U, "
            "false, NULL)", configure)
        self.assertIn(
            "!xhci_control(hid, 0x21U, 0x0BU, 0U, interface, 0U, "
            "false, NULL)", configure)
        self.assertIn("XHCI_FAILURE_SET_CONFIGURATION", configure)
        self.assertNotIn("retry", configure.lower())

    def test_usb_contract_documents_physical_short_packet_boundary(self):
        contract = (ROOT / "docs/architecture/USB_SUBSYSTEM.md").read_text(
            encoding="utf-8")
        self.assertIn("USB 2.0", contract)
        self.assertIn("HID 1.11", contract)
        self.assertIn("xHCI 1.2", contract)
        self.assertIn("Completion Code 13", contract)
        self.assertIn("SET_CONFIGURATION", contract)
        self.assertIn("SET_PROTOCOL", contract)
        self.assertIn("zero-length", contract)
        self.assertIn("fail-closed", contract)

    def test_xhci_port_discovery_does_not_depend_on_ps2_boot_delay(self):
        source = (ROOT / "drivers/usb/xhci.c").read_text(encoding="utf-8")
        self.assertIn(
            "uint32_t connected_ports =\n        "
            "xhci_wait_connected_ports(XHCI_PORT_SETTLE_MS);",
            source,
        )
        self.assertNotIn(
            "? xhci_wait_connected_ports(XHCI_PORT_SETTLE_MS)\n"
            "        : xhci_connected_ports()",
            source,
        )

    def test_every_boot_reaches_shell_without_desktop_autostart(self):
        kernel = (ROOT / "kernel/init/kernel.c").read_text(encoding="utf-8")
        start = kernel.index("REIST_GUI DESKTOP_AUTOSTART_DISABLED")
        shell = kernel.index(
            'start_userspace_program(multiboot_info, "bin/shell.prg"', start)
        boot = kernel[start:shell]
        self.assertIn("explicit DESKTOP command required", boot)
        self.assertNotIn("supervisor_start_compositor", kernel)
        self.assertNotIn("supervisor_compositor_session_active", kernel)
        self.assertNotIn("REIST_RESILIENT_PAGE_BOOT_PROOF", boot)


if __name__ == "__main__":
    unittest.main()
