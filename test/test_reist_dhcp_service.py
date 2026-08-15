"""Contracts for generation-scoped Ring-3-mediated DHCP configuration."""

from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


class ReistDhcpServiceTests(unittest.TestCase):
    def test_dhcp_transport_only_submits_a_proposal(self) -> None:
        netstack = read("drivers/net/netstack.c")
        start = netstack.index("bool netstack_configure_dhcp(void)")
        body = netstack[start:netstack.index(
            "bool netstack_apply_supervised_dhcp", start)]
        self.assertIn("supervisor_network_submit_dhcp_config", body)
        self.assertNotIn("net_config.ip_address = ip", body)
        self.assertIn("bool netstack_apply_supervised_dhcp", netstack)

    def test_proposal_is_protected_bounded_and_generation_scoped(self) -> None:
        header = read("include/kernel/supervisor.h")
        supervisor = read("kernel/init/supervisor.c")
        ipc_header = read("include/kernel/ipc.h")
        ipc = read("kernel/ipc/ipc.c")
        self.assertIn("supervisor_protected_dhcp_context_t", header)
        self.assertIn("SUPERVISOR_DHCP_COMMIT_TIMEOUT_MS 1000U", supervisor)
        self.assertIn("dhcp_authority", supervisor)
        self.assertIn("control.process_generation", supervisor)
        self.assertIn("dhcp_config_valid_values", supervisor)
        self.assertIn("supervisor_protected_dhcp_lease_t", header)
        self.assertIn("ipc_send_kernel_to_owner", ipc_header)
        self.assertIn("ipc_send_kernel_to_owner", supervisor)
        self.assertIn("enqueue_message_locked(endpoint_slot, endpoint, message,\n"
                      "                                        0, 0U)", ipc)

    def test_commit_consumes_authority_before_network_mutation(self) -> None:
        supervisor = read("kernel/init/supervisor.c")
        start = supervisor.index("int supervisor_network_commit_dhcp_config(")
        body = supervisor[start:supervisor.index(
            "static void supervisor_worker", start)]
        self.assertLess(body.index("probe_authority_take_epoch"),
                        body.index("dhcp_context_clear"))
        self.assertLess(body.index("dhcp_context_clear"),
                        body.index("supervisor_unlock(flags)"))
        self.assertLess(body.index("supervisor_unlock(flags)"),
                        body.index("netstack_apply_supervised_dhcp"))

    def test_append_only_syscall_and_probe_profile_match(self) -> None:
        self.assertIn("SYS_REIST_DHCP_COMMIT 73",
                      read("lib/libc/stdlib.h"))
        self.assertIn("X86OS_SYS_REIST_DHCP_COMMIT = 73",
                      read("userspace/sdk/include/x86os.h"))
        self.assertIn("case SYS_REIST_DHCP_COMMIT",
                      read("kernel/syscall/syscall_table.c"))
        self.assertIn("SYS_REIST_DHCP_COMMIT", read("kernel/proc/process.c"))

    def test_service_validates_netd_before_commit(self) -> None:
        probe = read("examples/userspace/reist_probe.c")
        self.assertIn("message->payload[3] == 'D'", probe)
        self.assertIn("dhcp_proposal_valid(message)", probe)
        self.assertIn("host_mask & (host_mask + 1U)", probe)
        self.assertIn("x86os_reist_commit_dhcp(&commit)", probe)
        self.assertIn("message->length != 28U", probe)
        self.assertIn("lease_seconds", probe)

    def test_lease_expiry_is_protected_and_withdraws_configuration(self) -> None:
        header = read("include/kernel/supervisor.h")
        supervisor = read("kernel/init/supervisor.c")
        netstack = read("drivers/net/netstack.c")
        self.assertIn("SUPERVISOR_DHCP_LEASE_VERSION", header)
        self.assertIn("supervisor_protected_dhcp_lease_publish", supervisor)
        self.assertIn("now_ms >= lease.deadline_ms", supervisor)
        self.assertIn("netstack_clear_supervised_dhcp(lease.ip_address)",
                      supervisor)
        self.assertIn("REIST_NETWORK DHCP_LEASE_EXPIRED", supervisor)
        self.assertIn("bool netstack_clear_supervised_dhcp", netstack)

    def test_dhcp_ack_requires_a_bounded_lease_option(self) -> None:
        netstack = read("drivers/net/netstack.c")
        self.assertIn("case DHO_LEASE_TIME", netstack)
        self.assertIn("SUPERVISOR_DHCP_LEASE_MIN_SECONDS", netstack)
        self.assertIn("SUPERVISOR_DHCP_LEASE_MAX_SECONDS", netstack)

    def test_runtime_has_a_bounded_expiry_fault_profile(self) -> None:
        build = read("scripts/build-windows.ps1")
        runtime = read("scripts/test-reist-runtime.ps1")
        runner = read("scripts/run_qemu_smoke.py")
        self.assertIn("DhcpLeaseFaultInjection", build)
        self.assertIn("REIST_DHCP_LEASE_TEST_MS=2500U", read("Makefile"))
        self.assertIn("'dhcp-expiry'", runtime)
        self.assertIn("--expect-dhcp-expiry", runner)

    def test_boot_starts_supervisor_before_dhcp(self) -> None:
        kernel = read("kernel/init/kernel.c")
        self.assertLess(kernel.index("supervisor_start_worker()"),
                        kernel.index("configure_network_after_service();"))
        self.assertIn("supervisor_probe_ready()", kernel)
        self.assertIn("pit_monotonic_ms() < ready_deadline", kernel)

    def test_runtime_requires_real_preboot_mediation(self) -> None:
        runner = read("scripts/run_qemu_smoke.py")
        runtime = read("scripts/test-reist-runtime.ps1")
        self.assertIn('"--expect-dhcp-config"', runner)
        self.assertIn("REIST_DHCP_CONFIG_QUEUED_MARKER", runner)
        self.assertIn("'dhcp-config'", runtime)
        self.assertIn("'--nic', 'rtl8139', '--expect-dhcp-config'", runtime)


if __name__ == "__main__":
    unittest.main()
