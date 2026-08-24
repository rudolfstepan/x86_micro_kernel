"""Source contract for bounded process-local standard descriptors."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


class StandardDescriptorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = read("kernel/proc/process.h")
        cls.process = read("kernel/proc/process.c")
        cls.syscall = read("kernel/syscall/syscall_table.c")
        cls.sdk = read("userspace/sdk/include/x86os.h")
        cls.guest = read("userspace/programs/guest_test.c")

    def test_fixed_layout_preserves_dynamic_descriptor_range(self) -> None:
        self.assertIn("#define MAX_PROCESS_FILES 8", self.header)
        self.assertIn("#define PROCESS_STANDARD_DESCRIPTOR_COUNT 3", self.header)
        self.assertIn("MAX_PROCESS_DESCRIPTORS", self.header)
        self.assertIn("process_file_t files[MAX_PROCESS_DESCRIPTORS]", self.header)
        self.assertIn("i = PROCESS_FD_BASE; i < MAX_PROCESS_DESCRIPTORS", self.process)
        self.assertIn("return slot;", self.process)
        self.assertNotIn("return slot + PROCESS_FD_BASE", self.process)

    def test_each_process_generation_gets_directional_standard_entries(self) -> None:
        self.assertIn("initialize_standard_descriptors(process);", self.process)
        self.assertIn("PROCESS_DESCRIPTOR_TERMINAL_INPUT", self.process)
        self.assertGreaterEqual(
            self.process.count("PROCESS_DESCRIPTOR_TERMINAL_OUTPUT"), 3
        )
        self.assertIn(".readable = true", self.process)
        self.assertIn(".writable = true", self.process)
        self.assertIn("process_descriptor_validate_access", self.process)

    def test_stdin_is_one_nonblocking_poll(self) -> None:
        read_start = self.syscall.index("static int syscall_read(")
        read_end = self.syscall.index("static int syscall_close(", read_start)
        body = self.syscall[read_start:read_end]
        self.assertEqual(body.count("getchar_nonblocking()"), 1)
        self.assertIn("return -11; /* EAGAIN: bounded terminal poll. */", body)
        self.assertNotIn("while", body.split("PROCESS_DESCRIPTOR_TERMINAL_INPUT")[1].split("if (kind !=")[0])

    def test_sdk_and_guest_prove_rights_and_independent_close(self) -> None:
        self.assertIn("X86OS_STDIN_FILENO = 0", self.sdk)
        self.assertIn("X86OS_STDOUT_FILENO = 1", self.sdk)
        self.assertIn("X86OS_STDERR_FILENO = 2", self.sdk)
        for marker in (
            "STDOUT_FD_OK",
            "STDERR_FD_OK",
            "STDERR_AFTER_STDOUT_CLOSE_OK",
            "TEST_STAGE STANDARD_DESCRIPTORS_OK",
        ):
            self.assertIn(marker, self.guest)
        self.assertIn('x86os_open("/readme.txt")', self.guest)
        self.assertIn("descriptor != 3", self.guest)
        self.assertIn("x86os_close(X86OS_STDOUT_FILENO) != -9", self.guest)


if __name__ == "__main__":
    unittest.main()
