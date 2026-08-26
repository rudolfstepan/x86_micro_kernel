"""Source contract for the bounded OHCI USB-1.1 HID host controller (stage 1)."""
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


class OhciSourceContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.driver = read("drivers/usb/ohci.c")
        cls.header = read("drivers/usb/ohci.h")
        cls.usb_core = read("drivers/usb/usb_core.c")
        cls.ehci_companion = read("drivers/usb/ehci_companion.c")
        cls.kb = read("drivers/char/kb.c")

    def test_public_interface_is_declared(self):
        self.assertIn("int ohci_probe(pci_device_t *dev);", self.header)
        self.assertIn("void ohci_poll(void);", self.header)

    def test_probe_only_accepts_ohci_programming_interface(self):
        self.assertIn("dev->prog_if != 0x10U", self.driver)

    def test_dma_objects_are_validated_before_hardware(self):
        self.assertIn("__attribute__((aligned(256)))", self.driver)
        self.assertIn("ohci_dma_valid(hcca, 256U)", self.driver)
        self.assertNotIn("malloc", self.driver)

    def test_bringup_reclaims_ownership_and_reaches_operational(self):
        self.assertIn("ohci_acquire_ownership", self.driver)
        self.assertIn("OHCI_CMD_OCR", self.driver)
        self.assertIn("OHCI_CTRL_HCFS_OPERATIONAL", self.driver)
        self.assertIn("map_mmio_region", self.driver)
        self.assertIn(
            "pci_set_bus_master_verified(dev, true)",
            self.driver)

    def test_every_wait_is_bounded(self):
        self.assertIn("OHCI_RESET_TIMEOUT_MS", self.driver)
        self.assertIn("pit_monotonic_ms", self.driver)
        self.assertIn("ohci_deadline_expired", self.driver)
        self.assertIn("cpu_halt();", self.driver)
        self.assertNotIn("bounded settle */ }", self.driver)

    def test_ehci_ports_are_quiesced_and_returned_to_companions(self):
        self.assertIn("EHCI_OP_CONFIGFLAG", self.ehci_companion)
        self.assertIn("EHCI_USBSTS_HALTED", self.ehci_companion)
        self.assertIn("claim_legacy_ownership", self.ehci_companion)
        self.assertIn("pci_set_bus_master_verified(device, false)",
                      self.ehci_companion)
        self.assertIn("ehci_route_ports_to_companions()", self.usb_core)

    def test_usb_core_dispatches_ohci_for_prog_if_0x10(self):
        self.assertIn('#include "drivers/usb/ohci.h"', self.usb_core)
        self.assertIn("dev->prog_if == 0x10", self.usb_core)
        self.assertIn("ohci_probe(dev)", self.usb_core)

    def test_console_poll_services_ohci(self):
        self.assertIn('#include "drivers/usb/ohci.h"', self.kb)
        self.assertIn("ohci_poll();", self.kb)

    def test_enumeration_runs_bounded_control_transfers(self):
        self.assertIn("ohci_control_transfer", self.driver)
        self.assertIn("ohci_wait_ed_done", self.driver)
        self.assertIn("OHCI_CONTROL_TIMEOUT_MS", self.driver)
        # SET_ADDRESS (0x05), SET_CONFIGURATION (0x09), HID SET_PROTOCOL (0x0B)
        self.assertIn("0x05U", self.driver)
        self.assertIn("0x09U", self.driver)
        self.assertIn("0x0BU", self.driver)

    def test_all_connected_root_ports_are_checked_without_address_collision(self):
        self.assertIn("port <= controller.port_count", self.driver)
        self.assertIn("diagnostics.connected_ports &", self.driver)
        self.assertIn("hid.address = (uint8_t)port", self.driver)

    def test_boot_hid_interface_and_interrupt_endpoint_are_selected(self):
        self.assertIn("ohci_parse_boot_hid", self.driver)
        self.assertIn("cls == 3U && sub == 1U", self.driver)
        self.assertIn("ohci_arm_interrupt", self.driver)

    def test_reports_flow_into_shared_hid_layer(self):
        self.assertIn("hid_keyboard_report(device->generation", self.driver)
        self.assertIn("hid_mouse_report(device->generation", self.driver)
        self.assertIn("hid_keyboard_attach(enumerating_hid.generation)",
                      self.driver)

    def test_keyboard_and_mouse_have_independent_periodic_endpoints(self):
        self.assertIn("interrupt_eds[OHCI_HID_SLOT_COUNT]", self.driver)
        self.assertIn("hid_devices[OHCI_HID_SLOT_COUNT]", self.driver)
        self.assertIn("OHCI_DIAG_KEYBOARD_MOUSE_READY", self.driver)

    def test_ohci_diagnostics_are_publicly_readable(self):
        self.assertIn("ohci_get_diagnostics", self.header)
        self.assertIn("bool ohci_get_diagnostics", self.driver)

    def test_disconnect_detaches_generation_scoped_device(self):
        self.assertIn("OHCI_PORT_CCS", self.driver)
        self.assertIn("ohci_detach_hid", self.driver)
        self.assertIn("hid_keyboard_detach(device->generation)", self.driver)


if __name__ == "__main__":
    unittest.main()
