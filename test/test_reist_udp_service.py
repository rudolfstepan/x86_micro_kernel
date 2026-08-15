"""Contracts for bounded generation-scoped Ring-3 UDP bindings."""

from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


class ReistUdpServiceTests(unittest.TestCase):
    def test_kernel_accepts_only_checked_bound_service_datagrams(self) -> None:
        netstack = read("drivers/net/netstack.c")
        start = netstack.index("static void handle_udp_packet(")
        body = netstack[start:netstack.index("// UDP low-level", start)]
        self.assertIn("udp->checksum == 0U", body)
        self.assertIn("udp_checksum_valid", body)
        self.assertIn("supervisor_network_submit_udp", body)

    def test_context_authority_and_bindings_are_protected_and_bounded(self) -> None:
        header = read("include/kernel/supervisor.h")
        supervisor = read("kernel/init/supervisor.c")
        self.assertIn("SUPERVISOR_UDP_ECHO_MAX_DATA 32U", header)
        self.assertIn("SUPERVISOR_UDP_MAX_BINDINGS 4U", header)
        self.assertIn("supervisor_protected_udp_binding_t", header)
        self.assertIn("supervisor_protected_udp_echo_context_t", header)
        self.assertIn("udp_bindings[SUPERVISOR_UDP_MAX_BINDINGS]", supervisor)
        self.assertIn("SUPERVISOR_NETWORK_PROBE_TIMEOUT_MS", supervisor)

    def test_bindings_are_generation_scoped_unique_and_revoked_on_fence(self) -> None:
        supervisor = read("kernel/init/supervisor.c")
        bind = supervisor[supervisor.index("int supervisor_network_udp_bind("):
                          supervisor.index("int supervisor_network_udp_unbind(")]
        self.assertIn("bindings[available].generation + 1U", bind)
        self.assertIn("bindings[slot].port == request->port", bind)
        self.assertIn("bindings[slot].generation < 0xFFFFFFU", bind)
        fence = supervisor[supervisor.index("static bool probe_fence_apply("):
                           supervisor.index("static void probe_report_recovery_pair")]
        self.assertIn("SUPERVISOR_UDP_MAX_BINDINGS", fence)
        self.assertIn("binding.process_generation = 0U", fence)
        self.assertIn("udp_binding_write", fence)

    def test_reply_consumes_state_before_the_only_send(self) -> None:
        supervisor = read("kernel/init/supervisor.c")
        start = supervisor.index("int supervisor_network_send_udp_reply(")
        end = supervisor.index("bool supervisor_network_submit_udp_echo(", start)
        body = supervisor[start:end]
        self.assertLess(body.index("probe_authority_take_epoch"),
                        body.index("udp_echo_context_clear"))
        self.assertLess(body.index("udp_echo_context_clear"),
                        body.index("supervisor_unlock(flags)"))
        self.assertLess(body.index("supervisor_unlock(flags)"),
                        body.index("netstack_send_supervised_udp_reply"))

    def test_append_only_syscalls_and_default_deny_profile_match(self) -> None:
        libc = read("lib/libc/stdlib.h")
        sdk = read("userspace/sdk/include/x86os.h")
        dispatch = read("kernel/syscall/syscall_table.c")
        profile = read("kernel/proc/process.c")
        for item in ("SYS_REIST_UDP_BIND 75", "SYS_REIST_UDP_UNBIND 76",
                     "SYS_REIST_UDP_REPLY 77"):
            self.assertIn(item, libc)
        for item in ("X86OS_SYS_REIST_UDP_BIND = 75",
                     "X86OS_SYS_REIST_UDP_UNBIND = 76",
                     "X86OS_SYS_REIST_UDP_REPLY = 77"):
            self.assertIn(item, sdk)
        for name in ("SYS_REIST_UDP_BIND", "SYS_REIST_UDP_UNBIND",
                     "SYS_REIST_UDP_REPLY"):
            self.assertIn("case " + name, dispatch)
            self.assertIn(name, profile)

    def test_ring3_revalidates_exact_netv_message_before_reply(self) -> None:
        probe = read("examples/userspace/reist_probe.c")
        self.assertIn("message->payload[3] == 'V'", probe)
        self.assertIn("udp_proposal_valid(message)", probe)
        self.assertIn("destination_port >= 1024U", probe)
        self.assertIn("message->length == 28U + data_length", probe)
        self.assertIn("x86os_reist_udp_reply(&reply)", probe)
        self.assertIn("const uint16_t udp_ports[4] = {9000U, 9001U, 9002U, 9003U}",
                      probe)
        self.assertIn("!= -28", probe)
        self.assertIn("!= -17", probe)
        self.assertIn("x86os_reist_udp_unbind(stale_binding) != -9", probe)

    def test_no_direct_udp_reply_exists_in_receive_path(self) -> None:
        netstack = read("drivers/net/netstack.c")
        start = netstack.index("static void handle_udp_packet(")
        body = netstack[start:netstack.index("// UDP low-level", start)]
        self.assertNotIn("netstack_send_supervised_udp_reply", body)
        self.assertNotIn("netstack_send_udp_echo_reply", body)

    def test_runtime_contract_covers_a_second_bound_port(self) -> None:
        runtime = read("scripts/test-reist-runtime.ps1")
        runner = read("scripts/run_qemu_smoke.py")
        self.assertIn("'udp-bindings'", runtime)
        self.assertIn("'--udp-port', '9001'", runtime)
        self.assertIn("REIST_UDP_DATAGRAM_QUEUED_MARKER", runner)
        self.assertIn("source_port_expected", runner)


if __name__ == "__main__":
    unittest.main()
