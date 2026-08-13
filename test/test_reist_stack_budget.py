import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class ReistStackBudgetTests(unittest.TestCase):
    def test_kernel_build_makes_compiler_gates_mandatory(self):
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        self.assertIn("-Werror=vla", makefile)
        self.assertIn("-Wframe-larger-than=4096", makefile)
        self.assertIn("-Werror=frame-larger-than", makefile)
        self.assertIn("check-kernel-stack: $(ALL_OBJ)", makefile)


if __name__ == "__main__":
    unittest.main()
