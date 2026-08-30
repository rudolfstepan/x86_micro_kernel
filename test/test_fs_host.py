"""Build and run the filesystem host regression harnesses."""

from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
GCC = shutil.which("gcc")


class FilesystemSourceTests(unittest.TestCase):
    def test_fat12_directory_reads_do_not_print_diagnostics(self) -> None:
        source = (ROOT / "fs" / "fat12" / "fat12.c").read_text(encoding="utf-8")
        self.assertNotIn("Reading subdirectory. Start cluster:", source)

    def test_vfs_mount_initializes_maintenance_state(self) -> None:
        source = (ROOT / "fs" / "vfs" / "vfs.c").read_text(encoding="utf-8")
        mount = source[source.index("static int vfs_mount_locked"):
                       source.index("static int vfs_unmount_locked")]
        self.assertIn("fs->open_nodes = 0", mount)
        self.assertIn("fs->maintenance_blocked = maintenance_blocked", mount)

    def test_fat32_directory_scan_uses_fixed_serialized_workspace(self) -> None:
        source = (ROOT / "fs" / "fat32" / "fat32_files.c").read_text(
            encoding="utf-8"
        )
        scan = source[source.index("static fat32_lookup_result_t "
                                   "fat32_scan_directory("):
                      source.index("fat32_lookup_result_t "
                                   "fat32_lookup_entry_named(")]
        self.assertIn("fat32_directory_scan_workspace_t", source)
        self.assertIn("static fat32_directory_scan_workspace_t", source)
        self.assertIn("fat32_operation_workspace_begin()", scan)
        self.assertIn("workspace->in_use", scan)
        self.assertIn("fat32_operation_workspace_end", scan)
        self.assertIn("release_workspace:", scan)
        self.assertNotIn("struct fat32_dir_entry sector_entries[", scan)
        self.assertNotIn("fat32_lfn_reader_t lfn;", scan)
        self.assertNotIn("char short_name[13]", scan)
        self.assertNotIn("char visible_name[MAX_PATH_LENGTH]", scan)
        self.assertNotIn("reist_unicode_caseless_nfc_equal", scan)

    def test_fat32_metadata_mutations_use_fixed_serialized_workspaces(self) -> None:
        directory = (ROOT / "fs" / "fat32" / "fat32_dir.c").read_text(
            encoding="utf-8"
        )
        addition = directory[
            directory.index("bool add_entry_to_directory_checked("):
            directory.index("bool add_entry_to_directory(", directory.index(
                "bool add_entry_to_directory_checked("))
        ]
        self.assertIn("fat32_directory_insert_workspace_t", directory)
        self.assertIn("static fat32_directory_insert_workspace_t", directory)
        self.assertIn("fat32_operation_workspace_begin()", addition)
        self.assertIn("workspace->in_use", addition)
        self.assertIn("release_workspace:", addition)
        self.assertNotIn("uint16_t lfn_units[", addition)
        self.assertNotIn("fat32_slot_location_t locations[", addition)
        self.assertNotIn("struct fat32_dir_entry raw[", addition)

        cluster = (ROOT / "fs" / "fat32" / "fat32_cluster.c").read_text(
            encoding="utf-8"
        )
        update = cluster[
            cluster.index("bool mark_cluster_in_fat("):
            cluster.index("unsigned int get_first_data_sector(")
        ]
        copy = cluster[
            cluster.index("static bool write_fat_copy_entry("):
            cluster.index("bool mark_cluster_in_fat(")
        ]
        self.assertIn("fat32_fat_update_workspace_t", cluster)
        self.assertIn("fat32_operation_workspace_begin()", update)
        self.assertIn("release_workspace:", update)
        self.assertNotIn("active_buffer[SECTOR_SIZE]", update)
        self.assertNotIn("unsigned char buffer[SECTOR_SIZE];", copy)
        self.assertNotIn("unsigned char verify[SECTOR_SIZE];", copy)

        adapter = (ROOT / "fs" / "fat32" /
                   "fat32_vfs_adapter.c").read_text(encoding="utf-8")
        tombstone = adapter[
            adapter.index("static bool fat32_tombstone_rename_source("):
            adapter.index("static int fat32_vfs_rename_unlocked(")
        ]
        rename = adapter[
            adapter.index("static int fat32_vfs_rename_unlocked("):
            adapter.index("static int fat32_vfs_stat_unlocked(")
        ]
        wrapper = adapter[
            adapter.index("static int fat32_vfs_rename("):
            adapter.index("static int fat32_vfs_touch(")
        ]
        self.assertIn("fat32_rename_workspace_t", adapter)
        self.assertIn("static fat32_rename_workspace_t", adapter)
        self.assertIn("workspace->slots", tombstone)
        self.assertIn("workspace->sector_entries", tombstone)
        self.assertIn("workspace->old_leaf", rename)
        self.assertIn("workspace->new_leaf", rename)
        self.assertIn("workspace->in_use", wrapper)
        self.assertNotIn("fat32_rename_slot_t slots[", tombstone)
        self.assertNotIn("char old_leaf[MAX_PATH_LENGTH]", rename)
        self.assertNotIn("char new_leaf[MAX_PATH_LENGTH]", rename)


