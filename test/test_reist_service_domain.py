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
        self.assertIn("process_identity_alive(control.pid", supervisor)
        self.assertIn("control.process_generation", supervisor)
        self.assertIn("supervisor_protected_probe_control_read(", supervisor)
        self.assertIn("IPC_RIGHT_SEND | IPC_RIGHT_RECEIVE", supervisor)
        self.assertNotIn("IPC_RIGHT_CONTROL);", supervisor[
            supervisor.index("int supervisor_service_connect("):
            supervisor.index("static void supervisor_worker(")])
        self.assertIn("candidate->generation == source_generation", process)

    def test_real_ring3_service_has_bounded_request_reply(self):
        service = read("userspace/programs/reist_probe.c")
        guest = read("userspace/programs/guest_test.c")
        self.assertIn('message_request_is(&request, "DIAG")', service)
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
        for condition in ("control.fenced != 0U", "control.healthy == 0U",
                          "control.launch_count < 4U"):
            self.assertIn(condition, connect)

    def test_client_release_is_distinct_from_owner_destroy(self):
        ipc = read("kernel/ipc/ipc.c")
        sdk = read("userspace/sdk/include/x86os.h")
        guest = read("userspace/programs/guest_test.c")
        self.assertIn("int ipc_release(Process *process", ipc)
        start = ipc.index("int ipc_release(Process *process")
        release = ipc[start:ipc.index("void ipc_process_cleanup(", start)]
        self.assertIn("IPC_EACCES", release)
        self.assertIn("clear_capability_record_locked(record)", release)
        self.assertIn("X86OS_SYS_IPC_RELEASE = 58", sdk)
        self.assertGreaterEqual(guest.count("x86os_service_connect("), 4)
        self.assertGreaterEqual(guest.count("x86os_ipc_release(handle)"), 2)

    def test_bounded_network_parser_runs_in_ring3(self):
        service = read("userspace/programs/reist_probe.c")
        guest = read("userspace/programs/guest_test.c")
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
        self.assertIn("uint8_t service_header[42U]", netdev)
        self.assertIn("memcpy(service_header, packet", netdev)
        self.assertIn("supervisor_network_submit_header(", netdev)
        self.assertIn("supervisor_network_submit_header(service_header,", netdev)
        self.assertIn("sizeof(service_header)", netdev)
        self.assertIn("netdev_queue_service_packet(packet, length)", netdev)
        self.assertNotIn("netdev_queue_rx_packet", netdev)
        self.assertIn(".length = 64U", supervisor)
        handoff = supervisor[
            supervisor.index("bool supervisor_network_submit_header("):
            supervisor.index("static void supervisor_worker(")]
        self.assertIn("KASSERT_NOT_IRQ();", handoff)
        self.assertIn("KASSERT(irq_enabled());", handoff)
        self.assertIn("frame[12] != 0x08U", handoff)
        self.assertIn("frame[13] != 0x06U", handoff)
        self.assertNotIn("network_probe_authority.active_id", handoff)
        self.assertIn("supervisor_protected_probe_authority_take_epoch(",
                      handoff)

    def test_queue_pressure_consumes_probe_and_falls_back(self):
        supervisor = (ROOT / "kernel" / "init" / "supervisor.c").read_text()
        ipc = (ROOT / "kernel" / "ipc" / "ipc.c").read_text()
        service = (ROOT / "userspace" / "programs" / "reist_probe.c").read_text()
        guest = (ROOT / "userspace" / "programs" / "guest_test.c").read_text()
        self.assertIn('message_request_is(&request, "NETPRESSURE")', service)
        self.assertIn('"REIST_PRESSURE_READY"', service)
        self.assertIn("X86OS_IPC_QUEUE_DEPTH", guest)
        self.assertIn("QUEUE_PRESSURE_FALLBACK", supervisor)
        ingress = supervisor[supervisor.index("bool supervisor_network_submit_header"):
                             supervisor.index("int supervisor_network_probe_request")]
        self.assertLess(ingress.index(
                            "supervisor_protected_probe_authority_take_epoch("),
                        ingress.index("return ingress == 0"))
        self.assertIn("for (uint32_t index = 0U; index < 42U; ++index)",
                      supervisor)
        self.assertIn("ipc_send_external_from_peer(", supervisor)
        self.assertIn("if (endpoint->count >= IPC_QUEUE_DEPTH)", ipc)
        self.assertIn("peer->holder_pid", ipc)
        self.assertNotIn("ipc_send_timeout(", ingress)

    def test_arp_structure_validation_runs_only_in_ring3_service(self):
        service = read("userspace/programs/reist_probe.c")
        guest = read("userspace/programs/guest_test.c")
        self.assertIn("message->length < 46U", service)
        for offset in range(18, 26):
            self.assertIn(f"message->payload[{offset}]", service)
        self.assertIn("message->length = 46U", guest)
        self.assertIn("TEST_STAGE ARP_VALIDATION_OK", guest)
        self.assertIn("message.payload[22] = 5U", guest)
        self.assertNotIn("arp_packet_t", service)
        self.assertNotIn("k_malloc", service)

    def test_arp_reply_is_bound_to_probe_identity(self):
        supervisor = read("kernel/init/supervisor.c")
        service = read("userspace/programs/reist_probe.c")
        guest = read("userspace/programs/guest_test.c")
        netstack = read("drivers/net/netstack.c")
        self.assertIn("uint32_t netstack_get_gateway(void)", netstack)
        self.assertIn("supervisor_protected_network_context_prepare(",
                      supervisor)
        self.assertIn("network_context.gateway", supervisor)
        self.assertIn("network_context.local_ip", supervisor)
        self.assertIn("network_context.local_mac[index]", supervisor)
        self.assertIn(".length = 64U", supervisor)
        for offset in (32, 42, 46, 50, 54):
            self.assertIn(f"message->payload[{offset}U + index]", service)
        self.assertIn("message->payload[25] != 2U", service)
        self.assertIn("message.payload[46] ^= 1U", guest)
        self.assertIn("TEST_STAGE ARP_IDENTITY_OK", guest)

    def test_real_nic_probe_is_service_scoped_and_rate_limited(self):
        supervisor = read("kernel/init/supervisor.c")
        service = read("userspace/programs/reist_probe.c")
        guest = read("userspace/programs/guest_test.c")
        sdk = read("userspace/sdk/include/x86os.h")
        probe = supervisor[
            supervisor.index("int supervisor_network_probe_request("):
            supervisor.index("static void supervisor_worker(")]
        self.assertIn("pid != control.pid", probe)
        self.assertIn("generation != control.process_generation", probe)
        self.assertIn("< 250U", probe)
        self.assertIn("netstack_probe_gateway()", probe)
        self.assertIn("supervisor_protected_probe_authority_begin(", probe)
        self.assertIn("supervisor_protected_probe_authority_cancel(", probe)
        self.assertIn("X86OS_SYS_NETWORK_PROBE = 59", sdk)
        self.assertIn('message_request_is(&request, "NETPROBE")', service)
        self.assertIn("request.payload[3] == 'R'", service)
        self.assertIn("REIST_REPORT_NETWORK_HEADER", service)
        self.assertIn("TEST_STAGE NETWORK_HANDOFF_OK", guest)
        worker = supervisor[
            supervisor.index("static void supervisor_worker("):
            supervisor.index("bool supervisor_start_worker(")]
        self.assertIn("netdev_poll();", worker)
        self.assertIn("scheduler_sleep_ms(SUPERVISOR_CHECK_INTERVAL_MS)",
                      worker)

    def test_network_handoff_crash_revokes_and_reconnects(self):
        supervisor = read("kernel/init/supervisor.c")
        service = read("userspace/programs/reist_probe.c")
        guest = read("userspace/programs/guest_test.c")
        runner = read("scripts/run_qemu_smoke.py")
        self.assertIn('message_request_is(&request, "NETCRASH")', service)
        self.assertIn('volatile("ud2")', service)
        self.assertIn("supervisor_protected_probe_authority_cancel(",
                      supervisor)
        self.assertIn("supervisor_protected_network_context_clear(",
                      supervisor)
        self.assertIn("REIST_NETWORK SERVICE_CRASH_RECOVERED", supervisor)
        self.assertIn("attempt < 100U", guest)
        self.assertIn("TEST_STAGE NETWORK_RECOVERY_OK", guest)
        self.assertIn("REIST_NETWORK_RECOVERY_MARKER", runner)

    def test_monotone_probe_id_is_append_only_and_correlated(self):
        supervisor = read("kernel/init/supervisor.c")
        syscall = read("kernel/syscall/syscall_table.c")
        process = read("kernel/proc/process.c")
        sdk = read("userspace/sdk/include/x86os.h")
        service = read("userspace/programs/reist_probe.c")
        guest = read("userspace/programs/guest_test.c")
        self.assertIn("authority->next_id", supervisor)
        self.assertIn("authority->next_id > UINT32_MAX", supervisor)
        self.assertIn("authority->active_id = probe_id", supervisor)
        self.assertIn("message.payload[60U + index]", supervisor)
        self.assertIn("SYS_NETWORK_PROBE_ID 60", read("lib/libc/stdlib.h"))
        self.assertIn("X86OS_SYS_NETWORK_PROBE_ID = 60", sdk)
        self.assertIn("SYS_NETWORK_PROBE, SYS_NETWORK_PROBE_ID", process)
        body = syscall[syscall.index("static int syscall_network_probe_id("):
                       syscall.index("static int syscall_service_connect(")]
        self.assertLess(body.index("user_range_accessible"),
                        body.index("supervisor_network_probe_request_id"))
        self.assertIn("copy_to_user_space", body)
        self.assertIn("x86os_network_probe_id(&pending_network_probe_id)",
                      service)
        self.assertIn("message_probe_id(&request) != pending_network_probe_id",
                      service)
        self.assertIn("x86os_reist_commit_arp_binding(&binding)", service)
        self.assertIn("supervisor_protected_network_context_publish_binding_epoch(",
                      supervisor)
        self.assertIn("supervisor_protected_network_context_consume_epoch(",
                      supervisor)
        self.assertIn("REIST_NETWORK PROBE_ID_OK", supervisor)
        self.assertIn("x86os_network_probe_id((uint32_t*)(uintptr_t)0x1000U)",
                      guest)

    def test_network_degradation_is_counted_outside_irq_context(self):
        supervisor = read("kernel/init/supervisor.c")
        service = read("userspace/programs/reist_probe.c")
        self.assertIn("network_degradation_record(", supervisor)
        self.assertIn("SUPERVISOR_NETWORK_DEGRADED_QUEUE", supervisor)
        self.assertIn("SUPERVISOR_NETWORK_DEGRADED_EXPIRED", supervisor)
        self.assertIn("REIST_REPORT_NETWORK_DEGRADED", service)
        worker = supervisor[supervisor.index("static void supervisor_worker("):
                            supervisor.index("bool supervisor_start_worker(")]
        self.assertIn("supervisor_protected_probe_authority_expire_epoch(",
                      worker)
        self.assertIn("network_degradation_record(", worker)

    def test_network_transaction_epoch_rejects_mixed_snapshots(self):
        supervisor = read("kernel/init/supervisor.c")
        header = read("include/kernel/supervisor.h")
        self.assertIn("uint32_t network_epoch;", header)
        self.assertGreaterEqual(header.count("uint32_t transaction_epoch;"), 2)
        handoff = supervisor[
            supervisor.index("bool supervisor_network_submit_header("):
            supervisor.index("int supervisor_network_probe_request(")]
        self.assertIn("network_context.transaction_epoch != "
                      "control.network_epoch", handoff)
        self.assertIn("supervisor_protected_probe_authority_take_epoch(",
                      handoff)
        self.assertIn(
            "supervisor_protected_network_context_publish_binding_epoch(",
                      handoff)
        report = supervisor[
            supervisor.index("int supervisor_probe_report("):
            supervisor.index("int supervisor_service_connect(")]
        self.assertIn("control.network_epoch", report)
        self.assertIn("supervisor_protected_network_context_consume_epoch(",
                      report)

    def test_arp_binding_commit_is_narrow_versioned_and_epoch_bound(self):
        libc = read("lib/libc/stdlib.h")
        sdk = read("userspace/sdk/include/x86os.h")
        sdk_c = read("userspace/sdk/x86os.c")
        syscall = read("kernel/syscall/syscall_table.c")
        process = read("kernel/proc/process.c")
        supervisor = read("kernel/init/supervisor.c")
        netstack = read("drivers/net/netstack.c")
        service = read("userspace/programs/reist_probe.c")
        guest = read("userspace/programs/guest_test.c")
        runner = read("scripts/run_qemu_smoke.py")
        self.assertIn("SYS_REIST_ARP_BINDING 62", libc)
        self.assertIn("X86OS_SYS_REIST_ARP_BINDING = 62", sdk)
        self.assertIn("sizeof(x86os_reist_arp_binding_t) == 24U", sdk_c)
        self.assertIn("SYS_REIST_ARP_BINDING", process)
        body = syscall[syscall.index("static int syscall_reist_arp_binding("):
                       syscall.index("static int syscall_service_connect(")]
        self.assertLess(body.index("user_range_accessible"),
                        body.index("copy_from_user"))
        self.assertLess(body.index("copy_from_user"),
                        body.index("supervisor_network_commit_arp_binding"))
        mediator = supervisor[
            supervisor.index("int supervisor_network_commit_arp_binding("):
            supervisor.index("static void supervisor_worker(")]
        self.assertIn("control.network_epoch != binding->probe_id", mediator)
        self.assertIn("context.candidate_ip != binding->ip", mediator)
        self.assertIn("context.candidate_mac[index] != binding->mac[index]",
                      mediator)
        self.assertLess(mediator.index(
                            "supervisor_protected_network_context_consume_epoch"),
                        mediator.index("netstack_commit_arp_binding"))
        self.assertIn("SUPERVISED_ARP_CACHE_SIZE", read(
            "include/kernel/arp_binding_cache.h"))
        self.assertNotIn("ARP_CACHE_SIZE", netstack)
        self.assertIn("netstack_commit_arp_binding", netstack)
        self.assertIn("x86os_reist_commit_arp_binding(&binding)", service)
        self.assertIn("x86os_reist_commit_arp_binding(\n"
                      "            (const x86os_reist_arp_binding_t*)"
                      "(uintptr_t)0x1000U) != -14", guest)
        self.assertIn("REIST_NETWORK ARP_BINDING_OK", supervisor)
        self.assertIn("REIST_ARP_BINDING_MARKER", runner)

    def test_network_degradation_stats_abi_is_versioned_read_only(self):
        libc = read("lib/libc/stdlib.h")
        sdk = read("userspace/sdk/include/x86os.h")
        syscall = read("kernel/syscall/syscall_table.c")
        guest = read("userspace/programs/guest_test.c")
        self.assertIn("SYS_NETWORK_PROBE_STATS 61", libc)
        self.assertIn("X86OS_SYS_NETWORK_PROBE_STATS = 61", sdk)
        self.assertIn("X86OS_NETWORK_PROBE_STATS_VERSION 1U", sdk)
        self.assertIn("sizeof(x86os_network_probe_stats_t) == 24U",
                      read("userspace/sdk/x86os.c"))
        body = syscall[syscall.index("static int syscall_network_probe_stats("):
                       syscall.index("static int syscall_service_connect(")]
        self.assertLess(body.index("user_range_accessible"),
                        body.index("supervisor_network_degradation_snapshot"))
        self.assertIn("NETWORK_PROBE_STATS_VERSION", body)
        self.assertIn("snapshot_result != 0", body)
        self.assertLess(body.index("snapshot_result != 0"),
                        body.index("copy_to_user_space"))
        self.assertIn("copy_to_user_space", body)
        self.assertNotIn("supervisor_network_degradation_init", body)
        self.assertIn("TEST_STAGE NETWORK_STATS_OK", guest)
        self.assertIn("queue_fallback <= stats_before.queue_fallback", guest)

    def test_service_protocol_correlates_generation_scoped_requests(self):
        service = read("userspace/programs/reist_probe.c")
        guest = read("userspace/programs/guest_test.c")
        self.assertIn("SERVICE_PROTOCOL_HEADER_SIZE 8U", service)
        self.assertIn("message_request_id", service)
        self.assertIn("response_init(&response, request_id", service)
        self.assertIn("pending_network_request", service)
        self.assertIn("service_request_set", guest)
        self.assertIn("service_response_is", guest)
        self.assertIn("received != request_id", guest)
        self.assertIn("++request_id", guest)
        self.assertIn('message_request_is(&request, "BADID")', service)
        self.assertIn("request_id + 1U", service)
        self.assertIn("TEST_STAGE SERVICE_CORRELATION_OK", guest)


if __name__ == "__main__":
    unittest.main()
