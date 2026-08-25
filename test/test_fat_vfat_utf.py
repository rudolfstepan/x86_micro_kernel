"""Contracts and host behavior for bounded UTF-8/UTF-16 VFAT names."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
GCC = shutil.which("gcc")


class FatVfatUtfTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.codec = (ROOT / "include/reist/utf.h").read_text(encoding="utf-8")
        cls.fat = (ROOT / "fs/fat32/fat32.c").read_text(encoding="utf-8")
        cls.files = (ROOT / "fs/fat32/fat32_files.c").read_text(
            encoding="utf-8")
        cls.directory = (ROOT / "fs/fat32/fat32_dir.c").read_text(
            encoding="utf-8")
        cls.shadow = (ROOT /
            "userspace/storage/lib/vfs_shadow_fat32.c").read_text(
                encoding="utf-8")
        cls.guest = (ROOT / "userspace/programs/guest_test.c").read_text(
            encoding="utf-8")

    def test_codec_is_fixed_storage_and_rfc3629_bounded(self) -> None:
        self.assertNotIn("malloc", self.codec)
        self.assertIn("bytes[0] >= 0xC2U", self.codec)
        self.assertIn("bytes[0] == 0xE0U && bytes[1] < 0xA0U", self.codec)
        self.assertIn("bytes[0] == 0xEDU && bytes[1] >= 0xA0U", self.codec)
        self.assertIn("bytes[0] == 0xF4U && bytes[1] > 0x8FU", self.codec)
        self.assertIn("value >= 0xD800U && value <= 0xDFFFU", self.codec)

    @unittest.skipUnless(GCC, "gcc is required for the codec host behavior")
    def test_codec_roundtrip_and_rejects_malformed_sequences(self) -> None:
        source = r'''
#include "include/reist/utf.h"
#include <string.h>
int main(void) {
    static const char valid[] = "Gr\xC3\xBCn-\xF0\x9F\x9A\x80.txt";
    uint16_t units[32]; size_t count = 0U;
    if (!reist_utf8_to_utf16(valid, sizeof(valid) - 1U, units, 32U, &count) ||
        count != 11U || units[2] != 0x00FCU || units[5] != 0xD83DU ||
        units[6] != 0xDE80U) return 1;
    char output[64]; size_t bytes = 0U;
    if (!reist_utf16_to_utf8(units, count, output, sizeof(output), &bytes) ||
        bytes != sizeof(valid) - 1U || memcmp(output, valid, sizeof(valid)))
        return 2;
    static const char overlong[] = "\xC0\xAF";
    static const char surrogate[] = "\xED\xA0\x80";
    static const char too_high[] = "\xF4\x90\x80\x80";
    static const char truncated[] = "\xF0\x9F\x9A";
    if (reist_utf8_to_utf16(overlong, 2U, units, 32U, &count) ||
        reist_utf8_to_utf16(surrogate, 3U, units, 32U, &count) ||
        reist_utf8_to_utf16(too_high, 4U, units, 32U, &count) ||
        reist_utf8_to_utf16(truncated, 3U, units, 32U, &count)) return 3;
    static const uint16_t lone_high[] = {0xD83DU};
    static const uint16_t lone_low[] = {0xDE80U};
    if (reist_utf16_to_utf8(lone_high, 1U, output, sizeof(output), &bytes) ||
        reist_utf16_to_utf8(lone_low, 1U, output, sizeof(output), &bytes))
        return 4;
    if (reist_utf8_to_utf16(valid, sizeof(valid) - 1U, units, 10U, &count) ||
        reist_utf16_to_utf8(units, 11U, output, 14U, &bytes)) return 5;
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            test_c = Path(temporary) / "utf_test.c"
            executable = Path(temporary) / "utf_test.exe"
            test_c.write_text(source, encoding="utf-8")
            subprocess.run([GCC, "-std=c11", "-Wall", "-Wextra", "-Werror",
                            f"-I{ROOT}", str(test_c), "-o", str(executable)],
                           check=True, cwd=ROOT, capture_output=True)
            subprocess.run([str(executable)], check=True, cwd=ROOT,
                           capture_output=True)

    def test_slot_count_and_payload_use_utf16_units(self) -> None:
        self.assertIn("uint16_t lfn_units[FAT32_MAX_LFN_CHARS]", self.directory)
        self.assertIn("lfn_unit_count + FAT32_LFN_CHARS_PER_ENTRY", self.directory)
        self.assertIn("position < length ? name[position]", self.directory)
        self.assertNotIn("(uint8_t)name[position]", self.directory)

    def test_production_reader_validates_complete_utf16_sequence(self) -> None:
        self.assertIn("uint16_t units[FAT32_MAX_LFN_ENTRIES *", self.files)
        self.assertIn("fat32_lfn_finish", self.files)
        self.assertIn("reist_utf16_to_utf8", self.files)
        self.assertIn("unit_count > FAT32_MAX_LFN_CHARS", self.files)
        self.assertNotIn("value >= 0x20U && value <= 0x7EU", self.files)

    def test_names_validate_before_directory_allocation(self) -> None:
        validation = self.fat[self.fat.index("bool fat32_is_valid_name"):
                              self.fat.index("void fat32_format_short_name")]
        self.assertIn("reist_utf8_to_utf16", validation)
        add = self.directory[self.directory.index(
            "bool add_entry_to_directory_checked"):
            self.directory.index("publish_entry:")]
        self.assertLess(add.index("reist_utf8_to_utf16"),
                        add.index("allocate_new_cluster"))

    def test_shadow_parser_uses_same_bounded_codec(self) -> None:
        self.assertIn('#include "../../../include/reist/utf.h"', self.shadow)
        self.assertIn("shadow_lfn_finish", self.shadow)
        self.assertIn("reist_utf16_to_utf8", self.shadow)
        self.assertIn("reist_utf8_to_utf16", self.shadow)
        self.assertNotIn("malloc(", self.shadow)

    def test_guest_contains_real_utf8_vfat_roundtrip(self) -> None:
        self.assertIn("test_vfat_utf8", self.guest)
        self.assertIn("TEST_STAGE VFAT_UTF8_OK", self.guest)


if __name__ == "__main__":
    unittest.main()
