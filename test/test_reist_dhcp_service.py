"""Contracts for generation-scoped Ring-3-mediated DHCP configuration."""

from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


class ReistDhcpServiceTests(unittest.TestCase):
    def test_ring3_state_machine_is_bounded_and_nonblocking(self) -> None:
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / (
                "dhcp-state.exe" if os.name == "nt" else "dhcp-state")
            subprocess.run([
                compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I", str(ROOT / "userspace/sdk/include"),
                str(ROOT / "userspace/sdk/reist_dhcp_state.c"),
                str(ROOT / "test/test_reist_dhcp_state_host.c"),
                "-o", str(executable),
            ], check=True, capture_output=True, timeout=30)
            subprocess.run([str(executable)], check=True, timeout=10)

    def test_renewal_transport_is_single_step_and_asynchronous(self) -> None:
        netstack = read("drivers/net/netstack.c")
        supervisor = read("kernel/init/supervisor.c")
        probe = read("userspace/programs/reist_probe.c")
        self.assertIn("netstack_send_supervised_dhcp_request", netstack)
        self.assertNotIn("netstack_dhcp_poll", netstack)
        self.assertIn("supervisor_network_request_dhcp_renewal", supervisor)
        self.assertIn("supervisor_network_accept_dhcp_renewal", supervisor)
        self.assertIn("reist_dhcp_state_poll", probe)
        request = supervisor[supervisor.index(
            "int supervisor_network_request_dhcp_renewal("):]
        request = request[:request.index(
            "bool supervisor_network_accept_dhcp_renewal", 1)]
        self.assertNotIn("while (", request)
        self.assertNotIn("pit_delay", request)

    def test_renewal_abi_is_append_only_and_service_only(self) -> None:
        self.assertIn("SYS_REIST_DHCP_RENEW 78", read("lib/libc/stdlib.h"))
        self.assertIn("X86OS_SYS_REIST_DHCP_RENEW = 78",
                      read("userspace/sdk/include/x86os.h"))
        self.assertIn("case SYS_REIST_DHCP_RENEW",
                      read("kernel/syscall/syscall_table.c"))
        self.assertIn("SYS_REIST_DHCP_RENEW", read("kernel/proc/process.c"))

    def test_runtime_proves_real_renewal_without_lease_loss(self) -> None:
        build = read("scripts/build-windows.ps1")
        runtime = read("scripts/test-reist-runtime.ps1")
        runner = read("scripts/run_qemu_smoke.py")
        self.assertIn("DhcpRenewFaultInjection", build)
        self.assertIn("REIST_DHCP_RENEW_TEST_MS=5000U", read("Makefile"))
        self.assertIn("'dhcp-renewal'", runtime)
        self.assertIn("--expect-dhcp-renewal", runner)
        self.assertIn("REIST_DHCP_RENEWED_MARKER", runner)

    def test_dhcp_transport_only_submits_a_proposal(self) -> None:
        supervisor = read("kernel/init/supervisor.c")
        ingress = supervisor[supervisor.index(
            "int supervisor_network_dhcp_ingress("):]
        ingress = ingress[:ingress.index("int supervisor_network_udp_bind(")]
        self.assertIn("supervisor_network_submit_dhcp_config_operation",
                      ingress)
        self.assertNotIn("netstack_configure_dhcp", read(
            "drivers/net/netstack.c"))
        netstack = read("drivers/net/netstack.c")
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
        probe = read("userspace/programs/reist_probe.c")
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
        parser = read("userspace/sdk/reist_dhcp_parser.c")
        supervisor = read("kernel/init/supervisor.c")
        self.assertIn("case 51U", parser)
        self.assertIn("parsed.lease_seconds = read_be32(value)", parser)
        self.assertIn("SUPERVISOR_DHCP_LEASE_MIN_SECONDS", supervisor)
        self.assertIn("SUPERVISOR_DHCP_LEASE_MAX_SECONDS", supervisor)

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
