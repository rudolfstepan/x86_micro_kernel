import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class StorageMaintenanceContracts(unittest.TestCase):
    def read(self, path):
        return (ROOT / path).read_text(encoding="utf-8")

    def test_lease_is_fixed_capacity_and_generation_scoped(self):
        header = self.read("include/kernel/storage_maintenance.h")
        source = self.read("kernel/init/storage_maintenance.c")
        self.assertIn("STORAGE_MAINTENANCE_LEASE_MS 15000U", header)
        self.assertIn("lease_objects[STORAGE_MAINTENANCE_MAX_RESOURCES]", source)
        self.assertIn("process_generation", source)
        self.assertIn("media_fingerprint", source)
        self.assertIn("++state.token_generation", source)
        self.assertIn("deadline_expired", source)

    def test_expired_lease_cannot_be_renewed_or_validated(self):
        source = self.read("kernel/init/storage_maintenance.c")
        renew = source[source.index("int storage_maintenance_renew"):
                       source.index("int storage_maintenance_release")]
        valid = source[source.index("bool storage_maintenance_valid"):]
        self.assertIn("STORAGE_ETIMEDOUT", renew)
        self.assertIn("!deadline_expired", valid)

    def test_release_clears_authority_and_is_idempotent(self):
        source = self.read("kernel/init/storage_maintenance.c")
        release = source[source.index("int storage_maintenance_release"):
                          source.index("bool storage_maintenance_valid")]
        self.assertIn("if (state.active == 0U) return 0", release)
        self.assertIn("state.media_fingerprint = 0", release)
        self.assertIn("state.deadline_ms = 0", release)


if __name__ == "__main__":
    unittest.main()
