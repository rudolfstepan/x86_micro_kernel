import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


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


class PhysicalPs2KeyboardContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = read("drivers/char/kb.c")

    def test_i8042_takeover_is_bounded_and_programs_first_port(self):
        install = function(self.source, "void kb_install(")
        self.assertIn("I8042_POLL_LIMIT", self.source)
        self.assertIn("i8042_wait_input_clear", install)
        self.assertIn("I8042_CMD_DISABLE_PORT1", install)
        self.assertIn("I8042_CMD_TEST_CONTROLLER", install)
        self.assertIn("I8042_CMD_TEST_PORT1", install)
        self.assertIn("I8042_CMD_ENABLE_PORT1", install)
        self.assertNotRegex(install, r"while\s*\(\s*inb")

    def test_irq_and_raw_scan_set_are_explicit_and_acknowledged(self):
        install = function(self.source, "void kb_install(")
        self.assertIn("I8042_CONFIG_IRQ1", install)
        self.assertIn("I8042_CONFIG_TRANSLATION", install)
        self.assertRegex(
            install,
            r"config\s*&=\s*\(uint8_t\)~I8042_CONFIG_TRANSLATION",
        )
        self.assertIn("I8042_CONFIG_PORT1_CLOCK_DISABLED", install)
        self.assertIn("I8042_CMD_WRITE_CONFIG", install)
        self.assertIn("I8042_KEYBOARD_ACK", self.source)
        self.assertIn("I8042_KEYBOARD_RESEND", self.source)
        self.assertIn("i8042_keyboard_command", install)
        self.assertIn("I8042_KEYBOARD_ENABLE_SCANNING", install)
        self.assertIn("PIC1_DATA_PORT", install)

    def test_raw_set2_make_break_and_extended_sequences_are_decoded(self):
        decoder = function(self.source, "static void kb_process_scancode(")
        mapping = function(self.source, "static uint8_t set2_to_set1(")
        self.assertIn("SET2_RELEASE_PREFIX", decoder)
        self.assertIn("SC_EXTENDED_PREFIX", decoder)
        self.assertIn("set2_release_pending", decoder)
        self.assertIn("set2_to_set1", decoder)
        for raw_code in ("0x1CU", "0x5AU", "0x66U", "0x75U"):
            self.assertIn(raw_code, mapping)
        self.assertNotIn("SC_RELEASE_MASK", decoder)

    def test_lock_led_updates_are_deferred_outside_irq_context(self):
        decoder = function(self.source, "static void kb_process_scancode(")
        handler = function(self.source, "void kb_handler(")
        service = function(self.source, "static void kb_service_leds_locked(")
        blocking = function(self.source, "char getchar(")
        self.assertIn("keyboard_led_update_pending", decoder)
        self.assertIn("I8042_KEYBOARD_SET_LEDS", service)
        self.assertIn("i8042_keyboard_command", service)
        self.assertIn("KASSERT_NOT_IRQ", service)
        self.assertNotIn("i8042_keyboard_command", handler)
        self.assertIn("kb_service_leds_locked", blocking)

    def test_numlock_controls_keypad_digits_and_navigation(self):
        keypad = function(self.source, "static bool handle_keypad_key(")
        decoder = function(self.source, "static void kb_process_scancode(")
        self.assertIn("kbd_state.num_lock", keypad)
        for digit in ("'0'", "'1'", "'2'", "'3'", "'7'", "'8'", "'9'"):
            self.assertIn(digit, keypad)
        for navigation in (
            "KEY_UP", "KEY_DOWN", "KEY_LEFT", "KEY_RIGHT",
            "KEY_HOME", "KEY_END", "KEY_INSERT", "KEY_DELETE",
        ):
            self.assertIn(navigation, keypad)
        self.assertIn("handle_keypad_key", decoder)

    def test_temporary_physical_trace_is_removed(self):
        source = self.source
        drain = function(source, "static void kb_drain_output_locked(")
        handler = function(source, "void kb_handler(")
        self.assertNotIn("KEYBOARD_TRACE_CAPACITY", source)
        self.assertNotIn("keyboard_trace", source)
        self.assertNotIn("kb_trace_", source)
        self.assertNotIn("PS2 TRACE", source)
        self.assertNotIn("printf", drain)
        self.assertNotIn("printf", handler)

    def test_runtime_input_has_bounded_poll_fallback(self):
        handler = function(self.source, "void kb_handler(")
        blocking = function(self.source, "char getchar(")
        nonblocking = function(self.source, "char getchar_nonblocking(")
        self.assertIn("kb_drain_output_locked", handler)
        self.assertIn("KEYBOARD_DRAIN_BUDGET", self.source)
        self.assertIn("I8042_STATUS_AUX_DATA", self.source)
        self.assertIn("I8042_STATUS_ERROR_MASK", self.source)
        self.assertIn("kb_drain_output_locked", blocking)
        self.assertIn("wait_queue_block_until_locked", blocking)
        self.assertIn("KEYBOARD_POLL_INTERVAL_MS", blocking)
        self.assertNotIn("wait_queue_block_locked", blocking)
        self.assertIn("kb_poll_controller", nonblocking)

    def test_install_message_distinguishes_verified_irq_and_polling(self):
        install = function(self.source, "void kb_install(")
        self.assertIn("PS/2 keyboard ready", install)
        self.assertIn("scanset=2-raw", install)
        self.assertIn("IRQ1+poll", install)
        self.assertIn("poll-only", install)


if __name__ == "__main__":
    unittest.main()
