"""Contract and host behavior tests for the R1.2a resilient-page proof."""

from __future__ import annotations

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class MemoryResilienceTests(unittest.TestCase):
    def test_boot_proof_is_compile_time_only_and_precedes_processes(self) -> None:
        header = (ROOT / "include/kernel/resilient_page.h").read_text(
            encoding="utf-8")
        source = (ROOT / "kernel/init/resilient_page.c").read_text(
            encoding="utf-8")
        kernel = (ROOT / "kernel/init/kernel.c").read_text(encoding="utf-8")
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        build_script = (ROOT / "scripts/build-windows.ps1").read_text(
            encoding="utf-8")
        runtime = (ROOT / "scripts/test-reist-runtime.ps1").read_text(
            encoding="utf-8")

        self.assertIn("REIST_RESILIENT_PAGE_BOOT_PROOF", header)
        proof_start = source.index("bool resilient_page_boot_proof(void)")
        proof = source[proof_start:]
        self.assertEqual(proof.count("resilient_page_create("), 2)
        self.assertIn("metadata.data_generation", source)
        self.assertIn("boot_metadata_matches(primary, 2U", proof)
        self.assertIn("RESILIENT_PAGE_DEGRADED", proof)
        self.assertIn("RESILIENT_PAGE_HEALTHY", proof)
        self.assertIn("REIST_RESILIENT_PAGE BOOT_PROOF_OK", proof)

        memory_test = kernel.index("test_memory();")
        boot_proof = kernel.index("resilient_page_boot_proof()")
        process_admission = kernel.index("supervisor_start_probe(")
        self.assertLess(memory_test, boot_proof)
        self.assertLess(boot_proof, process_admission)
        self.assertIn(
            "#ifndef REIST_RESILIENT_PAGE_BOOT_PROOF\n"
            "    if (!supervisor_start_compositor",
            kernel,
        )
        self.assertIn("RESILIENT_PAGE_BOOT_PROOF ?= 0", makefile)
        self.assertIn("-DREIST_RESILIENT_PAGE_BOOT_PROOF", makefile)
        self.assertIn("[switch]$ResilientPageBootProof", build_script)
        self.assertIn("RESILIENT_PAGE_BOOT_PROOF=1", build_script)
        self.assertIn("'memory-resilience'", runtime)
        self.assertIn("-ResilientPageBootProof", runtime)
        self.assertIn("'--expect-resilient-page-boot-proof'", runtime)
        self.assertIn("'--vmware-vga'", runtime)
        self.assertIn("'--timeout', '180'", runtime)

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
