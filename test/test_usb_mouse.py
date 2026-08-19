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
        self.assertIn("hid_mouse_attach(hid->generation)", source)
        self.assertIn("hid_mouse_report(hid->generation", source)
        self.assertIn("xhci_interrupt_ring(hid)", source)
        self.assertIn("status & 0x00FFFFFFU", source)
        self.assertIn("controller.command_index++", source)
        self.assertIn("dcbaa[slot] = xhci_dma32(device_context)", source)
        self.assertIn("controller.context_size", source)
        self.assertIn("command->d[3] = TRB_TYPE(type) | control", source)
        self.assertIn("TRB_TYPE(TRB_SETUP) | TRB_IDT", source)
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
        self.assertIn("xhci_enumerate_root_hid(hid, root_port, protocol_mask)",
                      source)
        self.assertIn("#define XHCI_MAX_HID_DEVICES    2U", source)
        self.assertIn("endpoint0_rings[XHCI_MAX_HID_DEVICES]", source)
        self.assertIn("interrupt_rings[XHCI_MAX_HID_DEVICES]", source)
        self.assertIn("xhci_hid_by_protocol(1U)", source)
        self.assertIn("xhci_hid_by_protocol(2U)", source)
        scan_start = source.index("for (uint32_t root_port = 1U;")
        scan_end = source.index("if (release_failed ||", scan_start)
        self.assertNotIn("xhci_start_controller", source[scan_start:scan_end])
        self.assertIn("xhci_get_diagnostics", source)
        self.assertIn("diagnostics.mouse_reports++", source)
        self.assertIn("static uint32_t xhci_scratchpad_count", source)
        self.assertIn("uint32_t high = (hcs2 >> 21U) & 0x1FU", source)
        self.assertIn("uint32_t low = (hcs2 >> 27U) & 0x1FU", source)
        self.assertIn("return (high << 5U) | low", source)
        self.assertNotIn("((hcs2 >> 21U) & 0x1FU) |", source)
        self.assertIn("xhci_read(0x14U) & ~0x3U", source)
        self.assertIn("xhci_read(0x18U) & ~0x1FU", source)
        self.assertIn("#define XHCI_MAX_SCRATCHPADS    32U", source)
        self.assertIn("diagnostics.capability_rejections", source)
        self.assertIn("XHCI_CAP_REJECT_SCRATCHPADS", source)
        self.assertIn("#define XHCI_INTEL_XUSB2PR", source)
        self.assertIn("#define XHCI_INTEL_USB2PRM", source)
        self.assertIn("#define XHCI_INTEL_USB3_PSSEN", source)
        self.assertIn("#define XHCI_INTEL_USB3PRM", source)
        self.assertIn("static bool xhci_route_intel_ports", source)
        self.assertIn("candidate->prog_if == 0x20U", source)
        usb3_write = source.index(
            "xhci_pci_write32(dev, XHCI_INTEL_USB3_PSSEN, usb3_mask)")
        usb2_write = source.index(
            "xhci_pci_write32(dev, XHCI_INTEL_XUSB2PR, usb2_mask)")
        self.assertLess(usb3_write, usb2_write)
        self.assertIn("XHCI_INTEL_ROUTE_VERIFY_FAILED", source)
        self.assertIn("xhci_wait_connected_ports(XHCI_PORT_SETTLE_MS)", source)
        settle_start = source.index("static uint32_t xhci_wait_connected_ports")
        settle_end = source.index("static bool xhci_enumerate_root_hid",
                                  settle_start)
        settle = source[settle_start:settle_end]
        self.assertIn("observed |= xhci_connected_ports()", settle)
        self.assertNotIn("connected != 0U) return", settle)
        self.assertIn("XHCI_LEGACY_BIOS_OWNED", source)
        self.assertIn("xhci_write(extended, header | XHCI_LEGACY_OS_OWNED)",
                      source)
        self.assertNotIn("xhci_write(extended + 4U, legacy", source)
        self.assertIn("#define XHCI_LEGACY_SMI_ENABLES", source)
        self.assertIn("smi_before & XHCI_LEGACY_SMI_PRESERVE", source)
        self.assertIn("XHCI_LEGACY_SMI_EVENTS", source)
        self.assertIn("(smi_after & XHCI_LEGACY_SMI_ENABLES) == 0U", source)
        self.assertIn("extended += increment", source)
        self.assertIn('reset stage=halt-request', source)
        handoff = source.index("if (!xhci_legacy_handoff())")
        routing = source.index("if (!xhci_route_intel_ports(dev))", handoff)
        self.assertLess(handoff, routing)
        self.assertIn(
            "mouse_found && (!keyboard_found || "
            "mouse_interface < keyboard_interface)",
            source,
        )
        self.assertIn("if (!keyboard_found) return false", source)
        mouse_choice = source.index(
            "mouse_found && (!keyboard_found || "
            "mouse_interface < keyboard_interface)"
        )
        keyboard_choice = source.index("if (!keyboard_found) return false")
        self.assertLess(mouse_choice, keyboard_choice)
        interrupt_start = source.index("static void xhci_queue_interrupt_report")
        interrupt_end = source.index("static xhci_trb_t *xhci_command",
                                     interrupt_start)
        interrupt = source[interrupt_start:interrupt_end]
        self.assertIn("TRB_TYPE(TRB_TRANSFER) | TRB_ISP | TRB_IOC", interrupt)

    def test_usbinfo_persists_boot_diagnostics_for_the_shell(self):
        shell = (ROOT / "kernel/shell/command.c").read_text(encoding="utf-8")
        header = (ROOT / "drivers/usb/xhci.h").read_text(encoding="utf-8")
        self.assertIn('{"USBINFO", cmd_usbinfo}', shell)
        self.assertIn("void cmd_usbinfo", shell)
        self.assertIn("xHCI state=%s", shell)
        self.assertIn("transfers=%u keyboard=%u key-rejected=%u", shell)
        self.assertIn("XHCI_DIAG_MOUSE_READY", header)
        self.assertIn("XHCI_DIAG_KEYBOARD_MOUSE_READY", header)
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
        self.assertIn('"/bin", "/sbin", "/usr/bin", "/usr/gui/bin"', shell)
        self.assertIn("run_program(argc, argv)", shell)
        self.assertIn('"/bin", "/sbin", "/usr/bin", "/usr/gui/bin"', shell)
        self.assertIn("run_program(argc, argv)", shell)
        self.assertIn("x86os_usb_diagnostics(&status)", program)
        self.assertNotIn("x86os_usb_diagnostics_t status = {0}", program)
        self.assertIn("X86OS_SYS_USB_DIAGNOSTICS = 112", sdk_header)
        self.assertIn("X86OS_USB_DIAGNOSTICS_VERSION 5U", sdk_header)
        self.assertIn("uint32_t capability_rejections", sdk_header)
        self.assertIn("uint32_t intel_routing_flags", sdk_header)
        self.assertIn("X86OS_USB_INTEL_ROUTE_USB2_VERIFIED", sdk_header)
        self.assertIn("int x86os_usb_diagnostics", sdk)
        self.assertIn("sizeof(x86os_usb_diagnostics_t) == 208U", sdk)
        self.assertIn("case SYS_USB_DIAGNOSTICS:", syscalls)
        self.assertIn("SYSCALL_USB_DIAGNOSTICS_V1_SIZE 96U", syscalls)
        self.assertIn("SYSCALL_USB_DIAGNOSTICS_V2_SIZE 120U", syscalls)
        self.assertIn("SYSCALL_USB_DIAGNOSTICS_V3_SIZE 148U", syscalls)
        self.assertIn("SYSCALL_USB_DIAGNOSTICS_V4_SIZE 180U", syscalls)
        self.assertIn("sizeof(syscall_usb_diagnostics_t) == 208U", syscalls)
        function_start = syscalls.index("static int syscall_usb_diagnostics")
        function_end = syscalls.index("\n}", function_start)
        function = syscalls[function_start:function_end]
        self.assertLess(function.index("copy_from_user"),
                        function.index("xhci_poll()"))
        self.assertIn("copy_to_user", function)
        self.assertIn("result_size", function)
        self.assertIn("result.capability_rejections", function)
        self.assertIn("result.intel_routing_flags", function)
        self.assertIn("result.keyboard_port", function)
        self.assertIn("result.keyboard_reports", function)
        self.assertIn("result.mouse_port", function)
        self.assertIn("result.failure_stage", function)
        self.assertIn("result.configuration_length", function)
        self.assertIn('x86os_puts(" reject=")', program)
        self.assertIn('x86os_puts(" route-flags=")', program)
        self.assertIn('x86os_puts("     failure=")', program)
        self.assertIn('return "keyboard-mouse-ready"', program)

    def test_xhci_preserves_consumed_event_cycle_bits(self):
        source = (ROOT / "drivers/usb/xhci.c").read_text(encoding="utf-8")
        drain_start = source.index("static bool xhci_drain_events")
        drain_end = source.index("static bool xhci_wait_command", drain_start)
        drain = source[drain_start:drain_end]
        self.assertNotIn("memset(event, 0, sizeof(*event))", drain)
        self.assertIn("controller.event_cycle = !controller.event_cycle", drain)

    def test_xhci_evaluates_changed_full_speed_ep0_packet_size(self):
        source = (ROOT / "drivers/usb/xhci.c").read_text(encoding="utf-8")
        self.assertIn("TRB_EVALUATE_CONTEXT", source)
        start = source.index("static bool xhci_update_ep0_max_packet")
        end = source.index("static bool xhci_release_candidate", start)
        helper = source[start:end]
        self.assertIn("xhci_device_context(hid) + controller.context_size",
                      helper)
        self.assertIn("control[1] = (1U << 1U)", helper)
        self.assertIn("ep0[0] &= ~0x7U", helper)
        self.assertIn("~(0xFFFFU << 16U)", helper)
        self.assertIn("xhci_command(TRB_EVALUATE_CONTEXT", helper)
        self.assertIn("completed_slot != hid->slot_id", helper)
        self.assertIn("xhci_update_ep0_max_packet(hid, descriptor_packet)",
                      source)

    def test_xhci_waits_for_physical_port_reset_completion(self):
        source = (ROOT / "drivers/usb/xhci.c").read_text(encoding="utf-8")
        self.assertIn("static uint32_t xhci_port_neutral", source)
        self.assertIn("static bool xhci_reset_root_port", source)
        settle_start = source.index("static bool xhci_wait_port_recovery")
        settle_end = source.index("static bool xhci_reset_root_port",
                                  settle_start)
        settle = source[settle_start:settle_end]
        self.assertIn("pit_delay(delay_ms);", settle)
        self.assertIn("XHCI_PORT_MAX_RECOVERY_MS", settle)
        self.assertNotIn("XHCI_POLL_LIMIT", settle)
        start = source.index("static bool xhci_reset_root_port")
        end = source.index("static bool xhci_enumerate_root_hid", start)
        helper = source[start:end]
        self.assertIn("port & XHCI_PORT_CHANGE_BITS", helper)
        self.assertIn("(port & XHCI_PORT_PR) == 0U", helper)
        self.assertIn("(port & XHCI_PORT_PRC) != 0U", helper)
        self.assertIn("(port & XHCI_PORT_PED) != 0U", helper)
        self.assertIn(
            "xhci_wait_port_recovery(XHCI_PORT_RESET_RECOVERY_MS)", helper
        )
        self.assertNotIn(
            "(port & ~(XHCI_PORT_CSC | XHCI_PORT_PRC))", source
        )

    def test_xhci_control_td_matches_physical_controller_contract(self):
        source = (ROOT / "drivers/usb/xhci.c").read_text(encoding="utf-8")
        self.assertIn("#define TRB_ISP", source)
        start = source.index("static bool xhci_wait_control_transfer")
        end = source.index("static bool xhci_control", start)
        helper = source[start:end]
        self.assertIn("uint32_t requested_length", helper)
        self.assertIn("uint32_t data_pointer", helper)
        self.assertIn("uint32_t status_pointer", helper)
        self.assertIn("pointer == data_pointer", helper)
        self.assertIn("pointer == status_pointer", helper)
        self.assertIn("saw_data_short = true", helper)
        self.assertIn("completion != XHCI_COMPLETION_SHORT_PACKET", helper)
        self.assertIn("completion == XHCI_COMPLETION_SUCCESS", helper)
        self.assertIn("residual > requested_length", helper)
        self.assertIn("requested_length - residual", helper)
        self.assertIn("actual == requested_length", helper)
        control_start = source.index("static bool xhci_control")
        control_end = source.index("static bool xhci_address_device",
                                   control_start)
        control = source[control_start:control_end]
        self.assertIn("direction_in ? TRB_DIR_IN | TRB_ISP : 0U", control)
        self.assertNotIn("TRB_TYPE(TRB_SETUP) | TRB_IDT | TRB_CHAIN",
                         control)
        self.assertNotIn("TRB_TYPE(TRB_DATA) | TRB_CHAIN", control)
        self.assertIn("setup->d[3] = setup_control ^ TRB_CYCLE", control)
        self.assertIn("xhci_dma_write_barrier();\n    setup->d[3] = setup_control",
                      control)
        self.assertIn("xhci_wait_control_transfer(data_trb, status", control)
        self.assertIn("diagnostics.last_actual_length", helper)
        doorbell_start = source.index("static void xhci_ring_doorbell")
        doorbell_end = source.index("static void xhci_queue_interrupt_report",
                                    doorbell_start)
        doorbell = source[doorbell_start:doorbell_end]
        self.assertIn("xhci_dma_write_barrier()", doorbell)
        self.assertIn("(void)xhci_read(offset)", doorbell)

    def test_desktop_escape_returns_to_parent_shell(self):
        source = (ROOT / "userspace/gui/compositor/desktop.c").read_text(
            encoding="utf-8")
        escape = source.index(
            "if ((actions & DESKTOP_WM_RESULT_EXIT) != 0U)"
        )
        branch = source[
            escape:source.index(
                "\n\n        if ((actions & DESKTOP_WM_RESULT_LAUNCH)",
                escape,
            )
        ]
        self.assertIn("return 0;", branch)
        self.assertNotIn("launch_app", branch)
        self.assertIn("x86os_display_deactivate()", branch)
        self.assertIn('x86os_puts("DESKTOP_EXIT_OK\\n")', branch)

    def test_desktop_batches_mouse_reports_and_uses_software_pointer(self):
        source = (ROOT / "userspace/gui/compositor/desktop.c").read_text(
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
