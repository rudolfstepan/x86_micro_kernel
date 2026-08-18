import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class ReistArpCacheTests(unittest.TestCase):
    def test_protected_cache_host_behavior(self):
        with tempfile.TemporaryDirectory() as directory:
            output = pathlib.Path(directory) / "arp-cache-test.exe"
            command = [
                "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-DREIST_HOST_TEST", "-Iinclude", "-I.",
                "kernel/init/critical_object.c",
                "drivers/net/arp_binding_cache.c",
                "test/test_arp_binding_cache_host.c",
                "-o", str(output),
            ]
            subprocess.run(command, cwd=ROOT, check=True, capture_output=True)
            result = subprocess.run(
                [str(output)], cwd=ROOT, check=True, capture_output=True,
                text=True,
            )
            self.assertIn("ARP_BINDING_CACHE_OK", result.stdout)

    def test_netstack_uses_only_the_protected_cache(self):
        source = (ROOT / "drivers/net/netstack.c").read_text(encoding="utf-8")
        lookup = source[source.index("bool arp_lookup"):
                        source.index("static bool arp_send_request_now")]
        self.assertIn("supervised_arp_cache_lookup", lookup)
        self.assertIn("protected_result == SUPERVISED_ARP_HIT", lookup)
        self.assertNotIn("arp_cache[", source)
        self.assertNotIn("arp_remove_entry", source)

    def test_supervisor_commits_epoch_and_monotonic_time(self):
        source = (ROOT / "kernel/init/supervisor.c").read_text(encoding="utf-8")
        self.assertIn("binding->mac, binding->probe_id,", source)
        self.assertIn("pid, generation, pit_monotonic_ms()))", source)
        self.assertIn("control.pid, control.process_generation", source)
        self.assertIn("netstack_scrub_arp_bindings(now_ms, &expired, &corrected)",
                      source)
        self.assertIn("ARP_BINDING_CORRECTED", source)

    def test_routine_arp_success_trace_is_qemu_or_opt_in_only(self):
        source = (ROOT / "kernel/init/supervisor.c").read_text(encoding="utf-8")
        self.assertIn(
            "#if defined(QEMU_BUILD) || defined(REIST_NETWORK_TRACE)", source)
        for marker in (
                "RX_HEADER_ARP", "PROBE_ID_OK", "ARP_BINDING_OK",
                "ARP_RESOLUTION_QUEUED", "ARP_RESOLUTION_MEDIATED",
                "ARP_BINDING_EXPIRED"):
            marker_position = source.index(f"REIST_NETWORK {marker}")
            call_position = source.rfind("REIST_NETWORK_TRACE_PRINT(",
                                         0, marker_position)
            self.assertGreaterEqual(call_position, 0)
            self.assertLess(marker_position - call_position, 160)
        self.assertIn('printf("REIST_NETWORK ARP_RESOLUTION_REJECTED', source)
        self.assertIn('printf("REIST_NETWORK ARP_BINDING_CORRECTED', source)

    def test_cache_is_fixed_capacity_and_heap_free(self):
        header = (ROOT / "include/kernel/arp_binding_cache.h").read_text(
            encoding="utf-8")
        source = (ROOT / "drivers/net/arp_binding_cache.c").read_text(
            encoding="utf-8")
        self.assertIn("SUPERVISED_ARP_CACHE_SIZE 32U", header)
        self.assertIn("critical_object_t entries", header)
        self.assertNotIn("k_malloc", source)
        self.assertNotIn("k_free", source)

    def test_cache_is_initialized_without_network_hardware(self):
        kernel = (ROOT / "kernel/init/kernel.c").read_text(encoding="utf-8")
        self.assertIn("if (!netstack_safety_init())", kernel)
        self.assertLess(kernel.index("netstack_safety_init"),
                        kernel.index("supervisor_start_worker"))


if __name__ == "__main__":
    unittest.main()
