import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
COMMAND_SOURCE = ROOT / "kernel" / "shell" / "command.c"


class ShellSourceRegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = COMMAND_SOURCE.read_text(encoding="utf-8")

    def test_command_names_are_read_only_and_never_uppercased_in_place(self):
        self.assertIn("static const command_t command_table", self.source)
        self.assertRegex(self.source, r"typedef struct\s*\{\s*const char \*name;")
        self.assertNotRegex(
            self.source,
            r"str_to_upper\s*\(\s*command_table\s*\[",
        )

    def test_type_uses_vfs_instead_of_legacy_fat_open_calls(self):
        match = re.search(
            r"void open_file\s*\([^)]*\)\s*\{(?P<body>.*?)\n\}",
            self.source,
            re.DOTALL,
        )
        self.assertIsNotNone(match)
        body = match.group("body")
        self.assertIn("vfs_open(", body)
        self.assertIn("vfs_read(", body)
        self.assertIn("vfs_close(", body)
        self.assertNotIn("fat32_open_file(", body)
        self.assertNotIn("fat12_open_file(", body)

    def test_shell_has_no_unconditional_debug_output(self):
        self.assertNotIn("[DEBUG]", self.source)

    def test_ansi_keys_are_decoded_across_input_loop_iterations(self):
        self.assertIn("SHELL_ESCAPE_SEEN", self.source)
        self.assertIn("SHELL_ESCAPE_CSI", self.source)
        match = re.search(
            r"static bool handle_escape_key\s*\([^)]*\)\s*\{"
            r"(?P<body>.*?)\n\}",
            self.source,
            re.DOTALL,
        )
        self.assertIsNotNone(match)
        self.assertNotIn("getchar_nonblocking(", match.group("body"))

    def test_ping_repeats_until_ctrl_c(self):
        match = re.search(
            r"void cmd_ping\s*\([^)]*\)\s*\{(?P<body>.*?)\n\}",
            self.source,
            re.DOTALL,
        )
        self.assertIsNotNone(match)
        body = match.group("body")
        self.assertIn("while (!interrupted)", body)
        self.assertIn("getchar_nonblocking()", body)
        self.assertIn("ch == 0x03", body)
        self.assertIn("ping statistics", body)


if __name__ == "__main__":
    unittest.main()
