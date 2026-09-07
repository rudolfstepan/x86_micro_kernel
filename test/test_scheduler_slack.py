"""Execute the actual scheduler selection/accounting/yield code, not a model."""
import pathlib
import importlib.util
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class SchedulerSlackTests(unittest.TestCase):
    def test_guest_contract_and_existing_shell_packaging(self):
        guest = (ROOT / "userspace/programs/guest_test.c").read_text(encoding="utf-8")
        work = function(guest, "static int test_scheduler_slack_work(")
        self.assertIn("sample < 4000000U", work)
        self.assertIn("now - start >= 1000U", work)
        self.assertIn("adjacent_ms >= 400U", work)
        self.assertIn('static char metric[] = "SCHED_SLACK_METRIC', work)
        self.assertIn('text_equal(argv[1], "sched-slack")', guest)
        self.assertIn('wait_for_expected("FAULTUD.PRG", 134)', guest)
        for path, marker in (
            ("scripts/build_system_programs.py", 'ROOT / "userspace/programs/guest_test.c"'),
            ("scripts/build-windows.ps1", "'libexec/reist/gtest.prg' = 'GTEST.PRG'"),
            ("Makefile", "libexec/reist/gtest.prg=$(SYSTEM_PROGRAM_DIR)/GTEST.PRG"),
            ("userspace/bin/shell.c", "x86os_spawnv(executable, argc, child_argv)"),
        ):
            self.assertTrue(marker in (ROOT / path).read_text(encoding="utf-8"), path)

    def test_guest_metrics_fail_closed(self):
        sys.path.insert(0, str(ROOT / "scripts"))
        spec = importlib.util.spec_from_file_location(
            "slack_runner", ROOT / "scripts/run_qemu_scheduler_slack.py")
        runner = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(runner)
        prefix = "SCHED_SLACK_METRIC adjacent_ms="
        self.assertEqual(runner.validate_metrics(prefix + "0400\n" + prefix + "1000\n"),
                         [400, 1000])
        for text in ("", prefix + "0800\n", (prefix + "0399\n") * 2,
                     (prefix + "1001\n") * 2, (prefix + "0800\n") * 3):
            with self.assertRaises(ValueError):
                runner.validate_metrics(text)

    def test_real_scheduler_o0_o2(self):
        compiler = shutil.which("gcc")
        self.assertIsNotNone(compiler, "GCC with its runtime DLLs is required")
        source = (ROOT / "kernel/sched/scheduler.c").read_text(encoding="utf-8")
        header = (ROOT / "kernel/sched/scheduler.h").read_text(encoding="utf-8")
        declarations = "\n".join(re.findall(
            r"^#define (?:MAX_TASKS|TASK_\w+|SCHEDULER_HELD_MUTEX_CAPACITY)\b.*",
            header, re.M))
        # Real task/context definitions; only architecture/resource types are stubbed.
        context_start = header.index("typedef struct {", header.index("} runtime_timing_stats_t;"))
        declarations += "\n" + header[context_start:header.index("} task_t;") + len("} task_t;")]
        globals_start = source.index("static scheduler_window_t cpu_windows[")
        globals_end = source.index("static uint32_t peak_active_tasks;", globals_start)
        bodies = "\n\n".join(function(source, name) for name in (
            "static uint32_t scheduler_cpu_policy_index_locked(",
            "static void refresh_effective_classes_locked(",
            "static void account_current_runtime_locked(",
            "static int find_next_runnable(",
            "static bool claim_task_for_current_cpu(",
            "static int claim_next_runnable(",
            "int scheduler_yield(",
        ))
        harness = (ROOT / "test/test_scheduler_slack_host.c").read_text(encoding="utf-8")
        harness = harness.replace("/* REAL_TYPES */", declarations)
        harness = harness.replace("/* REAL_GLOBALS */", source[globals_start:globals_end])
        harness = harness.replace("/* REAL_FUNCTIONS */", bodies)
        with tempfile.TemporaryDirectory() as directory:
            unit = pathlib.Path(directory) / "scheduler.c"
            unit.write_text(harness, encoding="utf-8")
            for opt in ("-O0", "-O2"):
                with self.subTest(optimization=opt):
                    exe = pathlib.Path(directory) / (opt[1:] + ".exe")
                    result = subprocess.run([
                        compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", opt,
                        "-I", str(ROOT), str(unit),
                        str(ROOT / "kernel/sched/scheduling_policy.c"), "-o", str(exe),
                    ], capture_output=True, text=True, timeout=30)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    for mode in ("slack", "accounting", "selection", "faults"):
                        with self.subTest(mode=mode):
                            result = subprocess.run([str(exe), mode], capture_output=True,
                                                    text=True, timeout=10)
                            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                            self.assertIn("SCHEDULER_SLACK_OK", result.stdout)


if __name__ == "__main__":
    unittest.main()
