import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ATA = (ROOT / "drivers/block/ata.c").read_text(encoding="utf-8")
ATA_H = (ROOT / "drivers/block/ata.h").read_text(encoding="utf-8")


def function_body(name: str) -> str:
    match = re.search(
        rf"(?m)^(?:static\s+)?(?:bool|void|uint16_t|uint32_t)\s+{name}\s*"
        rf"\([^;]*?\)\s*\{{",
        ATA,
    )
    if match is None:
        raise AssertionError(f"function {name} not found")
    depth = 1
    cursor = match.end()
    while cursor < len(ATA) and depth:
        if ATA[cursor] == "{":
            depth += 1
        elif ATA[cursor] == "}":
            depth -= 1
        cursor += 1
    if depth:
        raise AssertionError(f"unbalanced function {name}")
    return ATA[match.end() : cursor - 1]


class AtaPciIdeSourceTests(unittest.TestCase):
    def test_only_pci_ide_class_is_selected(self):
        configure = function_body("ata_configure_channels")
        self.assertIn("controller->class_code != ATA_PCI_CLASS_STORAGE", configure)
        self.assertIn("controller->subclass_code != ATA_PCI_SUBCLASS_IDE", configure)
        self.assertIn("PCI_COMMAND_IO", configure)

    def test_compatibility_channels_keep_legacy_command_and_control_ports(self):
        self.assertIn("ATA_PRIMARY_IO, ATA_PRIMARY_CONTROL, true, false", ATA)
        self.assertIn("ATA_SECONDARY_IO, ATA_SECONDARY_CONTROL, true, false", ATA)
        self.assertIn("#define ATA_PRIMARY_CONTROL 0x3F6U", ATA)
        self.assertIn("#define ATA_SECONDARY_CONTROL 0x376U", ATA)

    def test_native_bars_are_io_validated_and_control_offset_is_two(self):
        validate = function_body("ata_pci_io_bar")
        native = function_body("ata_configure_controller_channel")
        self.assertIn("(raw & 1U) == 0U", validate)
        self.assertIn("port > UINT16_MAX", validate)
        self.assertIn("control_base + 2U", native)
        self.assertIn("ATA_PROBE_FLAG_INVALID_BAR", native)

    def test_all_bounded_pci_ide_functions_are_considered(self):
        configure = function_body("ata_configure_channels")
        self.assertIn("ATA_MAX_PCI_CONTROLLERS 4U", ATA)
        self.assertIn("ATA_CHANNEL_CAPACITY", ATA)
        self.assertIn("++ata_pci_ide_function_count", configure)
        self.assertIn("configured_controllers >= ATA_MAX_PCI_CONTROLLERS",
                      configure)
        self.assertNotIn("controller = candidate", configure)
        self.assertNotIn("break;", configure)

    def test_duplicate_command_and_control_ports_are_probed_once(self):
        append = function_body("ata_append_channel")
        self.assertIn("ata_channels[index].command_base == command_base", append)
        self.assertIn("ata_channels[index].control_port == control_port", append)
        self.assertIn("duplicate channel", append)
        self.assertIn("ata_channel_count >= ATA_CHANNEL_CAPACITY", append)

    def test_reset_and_identify_polling_are_bounded(self):
        reset = function_body("ata_probe_reset_channel")
        identify = function_body("ata_wait_identify_data")
        self.assertIn("elapsed < ATA_DETECTION_TIMEOUT_MS", reset)
        self.assertIn("elapsed <= timeout_ms", identify)
        self.assertIn("ATA_DEVICE_CONTROL_NIEN", reset)

    def test_operations_select_target_before_ready_or_command_status(self):
        for name in (
            "ata_read_sector_impl",
            "ata_write_sector_impl",
            "ata_program_pio_batch",
        ):
            with self.subTest(operation=name):
                body = function_body(name)
                selected = body.index("ata_select_target(")
                command = body.find("outb(ATA_COMMAND(base)")
                self.assertGreater(command, selected)
                self.assertNotIn("wait_for_drive_ready", body[:selected])

    def test_target_selection_ignores_stale_err_but_rejects_floating_bus(self):
        select = function_body("ata_select_target")
        self.assertIn("status == 0U || status == 0xFFU", select)
        self.assertIn("status & (0x80U | 0x08U)", select)
        self.assertNotIn("status & 0x01U", select)
        self.assertIn("ATA_ALT_STATUS(base)", function_body("ata_selection_delay"))
        self.assertIn("ata_control_port_for_base(base)", ATA_H)

    def test_probe_summary_is_fixed_width_and_public(self):
        summary = function_body("ata_probe_diagnostics")
        self.assertIn("ata_pci_ide_function_count", summary)
        self.assertIn("ata_pci_storage_function_count", summary)
        self.assertIn("ata_channel_count", summary)
        self.assertIn("ata_identified_drive_count", summary)
        self.assertIn("ata_probe_flags", summary)
        self.assertIn("uint32_t ata_probe_diagnostics(void);", ATA_H)


if __name__ == "__main__":
    unittest.main()
