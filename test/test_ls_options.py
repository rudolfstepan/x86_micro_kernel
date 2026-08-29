import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class LsOptionsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = (ROOT / "userspace/programs/ls.c").read_text(encoding="utf-8")

    def test_compact_columns_are_default_without_paging(self):
        self.assertIn("#define LS_COLUMNS 4U", self.source)
        self.assertIn("#define LS_COLUMN_WIDTH 19U", self.source)
        self.assertIn("ls_options_t options = {0};", self.source)
        self.assertIn("if (!options->pager", self.source)

    def test_compact_row_does_not_trigger_80_column_auto_wrap(self):
        self.assertNotIn("#define LS_COLUMN_WIDTH 20U", self.source)

    def test_linux_style_combined_short_options(self):
        for flag in "1Calph":
            self.assertIn("case '%s':" % flag, self.source)

    def test_paging_is_explicit_opt_in(self):
        self.assertIn('text_equal(argument, "--pager")', self.source)
        self.assertIn('text_equal(argument, "--no-pager")', self.source)

    def test_files_and_directories_are_supported(self):
        self.assertIn("target.type != X86OS_DIRECTORY", self.source)
        self.assertIn("reist_vfs_readdir_at(", self.source)
        self.assertNotIn("x86os_readdir", self.source)

    def test_directory_walk_has_one_absolute_bound(self):
        for token in ("LS_DIRECTORY_ENTRY_CAPACITY 128U",
                      "LS_DEADLINE_MS 5000U",
                      "LS_REQUEST_TIMEOUT_MS 1000U",
                      "x86os_monotonic_ms(",
                      "UINT64_MAX - started"):
            self.assertIn(token, self.source)
        self.assertIn("index >= LS_DIRECTORY_ENTRY_CAPACITY", self.source)


if __name__ == "__main__":
    unittest.main()
