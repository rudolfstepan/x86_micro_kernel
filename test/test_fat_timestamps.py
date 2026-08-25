"""Source-contract tests for bounded FAT timestamp publication."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class FatTimestampTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.vfs_header = (ROOT / "fs/vfs/vfs.h").read_text(encoding="utf-8")
        cls.sdk_header = (ROOT / "userspace/sdk/include/x86os.h").read_text(
            encoding="utf-8")
        cls.time = (ROOT / "fs/vfs/vfs_time.h").read_text(encoding="utf-8")
        cls.fat12 = (ROOT / "fs/fat12/fat12_vfs_adapter.c").read_text(
            encoding="utf-8")
        cls.fat32 = (ROOT / "fs/fat32/fat32_vfs_adapter.c").read_text(
            encoding="utf-8")
        cls.fat32_dir = (ROOT / "fs/fat32/fat32_dir.c").read_text(
            encoding="utf-8")
        cls.guest = (ROOT / "userspace/programs/guest_test.c").read_text(
            encoding="utf-8")

    @staticmethod
    def function(source: str, start: str, end: str) -> str:
        begin = source.index(start)
        return source[begin:source.index(end, begin)]

    def test_public_metadata_abi_remains_unextended(self) -> None:
        for source in (self.vfs_header, self.sdk_header):
            fields = [source.index("uint32_t create_time"),
                      source.index("uint32_t modify_time"),
                      source.index("uint32_t access_time")]
            self.assertEqual(fields, sorted(fields))

    def test_fat_conversion_is_validated_and_bounded(self) -> None:
        self.assertIn("year < 1970 || year > 2107", self.time)
        self.assertIn("month < 1 || month > 12", self.time)
        self.assertIn("day < 1 || (uint32_t)day > limit", self.time)
        self.assertIn("second / 2", self.time)
        self.assertIn("vfs_time_from_fat(source->last_access_date, 0)",
                      self.fat12)
        self.assertIn("vfs_time_from_fat(fat_entry->last_access_date, 0)",
                      self.fat32)

    def test_fat12_creation_and_mutation_publish_expected_fields(self) -> None:
        creation = self.function(
            self.fat12, "static void fat12_set_creation_time",
            "static void fat12_set_modified_time")
        for field in ("create_time", "create_date", "last_write_time",
                      "last_write_date", "last_access_date"):
            self.assertIn(field, creation)
        write = self.function(self.fat12, "static int fat12_vfs_write(",
                              "static int fat12_vfs_truncate(")
        truncate = self.function(self.fat12, "static int fat12_vfs_truncate(",
                                 "static int fat12_vfs_readdir(")
        self.assertIn("fat12_set_modified_time(&entry)", write)
        self.assertIn("fat12_set_modified_time(&entry)", truncate)
        self.assertLess(write.index("fat12_set_modified_time(&entry)"),
                        write.index("fat12_write_entry(&location, &entry)"))

    def test_fat12_all_new_entries_are_initialized_before_publication(self) -> None:
        mkdir = self.function(self.fat12, "static int fat12_vfs_mkdir(",
                              "static int fat12_vfs_rmdir(")
        create = self.function(self.fat12, "static int fat12_vfs_create(",
                               "static int fat12_vfs_delete(")
        self.assertGreaterEqual(mkdir.count("fat12_set_creation_time"), 3)
        self.assertLess(mkdir.index("fat12_set_creation_time(&entry)"),
                        mkdir.index("fat12_write_entry(&slot, &entry)"))
        self.assertLess(create.index("fat12_set_creation_time(&entry)"),
                        create.index("fat12_write_entry(&slot, &entry)"))

    def test_touch_preserves_creation_and_read_paths_do_not_write(self) -> None:
        touch12 = self.function(self.fat12, "static int fat12_vfs_touch(",
                               "static int fat12_vfs_stat(")
        self.assertIn("fat12_set_touch_time", touch12)
        touch_helper = self.function(
            self.fat12, "static void fat12_set_touch_time",
            "static int fat12_resolve_parent")
        self.assertNotIn("create_", touch_helper)
        touch32 = self.function(self.fat32, "static int fat32_vfs_touch(",
                               "static int fat32_vfs_stat(")
        self.assertIn("set_fat32_time(&entry.write_time", touch32)
        self.assertIn("set_fat32_time(NULL, &entry.last_access_date)", touch32)
        for source, starts in (
            (self.fat12, ("static int fat12_vfs_read(",
                          "static int fat12_vfs_stat(",
                          "static int fat12_vfs_fstat(")),
            (self.fat32, ("static int fat32_vfs_read(",
                          "static int fat32_vfs_stat(",
                          "static int fat32_vfs_fstat(")),
        ):
            for start in starts:
                body = source[source.index(start):source.index("\n}",
                    source.index(start))]
                self.assertNotIn("set_current", body)
                self.assertNotIn("set_fat32_time", body)
                self.assertNotIn("write_entry", body)

    def test_fat32_creation_and_guest_runtime_proof_remain_present(self) -> None:
        creation = self.function(self.fat32_dir,
            "void create_directory_entry(",
            "static bool fat32_short_case_representable")
        self.assertIn("entry->crt_time", creation)
        self.assertIn("entry->crt_date", creation)
        self.assertIn("entry->last_access_date", creation)
        self.assertIn("entry->write_time", creation)
        commit = self.function(self.fat32, "static bool fat32_commit_node_data",
                               "static int fat32_refresh_file_node")
        self.assertIn("set_fat32_time(&committed.write_time", commit)
        self.assertIn("test_fat_timestamps", self.guest)
        self.assertIn("TEST_STAGE FAT_TIMESTAMPS_OK", self.guest)


if __name__ == "__main__":
    unittest.main()
