"""Contracts for the bounded Ring-3-driven DHCP boot transaction."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


class ReistDhcpBootTests(unittest.TestCase):
    def test_append_only_boot_start_abi_is_probe_only(self) -> None:
        self.assertIn("SYS_REIST_DHCP_BOOT_START 82",
                      read("lib/libc/stdlib.h"))
        self.assertIn("X86OS_SYS_REIST_DHCP_BOOT_START = 82",
                      read("userspace/sdk/include/x86os.h"))
        self.assertIn("case SYS_REIST_DHCP_BOOT_START",
                      read("kernel/syscall/syscall_table.c"))
        self.assertIn("SYS_REIST_DHCP_BOOT_START",
                      read("kernel/proc/process.c"))

    def test_boot_transaction_is_protected_and_bounded(self) -> None:
        header = read("include/kernel/supervisor.h")
        supervisor = read("kernel/init/supervisor.c")
        for contract in (
            "supervisor_protected_dhcp_boot_t",
            "SUPERVISOR_DHCP_BOOT_DISCOVER_SENT",
            "SUPERVISOR_DHCP_BOOT_REQUEST_SENT",
            "SUPERVISOR_DHCP_BOOT_TIMEOUT_MS",
        ):
            self.assertIn(contract, header + supervisor)
        start = supervisor.index("int supervisor_network_start_dhcp_boot(")
        body = supervisor[start:supervisor.index(
            "int supervisor_network_dhcp_ingress(", start)]
        self.assertNotIn("while (", body)
        self.assertNotIn("pit_delay", body)
        self.assertIn("netstack_send_supervised_dhcp_discover", body)

    def test_ring3_service_drives_three_finite_attempts(self) -> None:
        probe = read("examples/userspace/reist_probe.c")
        self.assertIn("REIST_DHCP_BOOT_MAX_ATTEMPTS 3U", probe)
        self.assertIn("x86os_reist_start_dhcp_boot", probe)
        self.assertIn("dhcp_boot_attempts < REIST_DHCP_BOOT_MAX_ATTEMPTS",
                      probe)

    def test_offer_select_and_ack_are_ring3_mediated(self) -> None:
        supervisor = read("kernel/init/supervisor.c")
        ingress = supervisor[supervisor.index(
            "int supervisor_network_dhcp_ingress("):]
        self.assertIn("netstack_send_supervised_dhcp_select", ingress)
        self.assertIn("SUPERVISOR_DHCP_MESSAGE_OFFER", ingress)
        self.assertIn("SUPERVISOR_DHCP_MESSAGE_ACK", ingress)
        self.assertIn("REIST_NETWORK DHCP_BOOT_OFFER_RING3", ingress)
        self.assertIn("REIST_NETWORK DHCP_BOOT_ACK_RING3", ingress)

    def test_kernel_no_longer_calls_synchronous_boot_client(self) -> None:
        kernel = read("kernel/init/kernel.c")
        start = kernel.index("static void configure_network_after_service(")
        body = kernel[start:kernel.index("#ifdef REIST_HANDOVER", start)]
        self.assertNotIn("netstack_configure_dhcp", body)
        self.assertIn("netstack_is_configured", body)
        self.assertIn("pit_monotonic_ms() < commit_deadline", body)

    def test_legacy_ring0_dhcp_client_is_absent(self) -> None:
        source = read("drivers/net/netstack.c") + read("drivers/net/netdev.c")
        for legacy in (
            "netstack_configure_dhcp", "netstack_dhcp_poll",
            "dhcp_discover_request", "dhcp_parse_opts", "dhcp_queue",
        ):
            self.assertNotIn(legacy, source)


if __name__ == "__main__":
    unittest.main()
