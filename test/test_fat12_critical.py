import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class Fat12CriticalContracts(unittest.TestCase):
    def test_critical_files_are_explicitly_allowlisted(self):
        source = (ROOT / "fs/fat12/fat12_critical.c").read_text(encoding="utf-8")
        self.assertIn('"REIST.CFG"', source)
        self.assertIn('"STORAGE.CFG"', source)
        self.assertIn('"BOOT.CFG"', source)
        self.assertIn("return false", source)


if __name__ == "__main__":
    unittest.main()
