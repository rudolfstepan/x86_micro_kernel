import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class ReistCriticalObjectTests(unittest.TestCase):
    def test_host_fault_injection(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "critical-object-test.exe"
            subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-I", str(ROOT), str(ROOT / "kernel/init/critical_object.c"),
                 str(ROOT / "test/test_critical_object_host.c"),
                 "-o", str(executable)], check=True, capture_output=True,
                text=True, timeout=30,
            )
            result = subprocess.run([str(executable)], timeout=10)
            self.assertEqual(result.returncode, 0)

    def test_contract_contains_ecc_redundancy_and_semantic_validation(self):
        header = (ROOT / "include/kernel/critical_object.h").read_text(encoding="utf-8")
        source = (ROOT / "kernel/init/critical_object.c").read_text(encoding="utf-8")
        self.assertIn("critical_object_copy_t primary", header)
        self.assertIn("critical_object_copy_t shadow", header)
        self.assertIn("secded_encode", source)
        self.assertIn("secded_decode", source)
        self.assertIn("validator", source)
        self.assertIn("WORD_SEQUENCE", source)
        self.assertIn("copy_crc", source)


if __name__ == "__main__":
    unittest.main()
