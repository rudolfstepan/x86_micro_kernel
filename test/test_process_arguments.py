"""Execute the actual spawn snapshot and initial-stack code with faultable copies."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from build_user_program import find_zig
from test_memory_r12 import function_block


class ProcessArgumentTests(unittest.TestCase):
    def test_snapshot_and_stack_admission(self):
        table = (ROOT / "kernel/syscall/syscall_table.c").read_text()
        process = (ROOT / "kernel/proc/process.c").read_text()
        header = (ROOT / "kernel/proc/process.h").read_text()
        definitions = "\n".join(re.findall(r"^#define (?:PROCESS_ARGUMENT\w*|SYSCALL_MAX_ARGUMENTS|SYSCALL_ARGUMENT_CAPACITY) .*", header + "\n" + table, re.M))
        code = definitions + "\n"
        if "static int copy_spawn_argument(" in table:
            code += "static int copy_spawn_argument(char *out, size_t capacity, const char *in) " + function_block(table, "static int copy_spawn_argument(")
        code += "\nstatic int syscall_spawnv(const char *path, const char *const *argv, int argc) " + function_block(table, "static int syscall_spawnv(")
        code += "\nstatic int build_user_arguments(page_directory_t *pd, int argc, const char *const *argv, uint32_t *sp) " + function_block(process, "static int build_user_arguments(")
        # Preserve parameter names used by the production bodies.
        code = code.replace("const char *path, const char *const *argv, int argc)", "const char *user_path, const char *const *user_argv, int argc)")
        code = code.replace("page_directory_t *pd, int argc, const char *const *argv, uint32_t *sp)", "page_directory_t *page_directory, int argc, const char *const *argv, uint32_t *user_stack)")
        with tempfile.TemporaryDirectory(prefix="reist-arguments-") as directory:
            directory = Path(directory)
            source, executable = directory / "host.c", directory / "host.exe"
            source.write_text((ROOT / "test/test_process_arguments_host.c").read_text().replace("/* PRODUCTION */", code))
            env = os.environ.copy()
            env["ZIG_GLOBAL_CACHE_DIR"] = str(ROOT / "build/codex-agent/arguments-cache")
            env["ZIG_LOCAL_CACHE_DIR"] = str(directory / "cache")
            result = subprocess.run([str(find_zig()), "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(executable)], env=env, capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("PROCESS_ARGUMENTS_OK", result.stdout)


if __name__ == "__main__":
    unittest.main()
