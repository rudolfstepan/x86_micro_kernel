import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from build_user_sdk import extract_wapcaplet, LIBC_INCLUDE_ROOT, WAPCAPLET_SHA256
from build_user_program import find_zig


class LibcTests(unittest.TestCase):
    def test_headers_are_cpp_includable(self):
        source = '#include <stdlib.h>\n#include <string.h>\n#include <errno.h>\n#include <assert.h>\n'
        env = os.environ.copy()
        env["ZIG_GLOBAL_CACHE_DIR"] = str(ROOT / "build/codex-agent/libc-host/zig-global")
        env["ZIG_LOCAL_CACHE_DIR"] = str(ROOT / "build/codex-agent/libc-host/cpp-local")
        with tempfile.TemporaryDirectory() as tmp:
            fixture = Path(tmp) / "headers.cpp"
            output = Path(tmp) / "headers.o"
            fixture.write_text(source, encoding="ascii")
            subprocess.run([str(find_zig()), "cc", "-std=c++17",
                            "-c", "-I" + str(LIBC_INCLUDE_ROOT), str(fixture),
                            "-o", str(output)],
                           cwd=ROOT, env=env, check=True, timeout=30)
            self.assertTrue(output.is_file())
            self.assertGreater(output.stat().st_size, 0)

    def test_guest_flag_is_appended_without_changing_qemu_options(self):
        import inspect
        import run_qemu_smoke as smoke
        self.assertEqual(list(inspect.signature(smoke.run).parameters)[-1], "expect_libc_client")
        self.assertNotIn("expect_libc_client", inspect.signature(smoke.qemu_command).parameters)
        script = (ROOT / "scripts/test-reist-runtime.ps1").read_text()
        self.assertIn("'libc-client'", script)
        self.assertIn("'--expect-libc-client'", script)

    def test_real_heap_bytes_and_upstream_oom(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            vendor = extract_wapcaplet(directory / "upstream")
            # Prefix all implemented CRT symbols in host objects. The Windows
            # loader and host stdio keep their own allocation domain.
            symbols = ("malloc calloc realloc free memcpy memmove memset memcmp "
                       "memchr strlen strcmp strncmp strchr strrchr").split()
            executable = directory / "libc-host.exe"
            env = os.environ.copy()
            env["ZIG_GLOBAL_CACHE_DIR"] = str(ROOT / "build/codex-agent/libc-host/zig-global")
            env["ZIG_LOCAL_CACHE_DIR"] = str(directory / "zig-local")
            command = [str(find_zig()), "cc", "-std=c11", "-O1", "-fno-builtin",
                       "-Wall", "-Wextra", "-Werror", "-UNDEBUG",
                       *["-D" + name + "=reist_test_" + name for name in symbols],
                       "-I" + str(LIBC_INCLUDE_ROOT), "-I" + str(vendor / "include"),
                       str(ROOT / "test/test_libc_host.c"),
                       str(ROOT / "userspace/libc/lib/bytes.c"),
                       str(vendor / "src/libwapcaplet.c"), "-o", str(executable)]
            subprocess.run(command, check=True, env=env, cwd=ROOT, timeout=90)
            result = subprocess.run([str(executable)], check=True, cwd=ROOT,
                                    capture_output=True, text=True, timeout=40)
            self.assertIn("REIST_LIBC_HOST_OK", result.stdout)

    def test_pin_and_no_implicit_authority(self):
        self.assertEqual((ROOT / "third_party/libwapcaplet.sha256").read_text().split()[0],
                         WAPCAPLET_SHA256)
        heap = (ROOT / "userspace/libc/lib/heap.c").read_text()
        for forbidden in ("x86os_", "ipc_", "reist_vfs_", "sbrk(", "mmap("):
            self.assertNotIn(forbidden, heap)


if __name__ == "__main__":
    unittest.main()
