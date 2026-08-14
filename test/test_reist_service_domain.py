import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


class ReistServiceDomainTests(unittest.TestCase):
    def test_connect_abi_is_append_only_and_pointer_checked_first(self):
        libc = read("lib/libc/stdlib.h")
        sdk = read("userspace/sdk/include/x86os.h")
        syscall = read("kernel/syscall/syscall_table.c")
        self.assertIn("SYS_SERVICE_CONNECT 57", libc)
        self.assertIn("X86OS_SYS_SERVICE_CONNECT = 57", sdk)
        start = syscall.index("static int syscall_service_connect(")
        end = syscall.index("\n}", start)
        body = syscall[start:end]
        self.assertLess(body.index("user_range_accessible"),
                        body.index("supervisor_service_connect"))
        self.assertIn("copy_to_user_space", body)

    def test_directory_delegation_is_generation_scoped_and_attenuated(self):
        supervisor = read("kernel/init/supervisor.c")
        process = read("kernel/proc/process.c")
        self.assertIn("process_identity_alive(probe_runtime.pid", supervisor)
        self.assertIn("probe_runtime.process_generation", supervisor)
        self.assertIn("IPC_RIGHT_SEND | IPC_RIGHT_RECEIVE", supervisor)
        self.assertNotIn("IPC_RIGHT_CONTROL);", supervisor[
            supervisor.index("int supervisor_service_connect("):
            supervisor.index("static void supervisor_worker(")])
        self.assertIn("candidate->generation == source_generation", process)

    def test_real_ring3_service_has_bounded_request_reply(self):
        service = read("examples/userspace/reist_probe.c")
        guest = read("examples/userspace/guest_test.c")
        self.assertIn('message_is(&request, "DIAG")', service)
        self.assertIn('"REIST_DIAG_OK"', service)
        self.assertIn("x86os_ipc_receive_timeout(endpoint, &request, 40U)",
                      service)
        self.assertIn("x86os_ipc_send_timeout(endpoint, &response, 100U)",
                      service)
        self.assertIn("x86os_service_connect", guest)
        self.assertIn("TEST_STAGE DIAGNOSTIC_SERVICE_OK", guest)

    def test_service_is_unavailable_until_reintegrated(self):
        supervisor = read("kernel/init/supervisor.c")
        connect = supervisor[supervisor.index("int supervisor_service_connect("):
                             supervisor.index("static void supervisor_worker(")]
        for condition in ("probe_runtime.fenced", "!probe_runtime.healthy",
                          "probe_runtime.launch_count < 4U"):
            self.assertIn(condition, connect)

    def test_client_release_is_distinct_from_owner_destroy(self):
        ipc = read("kernel/ipc/ipc.c")
        sdk = read("userspace/sdk/include/x86os.h")
        guest = read("examples/userspace/guest_test.c")
        self.assertIn("int ipc_release(Process *process", ipc)
        start = ipc.index("int ipc_release(Process *process")
        release = ipc[start:ipc.index("void ipc_process_cleanup(", start)]
        self.assertIn("IPC_EACCES", release)
        self.assertIn("clear_capability_record_locked(record)", release)
        self.assertIn("X86OS_SYS_IPC_RELEASE = 58", sdk)
        self.assertGreaterEqual(guest.count("x86os_service_connect("), 4)
        self.assertGreaterEqual(guest.count("x86os_ipc_release(handle)"), 2)

    def test_bounded_network_parser_runs_in_ring3(self):
        service = read("examples/userspace/reist_probe.c")
        guest = read("examples/userspace/guest_test.c")
        runner = read("scripts/run_qemu_smoke.py")
        self.assertIn("static const char *network_classification(", service)
        self.assertIn("message->length < 18U", service)
        self.assertIn('return "REIST_NET_ARP";', service)
        self.assertIn('return "REIST_NET_IPV4";', service)
        self.assertNotIn("k_malloc", service)
        self.assertIn("ipc_message_set_test_arp_frame", guest)
        self.assertIn("TEST_STAGE NETWORK_PARSER_OK", guest)
        self.assertIn("REIST_NETWORK_MARKER", runner)

    def test_real_rx_header_handoff_is_bounded_and_peer_routed(self):
        netdev = read("drivers/net/netdev.c")
        supervisor = read("kernel/init/supervisor.c")
        ipc = read("kernel/ipc/ipc.c")
        self.assertIn("uint8_t service_header[14U]", netdev)
        self.assertIn("memcpy(service_header, packet", netdev)
        self.assertIn("supervisor_network_submit_header(service_header",
                      netdev)
        self.assertIn(".length = 18U", supervisor)
        handoff = supervisor[
            supervisor.index("bool supervisor_network_submit_header("):
            supervisor.index("static void supervisor_worker(")]
        self.assertIn("KASSERT_NOT_IRQ();", handoff)
        self.assertIn("KASSERT(irq_enabled());", handoff)
        self.assertIn("frame[12] != 0x08U", handoff)
        self.assertIn("frame[13] != 0x06U", handoff)
        self.assertIn("for (uint32_t index = 0U; index < 14U; ++index)",
                      supervisor)
        self.assertIn("ipc_send_external_from_peer(", supervisor)
        self.assertIn("if (endpoint->count >= IPC_QUEUE_DEPTH)", ipc)
        self.assertIn("peer->holder_pid", ipc)
        self.assertNotIn("ipc_send_timeout(", handoff)

    def test_real_nic_probe_is_service_scoped_and_rate_limited(self):
        supervisor = read("kernel/init/supervisor.c")
        service = read("examples/userspace/reist_probe.c")
        guest = read("examples/userspace/guest_test.c")
        sdk = read("userspace/sdk/include/x86os.h")
        probe = supervisor[
            supervisor.index("int supervisor_network_probe_request("):
            supervisor.index("static void supervisor_worker(")]
        self.assertIn("pid != probe_runtime.pid", probe)
        self.assertIn("generation != probe_runtime.process_generation", probe)
        self.assertIn("< 250U", probe)
        self.assertIn("netstack_probe_gateway()", probe)
        self.assertIn("X86OS_SYS_NETWORK_PROBE = 59", sdk)
        self.assertIn('message_is(&request, "NETPROBE")', service)
        self.assertIn("request.payload[3] == 'R'", service)
        self.assertIn("REIST_REPORT_NETWORK_HEADER", service)
        self.assertIn("TEST_STAGE NETWORK_HANDOFF_OK", guest)
        worker = supervisor[
            supervisor.index("static void supervisor_worker("):
            supervisor.index("bool supervisor_start_worker(")]
        self.assertIn("netdev_poll();", worker)
        self.assertIn("scheduler_sleep_ms(SUPERVISOR_CHECK_INTERVAL_MS)",
                      worker)


if __name__ == "__main__":
    unittest.main()
