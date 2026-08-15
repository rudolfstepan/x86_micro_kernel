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

    def test_equal_sequence_conflict_fails_closed(self):
        source = self.read("fs/fat12/fat12_replica.c")
        select = source[source.index("bool fat12_replica_select"):]
        self.assertIn("sequence == replica->mirror_header.sequence", select)
        self.assertIn("memcmp(replica->primary_data", select)
        self.assertIn("return false", select)


if __name__ == "__main__":
    unittest.main()
