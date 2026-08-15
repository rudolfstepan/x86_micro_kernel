from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


class ReistDhcpIngressTests(unittest.TestCase):
    def test_append_only_abi_is_fixed_and_probe_only(self) -> None:
        self.assertIn("SYS_REIST_DHCP_INGRESS 81",
                      read("lib/libc/stdlib.h"))
        self.assertIn("X86OS_SYS_REIST_DHCP_INGRESS = 81",
                      read("userspace/sdk/include/x86os.h"))
        self.assertIn("sizeof(x86os_reist_dhcp_ingress_t) == 52U",
                      read("userspace/sdk/x86os.c"))
        self.assertIn("case SYS_REIST_DHCP_INGRESS",
                      read("kernel/syscall/syscall_table.c"))
        self.assertIn("SYS_REIST_DHCP_INGRESS",
                      read("kernel/proc/process.c"))

    def test_service_submits_only_a_successfully_parsed_frame(self) -> None:
        service = read("examples/userspace/reist_probe.c")
        self.assertLess(service.index("reist_dhcp_parse_frame"),
                        service.index("x86os_reist_dhcp_ingress"))
        self.assertIn(".frame_crc32 = frame_crc32", service)
        self.assertIn("dhcp_ingress_consumed", service)
        self.assertIn("raw_udp_delivery && !dhcp_ingress_consumed", service)

    def test_ingress_is_delivery_generation_and_transaction_scoped(self) -> None:
        supervisor = read("kernel/init/supervisor.c")
        start = supervisor.index("int supervisor_network_dhcp_ingress(")
        body = supervisor[start:supervisor.index(
            "int supervisor_network_udp_bind(", start)]
        for contract in (
            "pid == control.pid", "generation == control.process_generation",
            "delivery.frame_crc32 != ingress->frame_crc32",
            "pit_monotonic_ms() >= delivery.deadline_ms",
            "dhcp_transaction_wire_value(renewal.transaction_id)",
            "ingress->client_mac[index] != local_mac[index]",
        ):
            self.assertIn(contract, body)
        self.assertLess(body.index("udp_delivery_clear"),
                        body.index("netstack_finish_supervised_dhcp_request"))

    def test_ring0_renewal_demux_is_suppressed_while_service_owns_it(self) -> None:
        netdev = read("drivers/net/netdev.c")
        supervisor = read("kernel/init/supervisor.c")
        self.assertIn("supervisor_network_dhcp_service_owns_ingress()",
                      netdev)
        worker = supervisor[supervisor.index("static void supervisor_worker"):
                            supervisor.index("bool supervisor_start_worker")]
        self.assertIn("!supervisor_network_dhcp_service_owns_ingress()",
                      worker)
        self.assertIn("netstack_dhcp_poll()", worker)

    def test_runtime_requires_ring3_renewal_ingress(self) -> None:
        runner = read("scripts/run_qemu_smoke.py")
        self.assertIn("REIST_NETWORK DHCP_RENEW_INGRESS_RING3", runner)
        self.assertIn("DHCP_RENEW_INGRESS_RING3_MARKER", runner)


if __name__ == "__main__":
    unittest.main()
