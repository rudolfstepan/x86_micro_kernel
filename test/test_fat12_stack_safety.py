import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def function_body(source: str, name: str) -> str:
    match = re.search(rf"\b{name}\s*\([^;]*?\)\s*\{{", source, re.S)
    if match is None:
        raise AssertionError(f"function {name} not found")
    depth = 1
    cursor = match.end()
    while cursor < len(source) and depth != 0:
        if source[cursor] == "{":
            depth += 1
        elif source[cursor] == "}":
            depth -= 1
        cursor += 1
    if depth != 0:
        raise AssertionError(f"function {name} has no closing brace")
    return source[match.start():cursor]


class Fat12StackSafetyTests(unittest.TestCase):
    def test_journal_uses_exact_instance_owned_four_sector_scratch(self) -> None:
        header = read("fs/fat12/fat12_journal.h")
        source = read("fs/fat12/fat12_journal.c")
        self.assertIn("#define FAT12_JOURNAL_SCRATCH_SECTORS 4U", header)
        self.assertIn("fat12_journal_scratch_t scratch;", header)
        self.assertIn("uint8_t sectors[FAT12_JOURNAL_SCRATCH_SECTORS]", header)
        self.assertIn("fat12_journal_scratch_begin", source)
        self.assertIn("fat12_journal_scratch_end", source)
        self.assertNotRegex(
            source,
            r"uint8_t\s+\w+\s*\[\s*FAT12_JOURNAL_SECTOR_SIZE\s*\]",
        )

    def test_core_write_workspace_is_deadline_serialized(self) -> None:
        header = read("fs/fat12/fat12.h")
        source = read("fs/fat12/fat12.c")
        self.assertIn("FAT12_OPERATION_LOCK_TIMEOUT_MS 10000U", source)
        self.assertIn("fat12_operation_mutex", source)
        self.assertIn("bool fat12_operation_workspace_begin(void);", header)
        self.assertIn("void fat12_operation_workspace_end(void);", header)
        body = function_body(source, "fat12_write_logical_sectors")
        self.assertIn("fat12_core_workspace_claim", body)
        self.assertIn("fat12_core_workspace_release", body)
        self.assertNotRegex(
            body, r"uint8_t\s+\w+\s*\[\s*FAT12_SECTOR_SIZE\s*\]"
        )

    def test_dominant_vfs_metadata_buffers_are_fixed_workspaces(self) -> None:
        source = read("fs/fat12/fat12_vfs_adapter.c")
        self.assertIn("fat12_vfs_workspace_t", source)
        self.assertIn("static fat12_vfs_workspace_t fat12_vfs_workspace", source)
        self.assertIn("fat12_vfs_workspace_claim", source)
        self.assertIn("fat12_vfs_workspace_release", source)
        for name in (
            "fat12_allocate_cluster",
            "fat12_scan_directory",
            "fat12_resolve_parent",
            "fat12_write_entry",
            "fat12_find_free_slot",
        ):
            body = function_body(source, name)
            self.assertIn("fat12_vfs_workspace_claim", body, name)
            self.assertIn("fat12_vfs_workspace_release", body, name)
            self.assertNotRegex(
                body,
                r"(?:uint8_t|char)\s+\w+\s*\[\s*(?:FAT12_SECTOR_SIZE|FAT12_VFS_PATH_MAX)\s*\]",
                name,
            )

    def test_media_failure_defers_formatting_until_supervisor_poll(self) -> None:
        source = read("kernel/init/storage_service.c")
        report = function_body(source, "storage_service_report_media_failure")
        emit = function_body(source, "storage_service_emit_pending_quarantine")
        poll = function_body(source, "storage_service_poll")
        self.assertNotIn("printf(", report)
        self.assertIn("pending_quarantine_reports", report)
        self.assertIn("__atomic_fetch_or", report)
        self.assertLess(report.index("control_write(&control)"),
                        report.index("__atomic_fetch_or"))
        self.assertIn('printf("REIST_STORAGE RESOURCE_QUARANTINED %u\\n"',
                      emit)
        self.assertIn("__atomic_compare_exchange_n", emit)
        self.assertLess(
            poll.index("storage_service_emit_pending_quarantine()"),
            poll.index("poll_media_reintegration(now_ms)"),
        )


if __name__ == "__main__":
    unittest.main()
