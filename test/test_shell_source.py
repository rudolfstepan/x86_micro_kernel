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

    def test_shell_services_floppy_motor_idle_timeout(self):
        self.assertIn("fdd_service();", self.source)

    def test_prg_filename_is_executed_without_run_command(self):
        self.assertIn("static bool is_program_filename", self.source)
        self.assertRegex(
            self.source,
            r"(?s)if \(is_program_filename\(original_command\)\).*?"
            r"cmd_run\(arg_cnt \+ 1, program_arguments\);",
        )

    def test_native_programs_receive_command_line_arguments(self):
        self.assertNotIn("Program arguments are not supported yet.", self.source)
        self.assertIn("create_process_for_file_args", self.source)
        self.assertIn("arguments, working_directory", self.source)

    def test_ls_is_a_userspace_program_not_a_kernel_builtin(self):
        self.assertNotIn('{"LS", cmd_ls}', self.source)
        programs = (ROOT / "scripts/build_system_programs.py").read_text(
            encoding="utf-8"
        )
        self.assertIn('"LS.PRG"', programs)

    def test_basic_is_a_userspace_program_not_a_kernel_builtin(self):
        self.assertNotIn('{"BASIC", cmd_basic}', self.source)
        self.assertNotIn('#include "userspace/bin/basic.h"', self.source)
        programs = (ROOT / "scripts/build_system_programs.py").read_text(
            encoding="utf-8"
        )
        self.assertIn('"BASIC.PRG"', programs)

    def test_prompt_has_no_trailing_space(self):
        self.assertIn('printf("%s:%s>", drive_label, dos_path);', self.source)
        self.assertNotIn('printf("%s:%s> ", drive_label, dos_path);', self.source)

    def test_program_extension_is_optional_at_prompt(self):
        self.assertIn("static bool try_run_program_without_extension", self.source)
        self.assertIn('strcpy(program_name + length, ".PRG");', self.source)
        self.assertIn(
            "try_run_program_without_extension(original_command,",
            self.source,
        )

    def test_run_waits_for_foreground_program(self):
        self.assertIn("wait_for_process(pid);", self.source)


if __name__ == "__main__":
    unittest.main()
