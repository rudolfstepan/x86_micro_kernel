import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


class ReistArpReplyTests(unittest.TestCase):
    def test_append_only_versioned_reply_abi_is_default_denied(self):
        libc = read("lib/libc/stdlib.h")
        sdk = read("userspace/sdk/include/x86os.h")
        sdk_c = read("userspace/sdk/x86os.c")
        process = read("kernel/proc/process.c")
        syscall = read("kernel/syscall/syscall_table.c")
        guest = read("examples/userspace/guest_test.c")
        self.assertIn("SYS_REIST_ARP_REPLY 63", libc)
        self.assertIn("X86OS_SYS_REIST_ARP_REPLY = 63", sdk)
        self.assertIn("sizeof(x86os_reist_arp_reply_t) == 24U", sdk_c)
        self.assertIn("SYS_REIST_ARP_REPLY", process)
        body = syscall[syscall.index("static int syscall_reist_arp_reply("):
                       syscall.index("static int syscall_service_connect(")]
        self.assertLess(body.index("user_range_accessible"),
                        body.index("copy_from_user"))
        self.assertLess(body.index("copy_from_user"),
                        body.index("supervisor_network_send_arp_reply"))
        self.assertIn("x86os_reist_send_arp_reply(&unauthorized_reply) != -13",
                      guest)

    def test_local_arp_requests_are_exclusively_service_owned(self):
        supervisor = read("kernel/init/supervisor.c")
        netdev = read("drivers/net/netdev.c")
        netstack = read("drivers/net/netstack.c")
        handoff = supervisor[
            supervisor.index("bool supervisor_network_submit_header("):
            supervisor.index("int supervisor_network_probe_request(")]
        self.assertIn("bool local_request", handoff)
        self.assertIn("return local_request;", handoff)
        self.assertIn(".payload = {'N', 'E', 'T', 'Q'}", handoff)
        self.assertIn("return true;", handoff)
        self.assertIn("if (!service_owned) netdev_queue_rx_packet", netdev)
        arp = netstack[netstack.index("static void handle_arp_packet("):
                       netstack.index("// IPv4/ICMP")]
        self.assertNotIn("arp_send_reply", arp)

    def test_reply_authority_is_bounded_protected_and_generation_scoped(self):
        header = read("include/kernel/supervisor.h")
        supervisor = read("kernel/init/supervisor.c")
        host = read("test/test_supervisor_host.c")
        self.assertIn("supervisor_protected_arp_reply_context_t", header)
        self.assertIn("SUPERVISOR_ARP_REPLY_CONTEXT_VERSION", supervisor)
        self.assertIn("SUPERVISOR_NETWORK_PROBE_TIMEOUT_MS", supervisor)
        mediator = supervisor[
            supervisor.index("int supervisor_network_send_arp_reply("):
            supervisor.index("static void supervisor_worker(")]
        self.assertIn("generation != control.process_generation", mediator)
        self.assertIn("context.transaction_epoch != generation", mediator)
        self.assertIn("supervisor_protected_probe_authority_take_epoch(",
                      mediator)
        self.assertLess(mediator.index(
                            "supervisor_protected_probe_authority_take_epoch("),
                        mediator.index("netstack_send_arp_reply("))
        self.assertIn("supervisor_test_corrupt_arp_reply_context", host)
        self.assertIn("SUPERVISOR_EINTEGRITY", host)

    def test_ring3_validates_request_before_mediated_output(self):
        service = read("examples/userspace/reist_probe.c")
        q_parser = service[service.index("if (message->payload[3] == 'Q')"):
                           service.index("} else if (message->payload[3] != '1')")]
        self.assertIn("message->payload[25] != 1U", q_parser)
        self.assertIn("request_id == 0U", q_parser)
        self.assertIn("destination_is_broadcast", q_parser)
        self.assertIn("target_mac_zero", q_parser)
        self.assertIn("target_mac_local", q_parser)
        self.assertIn("x86os_reist_send_arp_reply(&reply)", service)


if __name__ == "__main__":
    unittest.main()
