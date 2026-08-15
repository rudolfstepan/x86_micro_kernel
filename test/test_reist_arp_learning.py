import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class ReistArpLearningTests(unittest.TestCase):
    def test_legacy_learning_and_cache_are_removed(self):
        source = (ROOT / "drivers/net/netstack.c").read_text(encoding="utf-8")
        header = (ROOT / "drivers/net/netstack.h").read_text(encoding="utf-8")
        self.assertNotIn("arp_add_entry", source)
        self.assertNotIn("arp_cache[", source)
        self.assertNotIn("ARP_CACHE_SIZE", header)
        self.assertFalse((ROOT / "drivers/net/arp_learning_policy.c").exists())
        self.assertFalse((ROOT / "include/kernel/arp_learning_policy.h").exists())

    def test_route_changes_revoke_protected_bindings_before_publish(self):
        source = (ROOT / "drivers/net/netstack.c").read_text(encoding="utf-8")
        manual = source[source.index("bool netstack_set_config("):
                        source.index("bool netstack_apply_supervised_dhcp(")]
        dhcp_commit = source[source.index("bool netstack_apply_supervised_dhcp("):
                             source.index("uint32_t netstack_get_ip_address(")]
        self.assertIn("arp_revoke_route_bindings(net_config.gateway, gateway)",
                      manual)
        self.assertIn("arp_revoke_route_bindings(net_config.gateway, gateway)",
                      dhcp_commit)
        self.assertLess(manual.index("arp_revoke_route_bindings"),
                        manual.index("net_config.gateway    = gateway"))
        self.assertLess(dhcp_commit.index("arp_revoke_route_bindings"),
                        dhcp_commit.index("net_config.gateway = gateway"))

    def test_only_supervised_mediator_can_commit_any_binding(self):
        source = (ROOT / "drivers/net/netstack.c").read_text(encoding="utf-8")
        commit = source[source.index("bool netstack_commit_arp_binding("):
                        source.index("int netstack_revoke_arp_bindings(")]
        self.assertIn("supervised_arp_cache_commit", commit)
        self.assertNotIn("arp_remove_entry", commit)


if __name__ == "__main__":
    unittest.main()
