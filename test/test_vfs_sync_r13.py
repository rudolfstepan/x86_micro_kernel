"""Focused source-contract regressions for the R1.3 VFS guard."""

from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VFS_C = (ROOT / "fs/vfs/vfs.c").read_text(encoding="utf-8")
VFS_H = (ROOT / "fs/vfs/vfs.h").read_text(encoding="utf-8")


def extract_block(source: str, opening_brace: int) -> str:
    depth = 0
    for index in range(opening_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening_brace:index + 1]
    raise AssertionError("unterminated C block")


def function_block(signature: str) -> str:
    start = VFS_C.index(signature)
    return extract_block(VFS_C, VFS_C.index("{", start))


class VfsSynchronizationContractTests(unittest.TestCase):
    WRAPPERS = {
        "void vfs_init(": "vfs_init_locked(",
        "int vfs_register_filesystem(": "vfs_register_filesystem_locked(",
        "int vfs_mount(": "vfs_mount_locked(",
        "int vfs_unmount(": "vfs_unmount_locked(",
        "int vfs_open(": "vfs_open_locked(",
        "int vfs_close(": "vfs_close_locked(",
        "int vfs_read(": "vfs_read_locked(",
        "int vfs_write(": "vfs_write_locked(",
        "int vfs_readdir(": "vfs_readdir_locked(",
        "int vfs_readdir_batch(": "vfs_readdir_batch_locked(",
        "int vfs_mkdir(": "vfs_mkdir_locked(",
        "int vfs_rmdir(": "vfs_rmdir_locked(",
        "int vfs_create(": "vfs_create_locked(",
        "int vfs_delete(": "vfs_delete_locked(",
        "int vfs_rename(": "vfs_rename_locked(",
        "int vfs_stat(": "vfs_stat_locked(",
        "int vfs_space(": "vfs_space_locked(",
        "vfs_filesystem_t* vfs_get_filesystem(":
            "vfs_get_filesystem_locked(",
        "const char* vfs_get_relative_path(":
            "vfs_get_relative_path_locked(",
    }

    def test_guard_rejects_irq_and_if0_before_disabling_preemption(self) -> None:
        begin = function_block("static void vfs_operation_begin(")
        not_irq = begin.index("KASSERT_NOT_IRQ();")
        interrupts_enabled = begin.index("KASSERT(irq_enabled());")
        disable = begin.index("scheduler_preempt_disable();")
        self.assertLess(not_irq, interrupts_enabled)
        self.assertLess(interrupts_enabled, disable)

        end = function_block("static void vfs_operation_end(")
        self.assertIn("KASSERT_NOT_IRQ();", end)
        self.assertIn("KASSERT(irq_enabled());", end)
        self.assertIn("KASSERT(scheduler_preempt_is_disabled());", end)
        self.assertLess(
            end.index("KASSERT(scheduler_preempt_is_disabled());"),
            end.index("scheduler_preempt_enable();"),
        )

    def test_every_public_entry_uses_one_balanced_guard(self) -> None:
        for signature, locked_call in self.WRAPPERS.items():
            with self.subTest(function=signature):
                block = function_block(signature)
                self.assertEqual(block.count("vfs_operation_begin();"), 1)
                self.assertEqual(block.count("vfs_operation_end();"), 1)
                self.assertEqual(block.count(locked_call), 1)
                self.assertLess(
                    block.index("vfs_operation_begin();"),
                    block.index(locked_call),
                )
                self.assertLess(
                    block.index(locked_call),
                    block.index("vfs_operation_end();"),
                )

    def test_internal_path_resolution_does_not_reenter_public_guard(self) -> None:
        for locked_name in (
            "vfs_open_locked(",
            "vfs_readdir_locked(",
            "vfs_readdir_batch_locked(",
            "vfs_mkdir_locked(",
            "vfs_rmdir_locked(",
            "vfs_create_locked(",
            "vfs_delete_locked(",
            "vfs_rename_locked(",
            "vfs_stat_locked(",
            "vfs_space_locked(",
        ):
            with self.subTest(function=locked_name):
                block = function_block(f"static int {locked_name}")
                self.assertNotRegex(block, r"\bvfs_get_filesystem\s*\(")
                self.assertNotRegex(block, r"\bvfs_get_relative_path\s*\(")

    def test_guarded_vfs_has_no_block_or_context_switch_call(self) -> None:
        forbidden = re.compile(
            r"\b(?:scheduler_sleep_ms|scheduler_yield|"
            r"wait_queue_block_locked|swtch)\s*\("
        )
        self.assertNotRegex(VFS_C, forbidden)

    def test_header_publishes_the_nonblocking_execution_contract(self) -> None:
        contract = re.search(
            r"Execution contract \(uniprocessor\):(?P<body>.*?)\*/",
            VFS_H,
            flags=re.DOTALL,
        )
        self.assertIsNotNone(contract)
        body = contract.group("body").lower()
        for term in (
            "interrupts enabled",
            "preemption guard",
            "sleep",
            "yield",
            "block",
            "switch context",
        ):
            with self.subTest(term=term):
                self.assertIn(term, body)


if __name__ == "__main__":
    unittest.main()
