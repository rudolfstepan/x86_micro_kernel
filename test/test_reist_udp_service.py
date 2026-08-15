"""Contracts for the bounded Ring-3-mediated UDP echo slice."""

from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


class ReistUdpServiceTests(unittest.TestCase):
    def test_kernel_accepts_only_checked_bounded_service_datagrams(self) -> None:
        netstack = read("drivers/net/netstack.c")
        start = netstack.index("static void handle_udp_packet(")
        body = netstack[start:netstack.index("// UDP low-level", start)]
        self.assertIn("destination_port != SUPERVISOR_UDP_ECHO_PORT", body)
        self.assertIn("udp->checksum == 0U", body)
        self.assertIn("udp_checksum_valid", body)
        self.assertIn("supervisor_network_submit_udp_echo", body)

    def test_context_and_authority_are_protected_and_bounded(self) -> None:
        header = read("include/kernel/supervisor.h")
        supervisor = read("kernel/init/supervisor.c")
        self.assertIn("SUPERVISOR_UDP_ECHO_MAX_DATA 32U", header)
        self.assertIn("supervisor_protected_udp_echo_context_t", header)
        self.assertIn("udp_echo_authority", supervisor)
        self.assertIn("SUPERVISOR_NETWORK_PROBE_TIMEOUT_MS", supervisor)
        self.assertIn("supervisor_protected_udp_echo_context_clear", supervisor)

    def test_reply_consumes_state_before_the_only_send(self) -> None:
        supervisor = read("kernel/init/supervisor.c")
        start = supervisor.index("int supervisor_network_send_udp_echo_reply(")
        body = supervisor[start:supervisor.index("static void supervisor_worker", start)]
        self.assertLess(body.index("probe_authority_take_epoch"),
                        body.index("udp_echo_context_clear"))
        self.assertLess(body.index("udp_echo_context_clear"),
                        body.index("supervisor_unlock(flags)"))
        self.assertLess(body.index("supervisor_unlock(flags)"),
                        body.index("netstack_send_udp_echo_reply"))

    def test_append_only_syscall_and_default_deny_profile_match(self) -> None:
        self.assertIn("SYS_REIST_UDP_ECHO_REPLY 74",
                      read("lib/libc/stdlib.h"))
        self.assertIn("X86OS_SYS_REIST_UDP_ECHO_REPLY = 74",
                      read("userspace/sdk/include/x86os.h"))
        self.assertIn("case SYS_REIST_UDP_ECHO_REPLY",
                      read("kernel/syscall/syscall_table.c"))
        self.assertIn("SYS_REIST_UDP_ECHO_REPLY", read("kernel/proc/process.c"))

    def test_ring3_revalidates_exact_netu_message_before_reply(self) -> None:
        probe = read("examples/userspace/reist_probe.c")
        self.assertIn("message->payload[3] == 'U'", probe)
        self.assertIn("udp_echo_proposal_valid(message)", probe)
        self.assertIn("destination_port == 9000U", probe)
        self.assertIn("message->length == 24U + data_length", probe)
        self.assertIn("x86os_reist_send_udp_echo_reply(&reply)", probe)

    def test_no_direct_udp_echo_reply_exists_in_receive_path(self) -> None:
        netstack = read("drivers/net/netstack.c")
        start = netstack.index("static void handle_udp_packet(")
        body = netstack[start:netstack.index("// UDP low-level", start)]
        self.assertNotIn("netstack_send_udp_echo_reply", body)


if __name__ == "__main__":
    unittest.main()
