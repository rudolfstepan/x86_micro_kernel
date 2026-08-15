from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


class ReistUdpIngressTests(unittest.TestCase):
    def test_append_only_abi_and_copyout_rollback(self) -> None:
        libc = read("lib/libc/stdlib.h")
        sdk_h = read("userspace/sdk/include/x86os.h")
        sdk_c = read("userspace/sdk/x86os.c")
        syscall = read("kernel/syscall/syscall_table.c")
        process = read("kernel/proc/process.c")
        self.assertIn("SYS_REIST_UDP_INGRESS 80", libc)
        self.assertIn("X86OS_SYS_REIST_UDP_INGRESS = 80", sdk_h)
        self.assertIn("sizeof(x86os_reist_udp_ingress_t) == 40U", sdk_c)
        self.assertIn("sizeof(supervisor_udp_ingress_t) == 40U", syscall)
        self.assertIn("SYS_REIST_UDP_INGRESS", process)
        function = syscall[syscall.index("static int syscall_reist_udp_ingress"):]
        function = function[:function.index("_Static_assert", 1)]
        self.assertIn("user_range_accessible", function)
        self.assertIn("copy_from_user", function)
        self.assertIn("copy_to_user", function)
        self.assertIn("supervisor_network_cancel_udp_ingress", function)

    def test_delivery_is_generation_crc_and_deadline_scoped(self) -> None:
        header = read("include/kernel/supervisor.h")
        source = read("kernel/init/supervisor.c")
        self.assertIn("supervisor_protected_udp_delivery_t", header)
        self.assertIn("process_generation", header)
        self.assertIn("frame_crc32", header)
        self.assertIn("deadline_ms", header)
        self.assertIn("critical_object_init", source)
        ingress = source[source.index("int supervisor_network_udp_ingress("):]
        ingress = ingress[:ingress.index("bool supervisor_network_submit_udp(")]
        self.assertIn("delivery.process_generation != generation", ingress)
        self.assertIn("delivery.frame_crc32 != ingress->frame_crc32", ingress)
        self.assertIn("pit_monotonic_ms() >= delivery.deadline_ms", ingress)
        self.assertIn("udp_ingress_drop_is_canonical", ingress)
        self.assertNotIn("k_malloc", ingress)
        self.assertNotIn("for (;;)", ingress)

    def test_service_owned_ports_have_only_ring3_delivery(self) -> None:
        netstack = read("drivers/net/netstack.c")
        handler = netstack[netstack.index("static void handle_udp_packet"):]
        handler = handler[:handler.index("// UDP low-level")]
        ownership = handler.index("supervisor_network_udp_service_owns_port")
        legacy = handler.index("supervisor_network_submit_udp")
        self.assertLess(ownership, legacy)
        self.assertIn("return;", handler[ownership:legacy])

    def test_ring3_parser_accepts_or_canonically_drops_once(self) -> None:
        service = read("examples/userspace/reist_probe.c")
        frame_loop = service[service.index("raw_udp_delivery"):]
        frame_loop = frame_loop[:frame_loop.index("} else if (frame_result")]
        self.assertIn(".frame_crc32 = frame_crc32", frame_loop)
        self.assertIn("frame_crc32 = reist_frame_crc32", service)
        self.assertIn("udp_binding_for_port", frame_loop)
        self.assertEqual(frame_loop.count("x86os_reist_udp_ingress("), 1)
        self.assertIn("x86os_reist_udp_reply", frame_loop)

    def test_runtime_contract_uses_real_rtl8139_udp(self) -> None:
        runtime = read("scripts/test-reist-runtime.ps1")
        runner = read("scripts/run_qemu_smoke.py")
        self.assertIn("'network-udp-ingress'", runtime)
        self.assertIn("--expect-network-udp-ingress", runtime)
        self.assertIn("--inject-udp-echo", runtime)
        self.assertIn("REIST_NETWORK UDP_INGRESS_RING3", runner)


if __name__ == "__main__":
    unittest.main()
