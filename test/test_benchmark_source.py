#!/usr/bin/env python3
"""Source and packaging contract for the bounded Ring-3 benchmark."""

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class BenchmarkSourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = (ROOT / "userspace/programs/benchmark.c").read_text(
            encoding="utf-8"
        )
        cls.builder = (ROOT / "scripts/build_system_programs.py").read_text(
            encoding="utf-8"
        )
        cls.windows = (ROOT / "scripts/build-windows.ps1").read_text(
            encoding="utf-8"
        )
        cls.makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        cls.shell = (ROOT / "userspace/bin/shell.c").read_text(
            encoding="utf-8"
        )

    def test_all_workloads_are_compile_time_bounded(self) -> None:
        for token in (
            "BENCHMARK_CPU_MAX_ITERATIONS",
            "BENCHMARK_CPU_ATTEMPTS",
            "BENCHMARK_MAX_ELAPSED_MS",
            "BENCHMARK_MEMORY_BYTES",
            "BENCHMARK_MEMORY_MAX_PASSES",
            "BENCHMARK_DISK_CHUNK_BYTES",
            "BENCHMARK_DISK_CHUNKS",
            "BENCHMARK_VGA_MAX_FRAMES",
            "BENCHMARK_VGA_ATTEMPTS",
        ):
            self.assertIn(token, self.source)
        self.assertNotIn("while (1", self.source)
        self.assertNotIn("for (;;)", self.source)

    def test_disk_file_is_private_verified_and_cleaned_up(self) -> None:
        self.assertIn('"REIST-BENCH-"', self.source)
        self.assertIn("x86os_process_identity(&identity)", self.source)
        self.assertIn("int descriptor = x86os_create(path);", self.source)
        self.assertNotIn("X86OS_O_TRUNC", self.source)
        self.assertIn("verify_disk_chunk(chunk)", self.source)
        self.assertIn("x86os_fsync(descriptor)", self.source)
        self.assertIn("x86os_unlink(path)", self.source)
        for forbidden in (
            "x86os_storage_block_write",
            "x86os_device_open_region",
            "x86os_device_bind_dma",
            "x86os_device_bind_irq",
        ):
            self.assertNotIn(forbidden, self.source)

    def test_vga_uses_public_api_and_restores_console(self) -> None:
        self.assertIn("x86os_display_info(&display)", self.source)
        self.assertIn("x86os_fill_rect(", self.source)
        self.assertIn("if (touched) x86os_clear();", self.source)
        self.assertLess(
            self.source.index("benchmark_result_t vga = benchmark_vga();"),
            self.source.index('x86os_puts("REIST OS System Benchmark'),
        )

    def test_ascii_table_reports_every_requested_area(self) -> None:
        self.assertIn(
            '"+----------+----------------------+------------------+----------+\\n"',
            self.source,
        )
        for area in ('"CPU"', '"RAM"', '"HDD"', '"VGA"'):
            self.assertIn(f"print_result_row({area}", self.source)
        self.assertIn('return "N/V"', self.source)
        self.assertIn('return "FEHLER"', self.source)

    def test_program_is_installed_in_both_image_layouts(self) -> None:
        self.assertIn(
            '"BENCHMARK.PRG": ROOT / "userspace/programs/benchmark.c"',
            self.builder,
        )
        self.assertEqual(
            self.windows.count(
                "'usr/bin/benchmark.prg' = 'BENCHMARK.PRG'"
            ),
            1,
        )
        self.assertEqual(
            self.makefile.count(
                "usr/bin/benchmark.prg=$(SYSTEM_PROGRAM_DIR)/BENCHMARK.PRG"
            ),
            1,
        )
        self.assertIn('"/bin", "/sbin", "/usr/bin", "/usr/gui/bin"',
                      self.shell)


if __name__ == "__main__":
    unittest.main()