@unittest.skipUnless(GCC, "gcc is required for the C host regressions")
class FilesystemHostTests(unittest.TestCase):
    def build_and_run(self, name: str, sources: list[str]) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / (
                name + (".exe" if os.name == "nt" else "")
            )
            command = [
                GCC,
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fno-builtin",
                "-DKERNEL_HOST_TEST",
                f"-I{ROOT}",
                *(str(ROOT / source) for source in sources),
                "-o",
                str(executable),
            ]
            subprocess.run(command, check=True, cwd=ROOT, capture_output=True)
            subprocess.run([str(executable)], check=True, cwd=ROOT,
                           capture_output=True, timeout=10)

    def test_vfs_mount_lifecycle_and_path_boundaries(self) -> None:
        self.build_and_run(
            "test_vfs_host",
            ["test/test_vfs_host.c", "fs/vfs/vfs.c"],
        )

    def test_shell_path_normalization_and_drive_syntax(self) -> None:
        self.build_and_run(
            "test_shell_path_host",
            [
                "test/test_shell_path_host.c",
                "kernel/shell/path_resolver.c",
            ],
        )

    def test_custom_program_image_validation(self) -> None:
        self.build_and_run(
            "test_program_image_host",
            [
                "test/test_program_image_host.c",
                "kernel/proc/program_image.c",
            ],
        )

    def test_fat32_write_truncate_and_directory_extension(self) -> None:
        self.build_and_run(
            "test_fat32_host",
            [
                "test/test_fat32_host.c",
                "fs/fat32/fat32.c",
                "fs/fat32/fat32_cluster.c",
                "fs/fat32/fat32_dir.c",
                "fs/fat32/fat32_files.c",
                "fs/fat32/fat32_vfs_adapter.c",
                "fs/vfs/vfs.c",
                "drivers/block/ata_journal.c",
            ],
        )

    def test_ext2_partition_directory_and_indirect_blocks(self) -> None:
        self.build_and_run(
            "test_ext2_host",
            ["test/test_ext2_host.c", "fs/ext2/ext2.c"],
        )

    def test_crc_validated_gpt_partition_discovery(self) -> None:
        self.build_and_run(
            "test_partition_host",
            ["test/test_partition_host.c", "drivers/block/partition.c"],
        )

    def test_fat12_cluster_chain_seek_and_subdirectory_lifetime(self) -> None:
        self.build_and_run(
            "test_fat12_host",
            [
                "test/test_fat12_host.c",
                "fs/fat12/fat12.c",
                "fs/fat12/fat12_journal.c",
                "fs/fat12/fat12_remap.c",
                "fs/fat12/fat12_replica.c",
                "fs/fat12/fat12_critical.c",
                "fs/fat12/fat12_vfs_adapter.c",
                "lib/libc/string.c",
            ],
        )

    def test_fat12_journal_recovery_and_readback_failure(self) -> None:
        self.build_and_run(
            "test_fat12_journal_host",
            ["test/test_fat12_journal_host.c", "fs/fat12/fat12_journal.c",
             "lib/libc/string.c"],
        )

    def test_fat12_remap_persistence_and_duplicate_rejection(self) -> None:
        self.build_and_run(
            "test_fat12_remap_host",
            ["test/test_fat12_remap_host.c", "fs/fat12/fat12_remap.c",
             "lib/libc/string.c"],
        )

    def test_fat12_replica_selection_and_conflict_rejection(self) -> None:
        self.build_and_run(
            "test_fat12_replica_host",
            ["test/test_fat12_replica_host.c", "fs/fat12/fat12_replica.c",
             "lib/libc/string.c"],
        )


if __name__ == "__main__":
    unittest.main()
