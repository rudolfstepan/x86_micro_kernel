import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ATA = (ROOT / "drivers/block/ata.c").read_text(encoding="utf-8")
FDD = (ROOT / "drivers/block/fdd.c").read_text(encoding="utf-8")


def function_body(source: str, name: str, *, public: bool | None = None) -> str:
    pattern = re.compile(
        rf"(?m)^(?P<static>static\s+)?(?:bool|void)\s+{re.escape(name)}\s*"
        rf"\([^;]*?\)\s*\{{"
    )
    for match in pattern.finditer(source):
        is_public = match.group("static") is None
        if public is not None and public != is_public:
            continue
        depth = 1
        cursor = match.end()
        while cursor < len(source) and depth:
            if source[cursor] == "{":
                depth += 1
            elif source[cursor] == "}":
                depth -= 1
            cursor += 1
        if depth != 0:
            raise AssertionError(f"unbalanced body for {name}")
        return source[match.end(): cursor - 1]
    raise AssertionError(f"function {name} not found")


def assert_in_order(test: unittest.TestCase, body: str, *tokens: str) -> None:
    position = -1
    for token in tokens:
        next_position = body.find(token, position + 1)
        test.assertGreater(
            next_position, position, f"{token!r} missing or out of order"
        )
        position = next_position


