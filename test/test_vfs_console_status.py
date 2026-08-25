import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class VfsConsoleStatusTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = (ROOT / "fs" / "vfs" / "filesystem.c").read_text(
            encoding="utf-8"
        )
        start = cls.source.index("void auto_mount_all_drives(")
        cls.auto_mount = cls.source[start:]

    def test_boot_summary_has_no_terminal_control_sequences(self):
        self.assertNotIn("\\x1B", self.auto_mount)
        self.assertNotIn("\\033", self.auto_mount)
        self.assertNotIn("ANSI color", self.auto_mount)

    def test_counts_and_failure_state_remain_visible(self):
        self.assertIn(
            'printf("\\nFilesystem Status: %d/%d drives mounted",',
            self.auto_mount,
        )
        self.assertIn("mounted_count, drive_count", self.auto_mount)
        self.assertIn("if (failed_count > 0)", self.auto_mount)
        self.assertIn('printf(" (%d failed)", failed_count);', self.auto_mount)
        self.assertIn('printf("Active drive: %s\\n"', self.auto_mount)

    def test_plain_output_contract_is_explicit(self):
        self.assertIn("framebuffer and serial sinks share this stream",
                      self.auto_mount)
        self.assertIn("terminal-only ANSI bytes", self.auto_mount)


if __name__ == "__main__":
    unittest.main()
