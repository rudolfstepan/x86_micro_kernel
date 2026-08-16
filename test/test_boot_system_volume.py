"""Regression checks for deterministic REIST root-volume selection."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def function(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise AssertionError(f"unterminated function: {signature}")


class BootSystemVolumeTests(unittest.TestCase):
    def setUp(self) -> None:
        self.filesystem = read("fs/vfs/filesystem.c")
        self.fat32 = read("fs/fat32/fat32_vfs_adapter.c")
        self.kernel = read("kernel/init/kernel.c")
        self.process = read("kernel/proc/process.c")

    def test_system_volume_requires_exact_label_and_valid_fat32_bpb(self) -> None:
        select = function(self.filesystem, "static bool reist_system_volume(")
        self.assertIn('"X86 SYSTEM "', self.filesystem)
        for required in (
            "DRIVE_TYPE_PARTITION",
            "fat32_partition_type",
            "bytes_per_sector == SECTOR_SIZE",
            "sectors_per_fat_32 != 0U",
            "root_cluster >= 2U",
            "total_sectors_32 <= drive->sectors",
            'memcmp(boot->file_system_type, "FAT32   ", 8U)',
            "memcmp(boot->volume_label, REIST_SYSTEM_VOLUME_LABEL, 11U)",
        ):
            self.assertIn(required, select)

    def test_unique_system_volume_is_mounted_before_auxiliary_media(self) -> None:
        mount = function(self.filesystem, "void auto_mount_all_drives(")
        self.assertIn("find_reist_system_volume", mount)
        self.assertIn("find_boot_floppy", mount)
        self.assertIn("if (order == 0) i = preferred_root", mount)
        self.assertIn("order <= preferred_root", mount)
        self.assertIn("Preferred boot volume failed; refusing fallback root", mount)
        self.assertIn("Preferred boot volume read failed; refusing fallback root", mount)
        self.assertIn("Multiple X86 SYSTEM volumes", mount)
        self.assertRegex(mount, r"current_drive\s*=\s*default_drive\s*;")

    def test_bios_floppy_boot_precedes_hdd_system_label(self) -> None:
        mount = function(self.filesystem, "void auto_mount_all_drives(")
        floppy = mount.index("find_boot_floppy")
        hdd = mount.index("find_reist_system_volume")
        self.assertLess(floppy, hdd)
        self.assertIn("preferred_is_floppy", mount)
        driver = function(self.kernel, "static void driver_init(")
        self.assertIn("MULTIBOOT1_FLAG_BOOT_DEVICE", driver)
        self.assertIn("bios_drive < 0x80U", driver)
        self.assertIn("auto_mount_all_drives(boot_floppy_drive)", driver)

    def test_fat32_lookup_does_not_collapse_io_failure_to_not_found(self) -> None:
        resolve = function(self.fat32, "static int fat32_resolve_entry(")
        self.assertIn("fat32_lookup_entry_in_directory", resolve)
        self.assertIn("FAT32_LOOKUP_NOT_FOUND", resolve)
        self.assertIn("return VFS_ERR_IO", resolve)
        opened = function(self.fat32, "static int fat32_vfs_open_unlocked(")
        self.assertIn("return resolve_result", opened)

    def test_loader_reports_root_resource_and_caller_preserves_result(self) -> None:
        details = function(self.process, "static void program_load_root_details(")
        self.assertIn('strcmp(drive->mount_point, "/")', details)
        self.assertIn("drive->lba_offset", details)
        loader = function(self.process, "static int load_program_file(")
        self.assertIn("program_load_root_details(&identity, &location)", loader)
        self.assertIn("panic_context_set_result(result, identity, location)", loader)

        main = function(self.kernel, "void kernel_main(")
        probe_start = main.index("if (!supervisor_start_probe(")
        probe_end = main.index(
            'boot_context("userspace-start", "storage service"', probe_start,
        )
        self.assertNotIn(
            "panic_context_set_result", main[probe_start:probe_end],
        )

    def test_desktop_and_shell_are_loaded_only_from_selected_root(self) -> None:
        start = function(self.kernel, "static int start_userspace_program(")
        self.assertIn('strcmp(drive->mount_point, "/")', start)
        self.assertNotIn("preferred_type", start)
        self.assertNotIn("for (int pass", start)


if __name__ == "__main__":
    unittest.main()
