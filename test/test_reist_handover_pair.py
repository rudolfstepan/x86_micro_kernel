"""Contracts for replicated failover and fenced repaired-channel rejoin."""

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
    def test_state_frame_is_versioned_and_crc_protected(self):
        import zlib

        payload = (PAIR.smoke.HANDOVER_SERIAL_MAGIC, 1,
                   PAIR.smoke.HANDOVER_SERIAL_STATE,
                   PAIR.smoke.HANDOVER_SERIAL_STATE_FRAME.size,
                   1, 40, 2, 1, 7, 9, 42, 0)
        without_crc = PAIR.smoke.HANDOVER_SERIAL_STATE_FRAME.pack(
            *payload, 0)[:-4]
        frame = PAIR.smoke.HANDOVER_SERIAL_STATE_FRAME.pack(
            *payload, zlib.crc32(without_crc) & 0xFFFFFFFF)
        self.assertEqual(PAIR.smoke.validate_handover_state_frame(frame),
                         (2, 1, 7, 9, 42))
        corrupted = bytearray(frame)
        corrupted[24] ^= 1
        self.assertIsNone(PAIR.smoke.validate_handover_state_frame(
            bytes(corrupted)))

    def test_active_process_is_stopped_before_fence_ack(self):
        source = (SCRIPTS / "run_qemu_handover_pair.py").read_text()
        stop = source.index("smoke.stop_process(active)")
        verified = source.index("active.poll() is None", stop)
        ack = source.index("standby_connection.sendall(smoke.handover_frame", verified)
        self.assertLess(stop, verified)
        self.assertLess(verified, ack)
        self.assertIn("HOST_FENCE_ACTIVE_STOPPED", source)
        self.assertLess(source.index("HANDOVER_SERIAL_READY"),
                        source.index("for replica in replicas"))

    def test_repaired_channel_gets_state_but_no_authority(self):
        source = (SCRIPTS / "run_qemu_handover_pair.py").read_text()
        promoted = source.index("promoted_state = smoke.receive_exact")
        launch = source.index("rejoin = launch", promoted)
        forward = source.index("rejoin_connection.sendall(promoted_state)",
                               launch)
        fenced = source.index("REJOIN_FENCED_MARKER", forward)
        self.assertLess(promoted, launch)
        self.assertLess(launch, forward)
        self.assertLess(forward, fenced)
        self.assertIn("HOST_REJOIN_STATE_FORWARDED", source)

    def test_pair_build_and_reference_gate_are_wired(self):
        makefile = (ROOT / "Makefile").read_text()
        build = (ROOT / "scripts/build-windows.ps1").read_text()
        gate = (ROOT / "scripts/test-reist-handover-pair.ps1").read_text()
        self.assertIn("test-smoke-handover-pair:", makefile)
        self.assertIn("HANDOVER_NODE_ID=1", makefile)
        self.assertIn("HANDOVER_NODE_ID=2", makefile)
        self.assertIn("HANDOVER_NODE_ID=3", makefile)
        self.assertIn("[ValidateRange(0, 3)]", build)
        self.assertIn("run_qemu_handover_pair.py", gate)
        self.assertIn("-HandoverNodeId 1", gate)
        self.assertIn("-HandoverNodeId 2", gate)
        self.assertIn("-HandoverNodeId 3", gate)
        self.assertIn("--rejoin-image", gate)


if __name__ == "__main__":
    unittest.main()
