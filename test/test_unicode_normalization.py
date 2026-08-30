"""Unicode 15 NFC/full-casefold generation and bounded host behavior."""

from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unicodedata
import unittest


ROOT = Path(__file__).resolve().parents[1]
GCC = shutil.which("gcc")


class UnicodeNormalizationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.generator = (ROOT / "scripts/generate_unicode_tables.py").read_text(
            encoding="utf-8")
        cls.tables = (ROOT /
            "include/reist/unicode_tables_15_0.h").read_text(encoding="utf-8")
        cls.normalizer = (ROOT / "include/reist/unicode_norm.h").read_text(
            encoding="utf-8")
        cls.fat = (ROOT / "fs/fat32/fat32_files.c").read_text(encoding="utf-8")
        cls.shadow = (ROOT /
            "userspace/storage/lib/vfs_shadow_fat32.c").read_text(
                encoding="utf-8")

    @unittest.skipUnless(unicodedata.unidata_version == "15.0.0",
                         "Unicode 15.0 host database is required")
    def test_generated_table_is_reproducible(self) -> None:
        subprocess.run([sys.executable, "scripts/generate_unicode_tables.py",
                        "--check"], check=True, cwd=ROOT, capture_output=True)
        self.assertIn('#define REIST_UNICODE_DATA_VERSION "15.0.0"',
                      self.tables)
        self.assertEqual(self.tables.count("reist_unicode_mapping_t"), 3)

    def test_capacities_are_derived_from_all_unicode_scalars(self) -> None:
        self.assertIn("range(0x110000)", self.generator)
        self.assertIn("scalar_bound != 382", self.generator)
        self.assertIn("key_bound != 762", self.generator)
        self.assertIn("REIST_UNICODE_DECOMPOSED_CAPACITY 382U", self.tables)
        self.assertIn("REIST_UNICODE_KEY_CAPACITY 763U", self.tables)

    def test_runtime_is_fixed_storage_and_complete_algorithm(self) -> None:
        self.assertNotIn("malloc", self.normalizer)
        self.assertIn("reist_unicode_append_decomposed", self.normalizer)
        self.assertIn("reist_unicode_canonical_order", self.normalizer)
        self.assertIn("reist_unicode_canonical_compose", self.normalizer)
        self.assertIn("reist_unicode_casefolds", self.normalizer)
        self.assertIn("S_BASE = 0xAC00U", self.normalizer)
        self.assertIn("reist_unicode_caseless_nfc_equal", self.normalizer)

    def test_canonical_decomposition_uses_verified_fixed_pending_stack(self) -> None:
        start = self.normalizer.index(
            "static inline int reist_unicode_append_decomposed("
        )
        body = self.normalizer[
            start:self.normalizer.index("static inline void ", start)
        ]
        self.assertIn(
            "REIST_UNICODE_DECOMPOSITION_PENDING_CAPACITY 4U",
            self.normalizer,
        )
        self.assertIn(
            "pending[REIST_UNICODE_DECOMPOSITION_PENDING_CAPACITY]", body
        )
        self.assertEqual(body.count("reist_unicode_append_decomposed("), 1)

        data_text = self.tables.split(
            "static const uint32_t reist_unicode_decomposition_data[] = {", 1
        )[1].split("};", 1)[0]
        data = [
            int(value, 16)
            for value in re.findall(r"0x([0-9A-F]+)U", data_text)
        ]
        entry_text = self.tables.split(
            "static const reist_unicode_mapping_t "
            "reist_unicode_decompositions[] = {", 1
        )[1].split("};", 1)[0]
        mappings = {
            int(scalar, 16): (int(offset), int(length))
            for scalar, offset, length in re.findall(
                r"\{0x([0-9A-F]+)U, (\d+)U, (\d+)U\}", entry_text
            )
        }
        maximum_pending = 0
        for scalar in mappings:
            pending = [scalar]
            while pending:
                maximum_pending = max(maximum_pending, len(pending))
                current = pending.pop()
                mapping = mappings.get(current)
                if mapping is not None:
                    offset, length = mapping
                    pending.extend(reversed(data[offset:offset + length]))
        self.assertEqual(4, maximum_pending)
        self.assertLessEqual(maximum_pending, 4)

    @unittest.skipUnless(GCC, "gcc is required for Unicode host behavior")
    def test_full_script_plane_and_combining_behavior(self) -> None:
        source = r'''
#include "include/reist/unicode_norm.h"
typedef struct { const char *left; const char *right; int equal; } pair_t;
int main(void) {
    static const pair_t pairs[] = {
        {"\xC3\x85", "A\xCC\x8A", 1},
        {"\xC3\x84rger-\xC3\x9F", "a\xCC\x88RGER-SS", 1},
        {"\xCE\xA3", "\xCF\x83", 1},
        {"\xCF\x82", "\xCF\x83", 1},
        {"\xEF\xAC\x83", "ffi", 1},
        {"\xE2\x84\xAA", "k", 1},
        {"\xC4\xB0", "i\xCC\x87", 1},
        {"\xEA\xB0\x80", "\xE1\x84\x80\xE1\x85\xA1", 1},
        {"a\xCC\x95\xCC\x80", "\xC3\xA0\xCC\x95", 1},
        {"\xF0\x90\x90\x80", "\xF0\x90\x90\xA8", 1},
        {"\xF0\x9F\x9A\x80", "\xF0\x9F\x9A\x80", 1},
        {"a", "\xD0\xB0", 0},
        {"\xC3\xA4", "a", 0},
    };
    for (unsigned int i = 0U; i < sizeof(pairs) / sizeof(pairs[0]); ++i)
        if (reist_unicode_caseless_nfc_equal(pairs[i].left, pairs[i].right)
                != pairs[i].equal) return (int)i + 1;
    static const char malformed[] = "\xED\xA0\x80";
    if (reist_unicode_caseless_nfc_equal(malformed, malformed)) return 20;
    char key[REIST_UNICODE_KEY_CAPACITY];
    if (!reist_unicode_nfc_casefold_key("\xEF\xAC\x83", key) ||
        key[0] != 'f' || key[1] != 'f' || key[2] != 'i' || key[3] != 0)
        return 21;
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            source_path = Path(temporary) / "unicode_norm_test.c"
            executable = Path(temporary) / "unicode_norm_test.exe"
            source_path.write_text(source, encoding="utf-8")
            subprocess.run([GCC, "-std=c11", "-Wall", "-Wextra", "-Werror",
                            f"-I{ROOT}", str(source_path), "-o", str(executable)],
                           check=True, cwd=ROOT, capture_output=True)
            subprocess.run([str(executable)], check=True, cwd=ROOT,
                           capture_output=True)

    def test_both_fat_readers_use_normalized_identity_only(self) -> None:
        self.assertIn("static bool fat32_names_equal", self.fat)
        self.assertIn("reist_unicode_nfc_casefold_key(left,", self.fat)
        self.assertIn("reist_unicode_nfc_casefold_key(right,", self.fat)
        self.assertIn("workspace->left_key", self.fat)
        self.assertIn("workspace->right_key", self.fat)
        self.assertIn("reist_unicode_caseless_nfc_equal(left, right)",
                      self.shadow)


if __name__ == "__main__":
    unittest.main()
