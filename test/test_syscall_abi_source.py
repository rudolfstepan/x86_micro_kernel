"""Contract tests for the single append-only syscall ABI source."""

from pathlib import Path
import hashlib
import re
import subprocess
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


class SyscallAbiSourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.common = read("include/reist/abi/syscall.h")
        cls.kernel = read("lib/libc/stdlib.h")
        cls.sdk = read("userspace/sdk/include/x86os.h")
        cls.generator = read("scripts/generate_syscall_abi.py")

    def test_common_list_is_complete_append_only_v1(self) -> None:
        self.assertIn("REIST_SYSCALL_ABI_VERSION 1U", self.common)
        self.assertIn("REIST_SYSCALL_COUNT 121U", self.common)
        entries = re.findall(
            r"^\s*X\(([A-Z][A-Z0-9_]*), ([A-Z][A-Z0-9_]*), "
            r"([0-9]+)U\)", self.common, re.MULTILINE)
        self.assertEqual(len(entries), 121)
        self.assertEqual([int(item[2]) for item in entries], list(range(121)))
        self.assertEqual(len({item[0] for item in entries}), 121)
        self.assertEqual(len({item[1] for item in entries}), 121)
        self.assertEqual(entries[8],
                         ("INSTALL_IRQ", "RESERVED_INSTALL_IRQ", "8"))
        self.assertEqual(entries[90], ("ADMIN_STORAGE", "ADMIN_STORAGE", "90"))
        self.assertEqual(entries[91],
                         ("COMPONENT_CONTROL", "COMPONENT_CONTROL", "91"))
        self.assertEqual(entries[120], ("OPEN_FLAGS", "OPEN_FLAGS", "120"))

    def test_generated_compatibility_names_remain_exact(self) -> None:
        entries = re.findall(
            r"^\s*X\(([A-Z][A-Z0-9_]*), ([A-Z][A-Z0-9_]*), "
            r"([0-9]+)U\)", self.common, re.MULTILINE)
        for kernel_name, sdk_name, number in entries:
            self.assertRegex(
                self.kernel, rf"(?m)^#define SYS_{kernel_name} {number}(?: |$)")
            self.assertRegex(
                self.sdk, rf"(?m)^\s*X86OS_SYS_{sdk_name} = {number}(?:,|$)")
        self.assertEqual(
            self.kernel.count("BEGIN GENERATED REIST SYSCALLS"), 1)
        self.assertEqual(self.sdk.count("BEGIN GENERATED REIST SYSCALLS"), 1)

    def test_check_mode_is_read_only_and_in_sync(self) -> None:
        paths = [ROOT / "lib/libc/stdlib.h",
                 ROOT / "userspace/sdk/include/x86os.h",
                 ROOT / "userspace/sdk/include/reist/abi/syscall.h"]
        before = [hashlib.sha256(path.read_bytes()).digest() for path in paths]
        result = subprocess.run(
            [sys.executable, "scripts/generate_syscall_abi.py", "--check"],
            cwd=ROOT, capture_output=True, text=True, timeout=10, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("SYSCALL_ABI_CHECK_OK count=121", result.stdout)
        after = [hashlib.sha256(path.read_bytes()).digest() for path in paths]
        self.assertEqual(after, before)
        self.assertIn("replacements != 1", self.generator)

    def test_common_errno_subset_uses_standard_values(self) -> None:
        expected = {
            "EPERM": 1, "ENOENT": 2, "EIO": 5, "EBADF": 9,
            "EAGAIN": 11, "ENOMEM": 12, "EACCES": 13, "EFAULT": 14,
            "EINVAL": 22, "EMFILE": 24, "ENOSPC": 28, "EROFS": 30,
            "ENAMETOOLONG": 36, "EOVERFLOW": 75, "ENOTSUP": 95,
            "ETIMEDOUT": 110, "ESTALE": 116,
        }
        for name, number in expected.items():
            self.assertRegex(self.common,
                             rf"\bREIST_{name}\s*=\s*{number}(?:,|\b)")

    def test_both_build_paths_fail_closed_on_drift(self) -> None:
        makefile = read("Makefile")
        windows = read("scripts/build-windows.ps1")
        self.assertIn("check-syscall-abi:", makefile)
        self.assertIn("scripts/generate_syscall_abi.py --check", makefile)
        self.assertIn("| prepare check-syscall-abi", makefile)
        self.assertIn("'scripts/generate_syscall_abi.py', '--check'", windows)
        self.assertIn("Shared syscall ABI check failed", windows)


if __name__ == "__main__":
    unittest.main()
