"""Contracts for the two-QEMU fenced handover reference profile."""

from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / "scripts"
sys.path.insert(0, str(SCRIPTS))
SPEC = importlib.util.spec_from_file_location(
    "run_qemu_handover_pair", SCRIPTS / "run_qemu_handover_pair.py")
PAIR = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(PAIR)


class ReistHandoverPairTests(unittest.TestCase):
    def test_replica_frame_is_versioned_and_crc_protected(self):
        frame = PAIR.smoke.handover_frame(
            PAIR.smoke.HANDOVER_SERIAL_REPLICA, 1, 7)
        self.assertEqual(PAIR.smoke.validate_handover_frame(
            frame, PAIR.smoke.HANDOVER_SERIAL_REPLICA), (1, 7))
        self.assertIsNone(PAIR.smoke.validate_handover_frame(
            frame, PAIR.smoke.HANDOVER_SERIAL_REQUEST))

    def test_active_process_is_stopped_before_fence_ack(self):
        source = (SCRIPTS / "run_qemu_handover_pair.py").read_text()
        stop = source.index("smoke.stop_process(active)")
        verified = source.index("active.poll() is None", stop)
        ack = source.index("standby_connection.sendall(smoke.handover_frame", verified)
        self.assertLess(stop, verified)
        self.assertLess(verified, ack)
        self.assertIn("HOST_FENCE_ACTIVE_STOPPED", source)
        self.assertLess(source.index("HANDOVER_SERIAL_READY"),
                        source.index("standby_connection.sendall(replica)"))

    def test_pair_build_and_reference_gate_are_wired(self):
        makefile = (ROOT / "Makefile").read_text()
        build = (ROOT / "scripts/build-windows.ps1").read_text()
        gate = (ROOT / "scripts/test-reist-handover-pair.ps1").read_text()
        self.assertIn("test-smoke-handover-pair:", makefile)
        self.assertIn("HANDOVER_NODE_ID=1", makefile)
        self.assertIn("HANDOVER_NODE_ID=2", makefile)
        self.assertIn("[ValidateRange(0, 2)]", build)
        self.assertIn("run_qemu_handover_pair.py", gate)
        self.assertIn("-HandoverNodeId 1", gate)
        self.assertIn("-HandoverNodeId 2", gate)


if __name__ == "__main__":
    unittest.main()
