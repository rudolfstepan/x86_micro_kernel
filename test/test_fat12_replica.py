import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class Fat12ReplicaContracts(unittest.TestCase):
    def read(self, path):
        return (ROOT / path).read_text(encoding="utf-8")

    def test_replica_has_fixed_capacity_and_crc(self):
        header = self.read("fs/fat12/fat12_replica.h")
        source = self.read("fs/fat12/fat12_replica.c")
        self.assertIn("FAT12_REPLICA_MAX_BYTES 4096U", header)
        self.assertIn("fat12_replica_crc32", source)
        self.assertIn("valid_header", source)
        self.assertIn("fat12_replica_publish_persistent", source)
        self.assertIn("fat12_replica_load", source)
        self.assertIn("write_verified", source)

    def test_equal_sequence_conflict_fails_closed(self):
        source = self.read("fs/fat12/fat12_replica.c")
        select = source[source.index("bool fat12_replica_select"):]
        self.assertIn("sequence == replica->mirror_header.sequence", select)
        self.assertIn("memcmp(replica->primary_data", select)
        self.assertIn("return false", select)

    def test_critical_vfs_reads_fallback_only_to_validated_replica(self):
        source = self.read("fs/fat12/fat12_vfs_adapter.c")
        self.assertIn("fat12_is_critical_name", source)
        self.assertIn("fat12_read_critical_replica", source)
        self.assertIn("replica_length == node->size", source)
        self.assertIn("fat12_publish_critical_replica", source)


if __name__ == "__main__":
    unittest.main()
