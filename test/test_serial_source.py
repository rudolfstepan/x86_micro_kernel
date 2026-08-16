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


class SerialPresenceContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.serial = read("drivers/char/serial.c")
        cls.keyboard = read("drivers/char/kb.c")
        cls.kernel = read("kernel/init/kernel.c")

    def test_com1_probe_uses_two_scratch_patterns_and_restores_value(self):
        probe = function(self.serial, "static bool serial_probe_port(")
        self.assertIn("SERIAL_SCRATCH", probe)
        self.assertIn("SERIAL_PROBE_PATTERN_A", probe)
        self.assertIn("SERIAL_PROBE_PATTERN_B", probe)
        self.assertIn("original", probe)
        self.assertGreaterEqual(probe.count("outb(SERIAL_SCRATCH(port)"), 3)

    def test_absent_com1_gates_output_paths(self):
        initialize = function(self.serial, "void serial_init(")
        write_char = function(self.serial, "void serial_write_char(")
        transmit_empty = function(self.serial, "bool serial_is_transmit_empty(")
        for body in (initialize, write_char, transmit_empty):
            self.assertIn("serial_com1_present", body)
        self.assertIn("serial_default_present", read("drivers/char/serial.h"))

    def test_com1_is_output_only_and_has_no_keyboard_dependency(self):
        header = read("drivers/char/serial.h")
        keyboard_header = read("drivers/char/kb.h")
        self.assertNotIn('#include "kb.h"', self.serial)
        self.assertNotIn("serial_install_rx_irq", self.serial + header)
        self.assertNotIn("serial_read_char", self.serial + header)
        self.assertNotIn("serial_received", self.serial + header)
        self.assertNotIn("kb_notify_input_ready", self.serial + keyboard_header)

    def test_keyboard_api_consumes_only_ps2_queue(self):
        blocking = function(self.keyboard, "char getchar(")
        nonblocking = function(self.keyboard, "char getchar_nonblocking(")
        line = function(self.keyboard, "void get_input_line(")
        self.assertNotIn("serial_", self.keyboard)
        for body in (blocking, nonblocking, line):
            self.assertIn("input_queue_pop()", body)

    def test_transmit_wait_is_bounded(self):
        write_char = function(self.serial, "void serial_write_char(")
        self.assertIn("SERIAL_TX_POLL_LIMIT", write_char)
        self.assertIn("for (", write_char)

    def test_boot_diagnostic_distinguishes_absent_output_uart(self):
        self.assertIn("serial_default_present()", self.kernel)
        self.assertIn("COM1 unavailable; serial diagnostics disabled", self.kernel)
        self.assertNotIn("serial_install_rx_irq", self.kernel)


if __name__ == "__main__":
    unittest.main()
