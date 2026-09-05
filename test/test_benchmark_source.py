#!/usr/bin/env python3
"""Source and packaging contract for the bounded Ring-3 benchmark."""

import unittest
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from run_qemu_benchmark import read_rate_meets_minimum


class BenchmarkSourceTests(unittest.TestCase):
    def test_read_performance_requirement_fails_closed(self):
        self.assertTrue(read_rate_meets_minimum("400.00 KiB/s", 400))
        self.assertTrue(read_rate_meets_minimum("800.25 KiB/s", 400))
        for value in ("399.99 KiB/s", "-", "NaN KiB/s", "Inf KiB/s", "400 MB/s"):
            self.assertFalse(read_rate_meets_minimum(value, 400), value)
        self.assertFalse(read_rate_meets_minimum("800.00 KiB/s", float("nan")))
        self.assertFalse(read_rate_meets_minimum("800.00 KiB/s", -1))

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
        cls.runtime = (ROOT / "scripts/run_qemu_benchmark.py").read_text(
            encoding="utf-8"
        )

    def test_all_workloads_are_compile_time_bounded(self) -> None:
        for token in (
            "BENCHMARK_CPU_MAX_ITERATIONS",
            "BENCHMARK_CPU_ATTEMPTS",
            "BENCHMARK_CPU_MAX_WORKERS",
            "BENCHMARK_CPU_START_DELAY_MS",
            "BENCHMARK_MAX_ELAPSED_MS",
            "BENCHMARK_MEMORY_BYTES",
            "BENCHMARK_MEMORY_MAX_PASSES",
            "BENCHMARK_DISK_CHUNK_BYTES",
            "BENCHMARK_DISK_CHUNKS",
            "BENCHMARK_DISK_MAX_ELAPSED_MS",
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
        self.assertLess(
            self.source.index("x86os_unlink(path)"),
            self.source.index(
                "BENCHMARK_STATUS phase=hdd-cleanup state=complete"),
        )
        for forbidden in (
            "x86os_storage_block_write",
            "x86os_device_open_region",
            "x86os_device_bind_dma",
            "x86os_device_bind_irq",
        ):
            self.assertNotIn(forbidden, self.source)

    def test_status_identifies_each_phase_and_bounded_disk_progress(self) -> None:
        main_markers = (
            "BENCHMARK_STATUS phase=cpu",
            "BENCHMARK_STATUS phase=cpu-single",
            "BENCHMARK_STATUS phase=cpu-multi",
            "BENCHMARK_STATUS phase=ram-write",
            "BENCHMARK_STATUS phase=ram-read",
            "BENCHMARK_STATUS phase=vga",
            "BENCHMARK_STATUS phase=complete",
        )
        positions = [self.source.index(marker) for marker in main_markers]
        self.assertEqual(positions, sorted(positions))
        disk_markers = (
            "BENCHMARK_STATUS phase=hdd-create",
            "BENCHMARK_STATUS phase=hdd-write progress_kib=0",
            "BENCHMARK_STATUS phase=hdd-fsync",
            "BENCHMARK_STATUS phase=hdd-read progress_kib=0",
            "BENCHMARK_STATUS phase=hdd-cleanup",
        )
        positions = [self.source.index(marker) for marker in disk_markers]
        self.assertEqual(positions, sorted(positions))
        for marker in (
            "BENCHMARK_STATUS phase=hdd-failed step=fsync",
            "BENCHMARK_STATUS phase=hdd-failed step=write-clock",
            "BENCHMARK_STATUS phase=hdd-failed step=write-duration",
        ):
            self.assertIn(marker, self.source)
        for phase in ("hdd-write", "hdd-read"):
            for progress in (64, 128, 192, 256):
                self.assertEqual(
                    self.source.count(
                        f"BENCHMARK_STATUS phase={phase} "
                        f"progress_kib={progress} total_kib=256"
                    ),
                    1,
                )
        self.assertIn(
            "BENCHMARK_DISK_CHUNK_BYTES (64U * 1024U)", self.source
        )
        self.assertIn("BENCHMARK_DISK_CHUNKS 4U", self.source)
        self.assertIn("BENCHMARK_DISK_PROGRESS_CHUNKS 1U", self.source)
        self.assertIn("BENCHMARK_DISK_MAX_ELAPSED_MS 300000U", self.source)
        disk = self.source[
            self.source.index("static void benchmark_disk("):
            self.source.index("static benchmark_result_t benchmark_vga(")
        ]
        self.assertGreaterEqual(
            disk.count("BENCHMARK_DISK_MAX_ELAPSED_MS"), 2)
        self.assertIn("BENCHMARK_STATUS phase=hdd-failed step=write-time-limit",
                      disk)
        self.assertIn("BENCHMARK_STATUS phase=hdd-failed step=read-time-limit",
                      disk)
        self.assertIn("timed_status(progress, &write_status_ms)", self.source)
        self.assertIn("write_elapsed_with_status - write_status_ms", self.source)

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
        for cpu_test in ('"Single CPU"', '"Multi CPU gesamt"',
                         '"Multi/Single"'):
            self.assertIn(cpu_test, self.source)
        self.assertIn('return "N/V"', self.source)
        self.assertIn('return "FEHLER"', self.source)

    def test_cpu_scaling_uses_matched_synchronized_bounded_work(self) -> None:
        self.assertIn("x86os_cpu_topology(&topology)", self.source)
        self.assertIn("topology.online_cpu_count > BENCHMARK_CPU_MAX_WORKERS",
                      self.source)
        self.assertIn("shared_start = now + BENCHMARK_CPU_START_DELAY_MS",
                      self.source)
        self.assertIn('"--cpu-worker"', self.source)
        self.assertIn("x86os_spawnv(arguments[0], 4, arguments)", self.source)
        self.assertIn("x86os_wait(children[index], &status)", self.source)
        self.assertIn("cpu_work(iterations)", self.source)
        self.assertIn("BENCHMARK_CPU_OPERATIONS_PER_ITERATION * "
                      "topology.online_cpu_count", self.source)
        self.assertLess(
            self.source.index("x86os_spawnv(arguments[0], 4, arguments)"),
            self.source.index("elapsed_ms(shared_start, &elapsed)"))

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

    def test_runtime_gate_is_smp_selectable_bounded_and_checks_cleanup(self) -> None:
        self.assertIn('persistent=True, smp=smp', self.runtime)
        self.assertIn('parser.add_argument("--smp", type=int, default=1)',
                      self.runtime)
        self.assertIn('if args.smp < 1 or args.smp > 16:', self.runtime)
        self.assertIn('shutil.copyfile(image, clone)', self.runtime)
        self.assertIn('clone.unlink(missing_ok=True)', self.runtime)
        self.assertIn('default=120.0', self.runtime)
        self.assertIn('smoke.stop_process(process)', self.runtime)
        self.assertIn(
            '"BENCHMARK_STATUS phase=hdd-cleanup state=complete"',
            self.runtime,
        )
        self.assertIn('"Seq. Schreiben", "Seq. Lesen"', self.runtime)
        self.assertIn('last_status=', self.runtime)
        self.assertIn('"Single CPU", "Multi CPU gesamt", "Multi/Single"',
                      self.runtime)
        self.assertIn("CPU_ROW_PATTERN", self.runtime)


if __name__ == "__main__":
    unittest.main()
