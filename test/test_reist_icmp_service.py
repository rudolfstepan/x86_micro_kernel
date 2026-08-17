"""Contracts for bounded Ring-3-mediated ICMP echo replies."""

from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


class ReistIcmpServiceTests(unittest.TestCase):
    def test_echo_request_has_no_ring0_reply_fallback(self) -> None:
        netstack = read("drivers/net/netstack.c")
        self.assertNotIn("static void handle_icmp_packet(", netstack)
        self.assertNotIn("supervisor_network_submit_icmp_echo(", netstack)
        self.assertNotIn("netstack_process_packet", netstack)
        self.assertNotIn("case IP_PROTOCOL_ICMP:", netstack)

    def test_context_is_fixed_protected_and_generation_scoped(self) -> None:
        header = read("include/kernel/supervisor.h")
        supervisor = read("kernel/init/supervisor.c")
        self.assertIn("SUPERVISOR_ICMP_ECHO_MAX_DATA 32U", header)
        self.assertIn("critical_object_t object", header)
        self.assertIn("SUPERVISOR_ICMP_ECHO_CONTEXT_VERSION", supervisor)
        self.assertIn("icmp_echo_authority", supervisor)
        self.assertIn("control.process_generation", supervisor)
        self.assertIn("SUPERVISOR_NETWORK_PROBE_TIMEOUT_MS", supervisor)
        self.assertIn("supervisor_protected_icmp_echo_context_clear(",
                      supervisor)

    def test_service_consumes_authority_before_the_only_transmit(self) -> None:
        supervisor = read("kernel/init/supervisor.c")
        start = supervisor.index("int supervisor_network_send_icmp_echo_reply(")
        body = supervisor[start:supervisor.index("static void supervisor_worker", start)]
        take = body.index("supervisor_protected_probe_authority_take_epoch(")
        clear = body.index("supervisor_protected_icmp_echo_context_clear(")
        unlock = body.index("supervisor_unlock(flags)")
        send = body.index("netstack_send_icmp_echo_reply(")
        self.assertLess(take, clear)
        self.assertLess(clear, unlock)
        self.assertLess(unlock, send)

    def test_append_only_syscall_and_sdk_match(self) -> None:
        libc = read("lib/libc/stdlib.h")
        sdk = read("userspace/sdk/include/x86os.h")
        wrapper = read("userspace/sdk/x86os.c")
        syscall = read("kernel/syscall/syscall_table.c")
        process = read("kernel/proc/process.c")
        self.assertIn("SYS_REIST_ICMP_ECHO_REPLY 72", libc)
        self.assertIn("X86OS_SYS_REIST_ICMP_ECHO_REPLY = 72", sdk)
        self.assertIn("sizeof(x86os_reist_icmp_echo_reply_t) == 16U", wrapper)
        self.assertIn("SYS_REIST_ICMP_ECHO_REPLY", syscall)
        self.assertIn("copy_from_user(&reply", syscall)
        self.assertIn("SYS_REIST_ICMP_ECHO_REPLY", process)

    def test_ingress_is_append_only_protected_and_generation_scoped(self) -> None:
        libc = read("lib/libc/stdlib.h")
        sdk = read("userspace/sdk/include/x86os.h")
        wrapper = read("userspace/sdk/x86os.c")
        syscall = read("kernel/syscall/syscall_table.c")
        supervisor = read("kernel/init/supervisor.c")
        process = read("kernel/proc/process.c")
        self.assertIn("SYS_REIST_ICMP_INGRESS 83", libc)
        self.assertIn("X86OS_SYS_REIST_ICMP_INGRESS = 83", sdk)
        self.assertIn("sizeof(x86os_reist_icmp_ingress_t) == 40U", wrapper)
        self.assertIn("copy_from_user(&ingress", syscall)
        self.assertIn("SYS_REIST_ICMP_INGRESS", process)
        self.assertIn("supervisor_protected_icmp_delivery_t", supervisor)
        self.assertIn("delivery.process_generation != generation", supervisor)
        self.assertIn("delivery.frame_crc32 != ingress->frame_crc32", supervisor)
        self.assertIn("pit_monotonic_ms() >= delivery.deadline_ms", supervisor)
        self.assertIn("icmp_delivery_clear()", supervisor)

    def test_ring3_result_is_only_icmp_ingress_authority_source(self) -> None:
        probe = read("userspace/programs/reist_probe.c")
        supervisor = read("kernel/init/supervisor.c")
        self.assertIn("x86os_reist_icmp_ingress(", probe)
        self.assertIn("X86OS_REIST_ICMP_INGRESS_DROP", probe)
        self.assertIn("supervisor_network_icmp_ingress(", supervisor)
        ingress = supervisor.index("int supervisor_network_icmp_ingress(")
        submit = supervisor.index("supervisor_network_submit_icmp_echo(", ingress)
        self.assertLess(ingress, submit)
        self.assertIn("netstack_accept_validated_icmp_echo_reply", supervisor)

    def test_ring3_parser_validates_and_authorizes_net_i(self) -> None:
        probe = read("userspace/programs/reist_probe.c")
        self.assertIn("message->payload[3] == 'I'", probe)
        self.assertIn("data_length > 32U", probe)
        self.assertIn("message->length != 24U + data_length", probe)
        self.assertIn("x86os_reist_send_icmp_echo_reply(&reply)", probe)

    def test_runtime_injects_and_observes_real_echo_reply(self) -> None:
        runner = read("scripts/run_qemu_smoke.py")
        runtime = read("scripts/test-reist-runtime.ps1")
        self.assertIn("def icmp_echo_request_frame()", runner)
        self.assertIn("def receive_icmp_echo_reply(", runner)
        self.assertIn('"--inject-icmp-echo"', runner)
        self.assertIn("REIST_DHCP_CONFIG_MARKER, deadline", runner)
        self.assertIn("'icmp-echo'", runtime)
        self.assertIn("'--nic', 'rtl8139', '--inject-icmp-echo'", runtime)


if __name__ == "__main__":
    unittest.main()
