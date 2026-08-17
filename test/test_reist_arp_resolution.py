import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class ReistArpResolutionContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.netstack = (ROOT / "drivers/net/netstack.c").read_text()
        cls.supervisor = (ROOT / "kernel/init/supervisor.c").read_text()
        cls.service = (ROOT / "userspace/programs/reist_probe.c").read_text()
        cls.syscalls = (ROOT / "kernel/syscall/syscall_table.c").read_text()
        cls.sdk = (ROOT / "userspace/sdk/include/x86os.h").read_text()
        cls.runner = (ROOT / "scripts/run_qemu_smoke.py").read_text()
        cls.runtime = (ROOT / "scripts/test-reist-runtime.ps1").read_text()

    def test_cache_miss_is_mediated(self):
        body = self.netstack.split("void arp_send_request", 1)[1].split("}", 1)[0]
        self.assertIn("supervisor_network_request_arp_resolution", body)
        self.assertNotIn("arp_send_request_now", body)

    def test_resolution_has_protected_single_use_context(self):
        self.assertIn("arp_resolution_authority", self.supervisor)
        self.assertIn("arp_resolution_context", self.supervisor)
        self.assertIn("supervisor_protected_probe_authority_take_epoch", self.supervisor)

    def test_service_validates_fixed_neta_request(self):
        self.assertIn("request.payload[3] == 'A'", self.service)
        self.assertIn("request.length != 22U", self.service)
        self.assertIn("x86os_reist_send_arp_request", self.service)

    def test_abi_is_append_only_and_versioned(self):
        self.assertIn("X86OS_SYS_REIST_ARP_RESOLUTION = 64", self.sdk)
        self.assertIn("X86OS_SYS_NETWORK_ARP_RESOLVE = 65", self.sdk)
        self.assertIn("X86OS_REIST_ARP_RESOLUTION_VERSION 1U", self.sdk)
        self.assertIn("case SYS_REIST_ARP_RESOLUTION", self.syscalls)
        self.assertIn("sizeof(supervisor_arp_resolution_t) == 16U", self.syscalls)

    def test_failure_does_not_restore_legacy_transmit(self):
        request = self.supervisor.split(
            "bool supervisor_network_request_arp_resolution", 1
        )[1].split("int supervisor_network_send_arp_request", 1)[0]
        self.assertNotIn("netstack_send_arp_request", request)

    def test_runtime_requires_real_outgoing_arp_frame(self):
        self.assertIn("--expect-arp-resolution", self.runner)
        self.assertIn("receive_arp_request", self.runner)
        self.assertIn("ARP_RESOLUTION_MEDIATED", self.runner)
        self.assertIn("'arp-resolution'", self.runtime)


if __name__ == "__main__":
    unittest.main()
