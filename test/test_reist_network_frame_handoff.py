import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


class ReistNetworkFrameHandoffTests(unittest.TestCase):
    def test_append_only_abi_and_default_deny_profile(self) -> None:
        self.assertIn("SYS_REIST_NETWORK_FRAME 79", read("lib/libc/stdlib.h"))
        self.assertIn("X86OS_SYS_REIST_NETWORK_FRAME = 79",
                      read("userspace/sdk/include/x86os.h"))
        syscall = read("kernel/syscall/syscall_table.c")
        self.assertIn("case SYS_REIST_NETWORK_FRAME", syscall)
        self.assertIn("user_range_accessible", syscall)
        self.assertIn("copy_to_user", syscall)
        self.assertLess(syscall.index("copy_to_user(user_frame, &frame"),
                        syscall.index("supervisor_network_confirm_frame_delivery"))
        self.assertIn("SYS_REIST_NETWORK_FRAME", read("kernel/proc/process.c"))

    def test_service_queue_is_static_bounded_and_generation_fresh(self) -> None:
        netdev = read("drivers/net/netdev.c")
        self.assertIn("NETDEV_SERVICE_QUEUE_SIZE 8", netdev)
        self.assertIn("service_queue[NETDEV_SERVICE_QUEUE_SIZE]", netdev)
        self.assertIn("length > NETDEV_MAX_FRAME_SIZE", netdev)
        self.assertNotIn("k_malloc", netdev)
        supervisor = read("kernel/init/supervisor.c")
        spawn = supervisor[supervisor.index("static bool probe_spawn_next("):]
        spawn = spawn[:spawn.index("bool supervisor_start_probe(")]
        self.assertIn("netdev_reset_service_frames();", spawn)

    def test_receive_is_nonblocking_and_ring3_revalidates_frame(self) -> None:
        supervisor = read("kernel/init/supervisor.c")
        start = supervisor.index("int supervisor_network_receive_frame(")
        block = supervisor[start:supervisor.index(
            "int supervisor_service_connect(", start)]
        self.assertIn("if (length == 0) return -11;", block)
        for forbidden in ("pit_delay", "wait_queue", "k_malloc", "for (;;)"):
            self.assertNotIn(forbidden, block)
        service = read("examples/userspace/reist_probe.c")
        self.assertIn("x86os_reist_receive_network_frame", service)
        self.assertIn("network_frame.length < 14U", service)
        self.assertIn("X86OS_REIST_REPORT_NETWORK_FRAME", service)
        self.assertIn("frame_delivery_pending != 0U", supervisor)
        self.assertIn("frame_delivery_ethertype == value", supervisor)

    def test_runtime_gate_requires_real_ring3_consumption_marker(self) -> None:
        runtime = read("scripts/test-reist-runtime.ps1")
        runner = read("scripts/run_qemu_smoke.py")
        self.assertIn("'network-frame'", runtime)
        self.assertIn("--expect-network-frame", runtime)
        self.assertIn("REIST_NETWORK FRAME_HANDOFF", runner)
        self.assertIn("expect_network_frame", runner)


if __name__ == "__main__":
    unittest.main()
