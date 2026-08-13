import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class ReistOutputFenceTests(unittest.TestCase):
    def read(self, path):
        return (ROOT / path).read_text(encoding="utf-8")

    def test_fence_core_is_static_latched_and_bounded(self):
        source = self.read("kernel/init/output_fence.c")
        header = self.read("include/kernel/output_fence.h")
        self.assertIn("OUTPUT_FENCE_MAX_HANDLERS 8U", header)
        self.assertRegex(source, r"handlers\[OUTPUT_FENCE_MAX_HANDLERS\]")
        self.assertIn("__sync_lock_test_and_set(&fence_active, 1U)", source)
        for forbidden in ("k_malloc", "printf(", "vfs_", "while ("):
            self.assertNotIn(forbidden, source)

    def test_fatal_fences_before_record_and_watchdog(self):
        source = self.read("kernel/init/fatal.c")
        body = source[source.index("fatal_emergency_handoff"):]
        self.assertLess(body.index("output_fence_all()"),
                        body.index("persistent_record()"))
        self.assertLess(body.index("output_fence_all()"),
                        body.index("watchdog_fatal_handoff()"))

    def test_network_fence_is_registered_at_boot(self):
        source = self.read("kernel/init/kernel.c")
        self.assertIn("output_fence_init();", source)
        self.assertIn("output_fence_register(netdev_fence_outputs)", source)

    def test_all_network_transmit_paths_fail_closed(self):
        netdev = self.read("drivers/net/netdev.c")
        self.assertRegex(netdev, r"netdev_send\([^)]*\)\s*\{\s*if \(netdev_tx_fenced\)")
        for path, function in (
            ("drivers/net/e1000.c", "e1000_send_packet"),
            ("drivers/net/rtl8139.c", "rtl8139_send_packet"),
            ("drivers/net/ne2000.c", "ne2000_send_packet"),
        ):
            source = self.read(path)
            body = source[source.index("bool " + function):]
            self.assertLess(body.index("netdev_outputs_fenced()"), 200)

    def test_hardware_transmit_is_quiesced(self):
        self.assertIn("~E1000_TCTL_EN", self.read("drivers/net/e1000.c"))
        self.assertIn("~RTL_CMD_TX_ENABLE", self.read("drivers/net/rtl8139.c"))
        ne2000 = self.read("drivers/net/ne2000.c")
        fence = ne2000[ne2000.index("void ne2000_fence_outputs"):]
        self.assertIn("NE2000_IMR", fence)
        self.assertIn("CR_STP | CR_RD2", fence)


if __name__ == "__main__":
    unittest.main()
