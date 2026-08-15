import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class ReistArpLearningTests(unittest.TestCase):
    def test_gateway_is_excluded_from_legacy_learning(self):
        with tempfile.TemporaryDirectory() as directory:
            output = pathlib.Path(directory) / "arp-learning-test.exe"
            subprocess.run([
                "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-Iinclude", "-I.",
                "drivers/net/arp_learning_policy.c",
                "test/test_arp_learning_policy_host.c",
                "-o", str(output),
            ], cwd=ROOT, check=True, capture_output=True)
            result = subprocess.run(
                [str(output)], cwd=ROOT, check=True, capture_output=True,
                text=True,
            )
            self.assertIn("ARP_LEARNING_POLICY_OK", result.stdout)

    def test_all_legacy_cache_mutations_use_the_central_policy(self):
        source = (ROOT / "drivers/net/netstack.c").read_text(encoding="utf-8")
        add = source[source.index("void arp_add_entry("):
                     source.index("static void arp_remove_entry(")]
        self.assertIn("arp_learning_policy_allows_legacy(", add)
        self.assertLess(add.index("arp_learning_policy_allows_legacy("),
                        add.index("arp_cache[slot].ip"))
        self.assertEqual(source.count("arp_cache[slot].ip = ip"), 1)

    def test_gateway_configuration_purges_preexisting_legacy_binding(self):
        source = (ROOT / "drivers/net/netstack.c").read_text(encoding="utf-8")
        manual = source[source.index("void netstack_set_config("):
                        source.index("bool netstack_apply_supervised_dhcp(")]
        dhcp_commit = source[source.index("bool netstack_apply_supervised_dhcp("):
                             source.index("uint32_t netstack_get_ip_address(")]
        self.assertIn("arp_remove_entry(net_config.gateway);", manual)
        self.assertIn("arp_remove_entry(gateway);", manual)
        self.assertIn("arp_remove_entry(net_config.gateway);", dhcp_commit)
        self.assertIn("arp_remove_entry(gateway);", dhcp_commit)

    def test_only_supervised_mediator_can_commit_gateway_binding(self):
        source = (ROOT / "drivers/net/netstack.c").read_text(encoding="utf-8")
        commit = source[source.index("bool netstack_commit_arp_binding("):
                        source.index("int netstack_revoke_arp_bindings(")]
        self.assertIn("supervised_arp_cache_commit", commit)
        self.assertIn("arp_remove_entry(ip)", commit)


if __name__ == "__main__":
    unittest.main()
