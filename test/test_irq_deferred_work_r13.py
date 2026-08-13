"""Source regressions for hard-IRQ acknowledgement/deferred-work boundaries."""

from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def function_block(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening:index + 1]
    raise AssertionError(f"unterminated function {signature}")


def code_only(source: str) -> str:
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.DOTALL)
    return re.sub(r"//.*", "", source)


class HardIrqDeferredWorkTests(unittest.TestCase):
    def assert_irq_is_bounded(self, block: str) -> None:
        block = code_only(block)
        for forbidden in (
            "printf(",
            "malloc(",
            "calloc(",
            "realloc(",
            "k_malloc(",
            "kmalloc(",
            "free(",
            "kfree(",
            "memcpy(",
            "netdev_deliver_rx(",
            "pit_delay(",
            "sleep(",
            "yield(",
            "scheduler_",
        ):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, re.sub(r"\s+", "", block))
        self.assertNotRegex(block, r"\b(?:for|while)\s*\(")

    def test_hpet_irq_only_acknowledges_and_records_the_event(self) -> None:
        source = read("kernel/time/hpet.c")
        self.assertRegex(source, r"volatile\s+uint32_t\s+hpet_interrupt_count")
        handler = function_block(source, "void hpet_timer_isr(")
        self.assert_irq_is_bounded(handler)
        self.assertIn("HPET_INTERRUPT_STATUS", handler)
        self.assertRegex(handler, r"hpet_interrupt_count\s*\+\+")
        self.assertNotIn("HPET_TIMER_COMPARATOR", handler)

    def test_e1000_irq_defers_descriptor_work(self) -> None:
        source = read("drivers/net/e1000.c")
        handler = function_block(source, "static void e1000_isr(")
        self.assert_irq_is_bounded(handler)
        self.assertIn("E1000_REG_ICR", handler)
        self.assertIn("e1000_pending_events", handler)
        self.assertNotIn("e1000_drain_rx", handler)
        self.assertIn(
            "e1000_drain_rx();",
            function_block(source, "void e1000_poll_rx("),
        )

    def test_rtl8139_irq_defers_receive_ring_drain(self) -> None:
        source = read("drivers/net/rtl8139.c")
        handler = function_block(source, "void rtl8139_interrupt_handler(")
        self.assert_irq_is_bounded(handler)
        self.assertIn("RTL_ISR", handler)
        self.assertIn("rtl8139_rx_pending", handler)
        self.assertNotIn("rtl8139_drain_rx", handler)
        self.assertIn(
            "rtl8139_drain_rx();",
            function_block(source, "void rtl8139_poll_rx("),
        )

    def test_ne2000_irq_defers_dma_and_overrun_recovery(self) -> None:
        source = read("drivers/net/ne2000.c")
        handler = function_block(source, "void ne2000_irq_handler(")
        self.assert_irq_is_bounded(handler)
        self.assertIn("NE2000_ISR", handler)
        self.assertIn("ne2000_rx_pending", handler)
        self.assertIn("ne2000_rx_events", handler)
        self.assertNotIn("ne2000_receive_hardware_packet", handler)
        self.assertNotIn("ne2000_recover_rx_overrun", handler)

        poll = function_block(source, "void ne2000_poll_rx(")
        self.assertIn("ne2000_receive_hardware_packet", poll)
        self.assertIn("ne2000_recover_rx_overrun", poll)

    def test_netdev_poll_services_every_interrupt_driven_backend(self) -> None:
        source = read("drivers/net/netdev.c")
        poll = function_block(source, "void netdev_poll(")
        self.assertIn("KASSERT_NOT_IRQ();", poll)
        self.assertIn("KASSERT(irq_enabled());", poll)
        self.assertIn("netdev_poll_busy", source)
        self.assertIn("__sync_lock_test_and_set(&netdev_poll_busy", poll)
        self.assertIn("__sync_lock_release(&netdev_poll_busy)", poll)
        for backend in (
            "e1000_poll_rx();",
            "rtl8139_poll_rx();",
            "ne2000_poll_rx();",
        ):
            with self.subTest(backend=backend):
                self.assertIn(backend, poll)


if __name__ == "__main__":
    unittest.main()