class BlockTransactionContractTests(unittest.TestCase):
    def test_transaction_guards_require_task_context_and_enabled_irqs(self):
        for source, prefix in ((ATA, "ata"), (FDD, "fdd")):
            with self.subTest(driver=prefix, edge="begin"):
                begin = function_body(source, f"{prefix}_transaction_begin")
                assert_in_order(
                    self,
                    begin,
                    "KASSERT_NOT_IRQ()",
                    "KASSERT(irq_enabled())",
                    "scheduler_preempt_disable()",
                )
            with self.subTest(driver=prefix, edge="end"):
                end = function_body(source, f"{prefix}_transaction_end")
                assert_in_order(
                    self,
                    end,
                    "KASSERT_NOT_IRQ()",
                    "KASSERT(irq_enabled())",
                    "KASSERT(scheduler_preempt_is_disabled())",
                    "scheduler_preempt_enable()",
                )

    def test_guards_keep_hardware_interrupts_enabled(self):
        forbidden = re.compile(r"\b(?:irq_save|irq_disable)\s*\(|\bcli\b")
        for source, driver in ((ATA, "ATA"), (FDD, "FDD")):
            with self.subTest(driver=driver):
                self.assertIsNone(forbidden.search(source))

    def test_ata_public_transactions_pair_the_guard_on_every_return_path(self):
        wrappers = {
            "ata_read_sector": "ata_read_sector_impl(",
            "ata_write_sector": "ata_write_sector_impl(",
            "ata_detect_drives": "ata_detect_drives_impl(",
            "ata_identify_drive": "ata_identify_drive_impl(",
        }
        for name, operation in wrappers.items():
            with self.subTest(operation=name):
                body = function_body(ATA, name, public=True)
                self.assertEqual(body.count("ata_transaction_begin()"), 1)
                self.assertEqual(body.count("ata_transaction_end()"), 1)
                assert_in_order(
                    self,
                    body,
                    "ata_transaction_begin()",
                    operation,
                    "ata_transaction_end()",
                )

    def test_fdd_public_transactions_pair_the_guard_on_every_return_path(self):
        wrappers = {
            "fdc_initialize": "fdc_initialize_impl(",
            "fdc_motor_on": "fdc_motor_on_impl(",
            "fdc_motor_off": "fdc_motor_off_impl(",
            "fdd_service": "fdd_service_impl(",
            "fdc_init_controller": "fdc_init_controller_impl(",
            "fdc_read_sectors": "fdc_read_sectors_impl(",
            "fdc_write_sectors": "fdc_write_sectors_impl(",
            "fdc_calibrate_drive": "fdc_calibrate_drive_impl(",
            "fdd_detect_drives": "fdd_detect_drives_impl(",
        }
        for name, operation in wrappers.items():
            with self.subTest(operation=name):
                body = function_body(FDD, name, public=True)
                self.assertEqual(body.count("fdd_transaction_begin()"), 1)
                self.assertEqual(body.count("fdd_transaction_end()"), 1)
                assert_in_order(
                    self,
                    body,
                    "fdd_transaction_begin()",
                    operation,
                    "fdd_transaction_end()",
                )

    def test_nested_public_operations_use_the_nestable_guard(self):
        identify = function_body(ATA, "ata_detect_drives_impl", public=False)
        self.assertIn("ata_identify_drive(", identify)
        self.assertNotIn("ata_identify_drive_impl(", identify)

        read_one = function_body(FDD, "fdc_read_sector", public=True)
        write_one = function_body(FDD, "fdd_write_sector", public=True)
        for body, nested_call in (
            (read_one, "fdc_read_sectors("),
            (write_one, "fdc_write_sectors("),
        ):
            self.assertEqual(body.count("fdd_transaction_begin()"), 1)
            self.assertEqual(body.count("fdd_transaction_end()"), 1)
            assert_in_order(
                self,
                body,
                "fdd_transaction_begin()",
                nested_call,
                "fdd_transaction_end()",
            )

        detect = function_body(FDD, "fdd_detect_drives_impl", public=False)
        self.assertIn("fdc_init_controller(", detect)
        self.assertIn("fdc_calibrate_drive(", detect)
        self.assertNotIn("fdc_init_controller_impl(", detect)
        self.assertNotIn("fdc_calibrate_drive_impl(", detect)

    def test_fdd_irq_handler_remains_bounded_and_guard_free(self):
        irq = function_body(FDD, "fdd_irq_handler", public=True)
        self.assertIn("irq_triggered = true", irq)
        self.assertNotRegex(irq, r"transaction|scheduler_preempt|KASSERT")

    def test_transactions_do_not_cross_scheduler_switch_apis(self):
        forbidden = re.compile(
            r"\b(?:scheduler_sleep_ms|scheduler_yield|wait_queue_block(?:_locked)?|"
            r"swtch|task_exit(?:_status)?)\s*\("
        )
        for source, driver in ((ATA, "ATA"), (FDD, "FDD")):
            with self.subTest(driver=driver):
                self.assertIsNone(forbidden.search(source))

        stdlib = (ROOT / "lib/libc/stdlib.c").read_text(encoding="utf-8")
        pit = (ROOT / "kernel/time/pit.c").read_text(encoding="utf-8")
        delay = function_body(stdlib, "delay_ms", public=True)
        busy_wait = function_body(pit, "pit_delay", public=True)
        self.assertIn("pit_delay(ms)", delay)
        self.assertIn('"pause"', busy_wait)
        self.assertIsNone(forbidden.search(delay + busy_wait))

    def test_shell_and_filesystems_call_only_guarded_public_entrypoints(self):
        public_calls = re.compile(
            r"\b(?:ata_(?:read|write)_sector|fdc_(?:read|write)_sectors?|"
            r"fdd_write_sector)\s*\("
        )
        shell = ROOT / "kernel/shell/command.c"
        vfs_probe = ROOT / "fs/vfs/filesystem.c"
        fat12 = ROOT / "fs/fat12/fat12.c"
        self.assertRegex(shell.read_text(encoding="utf-8"), r"ata_read_sector\s*\(")
        self.assertRegex(vfs_probe.read_text(encoding="utf-8"), r"ata_read_sector\s*\(")
        self.assertRegex(fat12.read_text(encoding="utf-8"), r"fdc_read_sectors\s*\(")
        self.assertRegex(fat12.read_text(encoding="utf-8"), r"fdc_write_sectors\s*\(")

        callers = [shell, *sorted((ROOT / "fs").rglob("*.c"))]
        for path in callers:
            source = path.read_text(encoding="utf-8")
            if not public_calls.search(source):
                continue
            with self.subTest(caller=path.relative_to(ROOT)):
                self.assertNotRegex(source, r"\b(?:ata|fdc|fdd)_\w+_impl\s*\(")
                self.assertNotRegex(source, r"\b(?:irq_save|irq_disable)\s*\(")

    def test_boot_enables_irqs_before_storage_detection(self):
        kernel = (ROOT / "kernel/init/kernel.c").read_text(encoding="utf-8")
        assert_in_order(
            self,
            kernel,
            '__asm__ __volatile__("sti")',
            "ata_detect_drives()",
            "fdd_detect_drives()",
        )


if __name__ == "__main__":
    unittest.main()
