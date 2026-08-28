"""Contract and host behavior tests for the R1.2a resilient-page proof."""

from __future__ import annotations

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class MemoryResilienceTests(unittest.TestCase):
    def test_host_fault_campaign(self) -> None:
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "resilient-page-test.exe"
            command = [
                compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-DREIST_HOST_TEST", "-I", str(ROOT),
                str(ROOT / "kernel/init/critical_object.c"),
                str(ROOT / "kernel/init/resilient_page.c"),
                str(ROOT / "test/test_resilient_page_host.c"),
                "-o", str(executable),
            ]
            compiled = subprocess.run(
                command, cwd=ROOT, capture_output=True, text=True, timeout=30,
            )
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            result = subprocess.run(
                [str(executable)], cwd=ROOT, capture_output=True, text=True,
                timeout=10,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("RESILIENT_PAGE_HOST_OK", result.stdout)

    def test_contract_is_fixed_bounded_and_not_hardware_claim(self) -> None:
        header = (ROOT / "include/kernel/resilient_page.h").read_text(
            encoding="utf-8")
        source = (ROOT / "kernel/init/resilient_page.c").read_text(
            encoding="utf-8")
        architecture = (ROOT / "docs/architecture/MEMORY_RESILIENCE.md").read_text(
            encoding="utf-8")
        self.assertIn("RESILIENT_PAGE_SIZE 4096U", header)
        self.assertIn("RESILIENT_PAGE_CAPACITY 4U", header)
        self.assertIn("RESILIENT_PAGE_BANK_COUNT 2U", header)
        self.assertIn("critical_object_t metadata", source)
        self.assertIn("RESILIENT_PAGE_LOCK_RETRY_LIMIT", source)
        for forbidden in ("k_malloc", "k_free", "vfs_", "map_page(",
                          "unmap_page(", "dma_"):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, source)
        self.assertIn("simulierte", architecture)
        self.assertIn("beweist weder physische DIMM", architecture)

    def test_copy_on_write_precedes_metadata_commit(self) -> None:
        source = (ROOT / "kernel/init/resilient_page.c").read_text(
            encoding="utf-8")
        write_start = source.index("resilient_page_result_t resilient_page_write(")
        write_end = source.index("resilient_page_result_t resilient_page_scrub(")
        write = source[write_start:write_end]
        self.assertIn("inactive_bank", write)
        self.assertIn("page_crc32", write)
        self.assertIn("metadata_update_locked", write)
        self.assertLess(write.index("inactive_bank"),
                        write.index("metadata_update_locked"))
        metadata_commit = source[
            source.index("static resilient_page_result_t metadata_update_locked("):
            source.index("static resilient_page_result_t resolve_locked(")
        ]
        self.assertIn("critical_object_update", metadata_commit)
        self.assertIn("RESILIENT_PAGE_TEST_FAULT_WRITE_BEFORE_COMMIT", write)


if __name__ == "__main__":
    unittest.main()
