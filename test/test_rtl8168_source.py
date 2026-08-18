import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


class RTL8168SourceTests(unittest.TestCase):
    def test_h81m_k_binding_is_registered(self) -> None:
        driver = read("drivers/net/rtl8168.c")
        kernel = read("kernel/init/kernel.c")
        self.assertIn("#define RTL8168_VENDOR_ID 0x10ECU", driver)
        self.assertIn("#define RTL8168_DEVICE_ID 0x8168U", driver)
        self.assertIn("pci_register_driver_named(RTL8168_VENDOR_ID",
                         driver)
        self.assertIn("pci_device_exists(0x10EC, 0x8168)", kernel)
        self.assertIn("rtl8168_detect();", kernel)

    def test_driver_uses_fixed_dma_rings_and_32_bit_mmio(self) -> None:
        driver = read("drivers/net/rtl8168.c")
        self.assertIn("RTL8168_TX_RING_COUNT 8U", driver)
        self.assertIn("RTL8168_RX_RING_COUNT 16U", driver)
        self.assertIn("__attribute__((aligned(256)))", driver)
        self.assertIn("RTL8168_RX_DESC_LOW", driver)
        self.assertIn("RTL8168_TX_DESC_LOW", driver)
        self.assertIn("map_mmio_region", driver)
        self.assertIn("address_high = 0U", driver)
        self.assertIn("pci_set_bus_master", driver)

    def test_irq_is_deferred_and_polling_has_a_bound(self) -> None:
        driver = read("drivers/net/rtl8168.c")
        irq_start = driver.index("static void rtl8168_interrupt_handler")
        irq_end = driver.index("static void rtl8168_enable", irq_start)
        irq_handler = driver[irq_start:irq_end]
        self.assertIn("rtl8168_rx_pending", irq_handler)
        self.assertNotIn("netdev_deliver_rx", irq_handler)
        self.assertIn("RTL8168_POLL_LIMIT", driver)
        self.assertIn("RTL8168_RX_RING_COUNT", driver[driver.index(
            "void rtl8168_poll_rx"):])
        self.assertIn("netdev_deliver_rx", driver[driver.index(
            "void rtl8168_poll_rx"):])

    def test_netdev_selects_and_fences_the_backend(self) -> None:
        netdev = read("drivers/net/netdev.c")
        for symbol in (
            "rtl8168_is_initialized()",
            "rtl8168_send_packet(packet, length)",
            "rtl8168_poll_rx()",
            "rtl8168_fence_outputs()",
            "rtl8168_outputs_fenced()",
            "rtl8168_get_mac_address(mac)",
        ):
            self.assertIn(symbol, netdev)

    def test_driver_fails_closed_on_invalid_bar_or_irq(self) -> None:
        driver = read("drivers/net/rtl8168.c")
        self.assertIn("no usable 32-bit MMIO BAR", driver)
        self.assertIn("invalid legacy IRQ", driver)
        self.assertIn("if (!mmio)", driver)
        self.assertIn("if (!rtl8168_hardware_init())", driver)

    def test_driver_reports_and_gates_on_physical_link(self) -> None:
        driver = read("drivers/net/rtl8168.c")
        self.assertIn("RTL8168_PHY_STATUS", driver)
        self.assertIn("RTL8168_PHY_LINK_STATUS", driver)
        self.assertIn("rtl8168_wait_for_link", driver)
        self.assertIn("rtl8168_refresh_link", driver)
        self.assertIn("bool rtl8168_is_link_up(void)", driver)

    def test_receive_filter_accepts_dhcp_broadcasts(self) -> None:
        driver = read("drivers/net/rtl8168.c")
        self.assertIn("#define RTL8168_RX_ACCEPT_BROADCAST 0x08U", driver)
        rx_config = driver[driver.index(
            "rtl8168_write32(RTL8168_RX_CONFIG,"):]
        rx_config = rx_config[:rx_config.index(");")]
        self.assertIn("RTL8168_RX_ACCEPT_BROADCAST", rx_config)

    def test_receive_descriptors_publish_the_buffer_capacity(self) -> None:
        driver = read("drivers/net/rtl8168.c")
        start = driver.index("static void rtl8168_arm_rx_descriptor(")
        body = driver[start:driver.index("\n}", start)]
        self.assertIn("RTL8168_DESC_OWN | ring_end", body)
        self.assertIn(
            "RTL8168_RX_BUFFER_SIZE & RTL8168_DESC_LENGTH_MASK", body)


if __name__ == "__main__":
    unittest.main()
