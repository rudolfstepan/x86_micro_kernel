import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class AhciProbeContractTests(unittest.TestCase):
    def read(self, path):
        return (ROOT / path).read_text(encoding="utf-8")

    def test_probe_requires_ahci_class_and_valid_memory_bar(self):
        header = self.read("drivers/block/ahci.h")
        source = self.read("drivers/block/ahci.c")
        kernel = self.read("kernel/init/kernel.c")
        self.assertIn("AHCI_PCI_CLASS 0x01U", header)
        self.assertIn("AHCI_PCI_SUBCLASS 0x06U", header)
        self.assertIn("AHCI_PCI_PROG_IF 0x01U", header)
        self.assertIn("device->bar[5]", source)
        self.assertIn("device->class_code", source)
        self.assertIn("ahci_init();", kernel)
        self.assertIn("map_mmio_region(controller->abar", source)
        self.assertIn("AHCI_RESET_TIMEOUT_MS", source)
        self.assertIn("AHCI_RESET_MAX_POLLS", source)
        self.assertIn("AHCI_PORT_SSTS", source)
        self.assertIn("AHCI_PORT_SIG", source)
        self.assertIn("AHCI_SIG_ATA", source)
        self.assertIn("__attribute__((aligned(1024)))", source)
        self.assertIn("ahci_dma_address_valid", source)
        self.assertIn("AHCI_PORT_CLB", source)
        self.assertIn("AHCI_PORT_FB", source)
        self.assertIn("ahci_build_identify_command", source)
        self.assertIn("IDENTIFY DEVICE", source)
        self.assertIn("prdt_length = 1U", source)
        self.assertIn("byte_count_and_interrupt", source)
        self.assertIn("AHCI_PORT_CI", source)
        self.assertIn("AHCI_PORT_IS_TFES", source)
        self.assertIn("AHCI_COMMAND_TIMEOUT_MS", source)
        self.assertIn("ahci_stop_port(controller->mmio, port)", source)
        self.assertIn("ahci_identify_word", source)
        self.assertIn("identify_valid_ports", source)
        self.assertIn("sector_count", header)
        self.assertIn("sector_size", header)
        self.assertNotIn("pci_set_bus_master", source)


if __name__ == "__main__":
    unittest.main()
