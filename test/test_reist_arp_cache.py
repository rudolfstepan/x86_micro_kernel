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

    def test_netstack_checks_protected_cache_before_legacy(self):
        source = (ROOT / "drivers/net/netstack.c").read_text(encoding="utf-8")
        lookup = source[source.index("bool arp_lookup"):
                        source.index("static bool arp_send_request_now")]
        self.assertLess(lookup.index("supervised_arp_cache_lookup"),
                        lookup.index("arp_cache[i].valid"))
        self.assertIn("protected_result != SUPERVISED_ARP_MISS", lookup)
        self.assertIn("arp_remove_entry(ip)", source)

    def test_supervisor_commits_epoch_and_monotonic_time(self):
        source = (ROOT / "kernel/init/supervisor.c").read_text(encoding="utf-8")
        self.assertIn("binding->mac, binding->probe_id,", source)
        self.assertIn("pid, generation, pit_monotonic_ms()))", source)
        self.assertIn("control.pid, control.process_generation", source)
        self.assertIn("netstack_scrub_arp_bindings(now_ms, &expired, &corrected)",
                      source)
        self.assertIn("ARP_BINDING_CORRECTED", source)

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
