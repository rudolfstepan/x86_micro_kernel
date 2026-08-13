import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class ReistNetworkDomainTests(unittest.TestCase):
    def read(self, path):
        return (ROOT / path).read_text(encoding="utf-8")

    def test_network_tx_is_a_real_supervised_domain(self):
        source = self.read("drivers/net/netdev.c")
        self.assertIn('supervisor_register("network-tx"', source)
        self.assertIn("NETDEV_TX_DEADLINE_MS 250U", source)
        self.assertIn(".restart_budget = 0", source)
        self.assertIn("supervisor_report_idle(netdev_supervisor_handle)", source)
        self.assertIn("uint64_t netdev_progress_marker", source)

    def test_kernel_fails_closed_if_registration_fails(self):
        kernel = self.read("kernel/init/kernel.c")
        self.assertIn("if (!netdev_supervision_init(pit_monotonic_ms()))", kernel)
        self.assertIn('panic("Unable to supervise network transmit domain")', kernel)

    def test_fence_verification_reads_each_device(self):
        netdev = self.read("drivers/net/netdev.c")
        for name in ("e1000_outputs_fenced()", "rtl8139_outputs_fenced()",
                     "ne2000_outputs_fenced()"):
            self.assertIn(name, netdev)
        self.assertIn("E1000_REG_TCTL", self.read("drivers/net/e1000.c"))
        self.assertIn("RTL_CMD_TX_ENABLE", self.read("drivers/net/rtl8139.c"))
        self.assertIn("command & CR_STP", self.read("drivers/net/ne2000.c"))

    def test_guest_runner_can_attach_a_supervised_nic(self):
        runner = self.read("scripts/run_qemu_smoke.py")
        self.assertIn('"--nic", choices=("none", "rtl8139", "e1000")', runner)
        self.assertIn('f"{nic},netdev=reistnet0"', runner)


if __name__ == "__main__":
    unittest.main()
