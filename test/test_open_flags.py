"""Contract tests for append-only open flags and descriptor rights."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


class OpenFlagsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.common = read("include/reist/abi/syscall.h")
        cls.header = read("kernel/proc/process.h")
        cls.process = read("kernel/proc/process.c")
        cls.syscall = read("kernel/syscall/syscall_table.c")
        cls.sdk_h = read("userspace/sdk/include/x86os.h")
        cls.sdk_c = read("userspace/sdk/x86os.c")
        cls.guest = read("userspace/programs/guest_test.c")

    def test_syscall_is_append_only_and_legacy_entries_remain(self) -> None:
        self.assertIn("X(OPEN_FLAGS, OPEN_FLAGS, 120U)", self.common)
        self.assertIn("X86OS_SYS_OPEN = 14", self.sdk_h)
        self.assertIn("X86OS_SYS_CREATE = 19", self.sdk_h)
        self.assertIn("X86OS_SYS_OPEN_FLAGS = 120", self.sdk_h)
        self.assertIn("(void*)&syscall_open_flags", self.syscall)
        self.assertIn("case SYS_OPEN_FLAGS:", self.syscall)

    def test_public_flags_use_standard_values(self) -> None:
        expected = {
            "X86OS_O_RDONLY": "0x0000U",
            "X86OS_O_WRONLY": "0x0001U",
            "X86OS_O_RDWR": "0x0002U",
            "X86OS_O_ACCMODE": "0x0003U",
            "X86OS_O_CREAT": "0x0040U",
            "X86OS_O_TRUNC": "0x0200U",
            "X86OS_O_APPEND": "0x0400U",
        }
        for name, value in expected.items():
            self.assertRegex(self.sdk_h, rf"{name}\s+{re.escape(value)}")
        self.assertIn("int x86os_open_flags(const char* path, uint32_t flags)",
                      self.sdk_c)

    def test_validation_precedes_path_and_filesystem_work(self) -> None:
        start = self.process.index("int process_file_open_flags(")
        end = self.process.index("int process_file_read(", start)
        body = self.process[start:end]
        validation = body.index("PROCESS_OPEN_ALLOWED_FLAGS")
        slot = body.index("PROCESS_FD_BASE")
        resolution = body.index("process_resolve_path")
        opening = body.index("vfs_open")
        self.assertLess(validation, slot)
        self.assertLess(slot, resolution)
        self.assertLess(resolution, opening)
        self.assertIn("return -REIST_ENOTSUP", body)
        self.assertLess(body.index("return -REIST_ENOTSUP"), resolution)
        self.assertIn("created", body)
        self.assertIn("vfs_delete(resolved)", body)

    def test_rights_append_and_profile_default_deny_are_explicit(self) -> None:
        self.assertIn("bool append;", self.header)
        self.assertIn("write_offset = file->node->size;", self.process)
        self.assertIn(".readable = readable", self.process)
        self.assertIn(".writable = writable", self.process)
        self.assertNotIn("SYS_OPEN_FLAGS,", self.process)
        self.assertIn("PROCESS_DOMAIN_SYSCALL_LIMIT 121U", self.header)

    def test_guest_proves_rights_append_validation_and_no_truncation(self) -> None:
        for marker in (
            "TEST_STAGE OPEN_FLAGS_OK",
            "X86OS_O_CREAT | X86OS_O_RDWR",
            "X86OS_O_WRONLY | X86OS_O_APPEND",
            "X86OS_O_RDONLY | X86OS_O_APPEND",
            "X86OS_O_WRONLY | X86OS_O_TRUNC",
        ):
            self.assertIn(marker, self.guest)
        self.assertRegex(self.guest, r"!=\s+-REIST_EBADF")
        self.assertRegex(self.guest, r"!=\s+-REIST_EINVAL")
        self.assertRegex(self.guest, r"!=\s+-REIST_ENOTSUP")


if __name__ == "__main__":
    unittest.main()
