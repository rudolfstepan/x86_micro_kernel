"""Behavior tests for the external watchdog/interlock acceptance contract."""

from __future__ import annotations

import hashlib
import importlib.util
from pathlib import Path
import tempfile
import tomllib
import unittest

ROOT = Path(__file__).resolve().parents[1]
PROFILE = ROOT / "safety/external_safety_monitor.toml"
SPEC = importlib.util.spec_from_file_location(
    "external_monitor_validator",
    ROOT / "scripts/validate_external_safety_monitor.py")
VALIDATOR = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(VALIDATOR)


class ExternalSafetyMonitorContractTests(unittest.TestCase):
    def test_repository_profile_is_valid_and_explicitly_unbound(self) -> None:
        self.assertEqual([], VALIDATOR.validate(PROFILE, ROOT))
        profile = tomllib.loads(PROFILE.read_text(encoding="utf-8"))
        self.assertEqual("unbound", profile["status"])
        self.assertFalse(profile["qualification"]["complete"])
        self.assertEqual([], profile["qualification"]["physical_evidence"])

    def _fixture(self, directory: str, transform=lambda text: text) -> Path:
        root = Path(directory)
        (root / "safety").mkdir()
        (root / "safety/hazards.toml").write_bytes(
            (ROOT / "safety/hazards.toml").read_bytes())
        (root / "safety/assurance_scope.toml").write_bytes(
            (ROOT / "safety/assurance_scope.toml").read_bytes())
        profile = root / "safety/external_safety_monitor.toml"
        profile.write_text(transform(PROFILE.read_text(encoding="utf-8")),
                           encoding="utf-8")
        return profile

    def _qualified(self, directory: str) -> Path:
        root = Path(directory)
        evidence_dir = root / "evidence"
        evidence_dir.mkdir(parents=True)
        report = evidence_dir / "report.md"
        raw_log = evidence_dir / "raw.jsonl"
        report.write_text("physical campaign report\n", encoding="utf-8")
        raw_log.write_text('{"result":"pass"}\n', encoding="utf-8")
        report_sha = hashlib.sha256(report.read_bytes()).hexdigest()
        log_sha = hashlib.sha256(raw_log.read_bytes()).hexdigest()

        def transform(text: str) -> str:
            text = text.replace('status = "unbound"', 'status = "qualified"', 1)
            text = text.replace('profile_id = "REIST-EXTMON-UNBOUND"',
                                'profile_id = "REIST-EXTMON-TEST"', 1)
            text = text.replace('selected = false\nsystem_id = ""',
                                'selected = true\nsystem_id = "target-01"', 1)
            text = text.replace('board_model = ""', 'board_model = "board"', 1)
            text = text.replace('board_revision = ""',
                                'board_revision = "rev-a"', 1)
            text = text.replace('serial_number = ""',
                                'serial_number = "target-serial"', 1)
            text = text.replace('selected = false\nmanufacturer = ""',
                                'selected = true\nmanufacturer = "vendor"', 1)
            text = text.replace('model = ""', 'model = "monitor"', 1)
            text = text.replace('hardware_revision = ""',
                                'hardware_revision = "rev-b"', 1)
            text = text.replace('firmware_version = ""',
                                'firmware_version = "1.0.0"', 1)
            text = text.replace('firmware_sha256 = ""',
                                f'firmware_sha256 = "{"a" * 64}"', 1)
            text = text.replace('transport = ""',
                                'transport = "isolated-uart"', 1)
            for field in (
                "separate_power_domain", "separate_clock_domain",
                "target_cpu_reset_independent",
                "hazard_output_driver_independent",
            ):
                text = text.replace(f"{field} = false", f"{field} = true", 1)
            text = text.replace("complete = false", "complete = true", 1)
            text = text.replace("physical_evidence = []\n", "", 1)
            blocks = []
            for scenario in sorted(VALIDATOR.SCENARIOS):
                blocks.append(f'''[[qualification.physical_evidence]]
scenario = "{scenario}"
kind = "physical-target"
repetitions = 3
result = "pass"
report = "evidence/report.md"
report_sha256 = "{report_sha}"
raw_log = "evidence/raw.jsonl"
raw_log_sha256 = "{log_sha}"
''')
            return text + "\n" + "\n".join(blocks)

        return self._fixture(directory, transform)

    def test_deadline_allocation_cannot_exceed_hazard_ftti(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            profile = self._fixture(directory, lambda text: text.replace(
                "ftti_ms = 1000", "ftti_ms = 999", 1))
            errors = VALIDATOR.validate(profile, Path(directory))
        self.assertTrue(any("exceeds ftti" in error for error in errors))

    def test_qualified_label_without_hardware_or_evidence_fails(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            profile = self._fixture(directory, lambda text: text.replace(
                'status = "unbound"', 'status = "qualified"', 1))
            errors = VALIDATOR.validate(profile, Path(directory))
        self.assertTrue(any("hardware" in error for error in errors))
        self.assertTrue(any("physical scenarios" in error for error in errors))

    def test_command_echo_cannot_be_used_as_fence_readback(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            profile = self._fixture(directory, lambda text: text.replace(
                "command_echo_is_readback = false",
                "command_echo_is_readback = true", 1))
            errors = VALIDATOR.validate(profile, Path(directory))
        self.assertTrue(any("command_echo_is_readback" in error
                            for error in errors))

    def test_protocol_must_reject_replay(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            profile = self._fixture(directory, lambda text: text.replace(
                "reject_replay = true", "reject_replay = false", 1))
            errors = VALIDATOR.validate(profile, Path(directory))
        self.assertTrue(any("reject_replay" in error for error in errors))

    def test_unbound_profile_rejects_partial_hardware_identity(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            profile = self._fixture(directory, lambda text: text.replace(
                'board_model = ""', 'board_model = "guess"', 1))
            errors = VALIDATOR.validate(profile, Path(directory))
        self.assertTrue(any("partial hardware identity" in error
                            for error in errors))

    def test_unknown_hazard_link_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            profile = self._fixture(directory, lambda text: text.replace(
                'hazard_id = "HZ-MONITOR-001"',
                'hazard_id = "HZ-MISSING-999"', 1))
            errors = VALIDATOR.validate(profile, Path(directory))
        self.assertTrue(any("resolve exactly once" in error
                            for error in errors))

    def test_complete_physical_campaign_can_qualify(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            profile = self._qualified(directory)
            self.assertEqual([], VALIDATOR.validate(profile, Path(directory)))

    def test_host_model_cannot_replace_physical_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            profile = self._qualified(directory)
            profile.write_text(profile.read_text(encoding="utf-8").replace(
                'kind = "physical-target"', 'kind = "host-model"', 1),
                encoding="utf-8")
            errors = VALIDATOR.validate(profile, Path(directory))
        self.assertTrue(any("kind must be physical-target" in error
                            for error in errors))

    def test_evidence_hash_mismatch_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            profile = self._qualified(directory)
            text = profile.read_text(encoding="utf-8")
            marker = 'report_sha256 = "'
            start = text.index(marker) + len(marker)
            text = text[:start] + ("0" * 64) + text[start + 64:]
            profile.write_text(text, encoding="utf-8")
            errors = VALIDATOR.validate(profile, Path(directory))
        self.assertTrue(any("SHA-256 mismatch" in error for error in errors))

    def test_evidence_path_escape_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            profile = self._qualified(directory)
            profile.write_text(profile.read_text(encoding="utf-8").replace(
                'report = "evidence/report.md"', 'report = "../report.md"', 1),
                encoding="utf-8")
            errors = VALIDATOR.validate(profile, Path(directory))
        self.assertTrue(any("unsafe path" in error for error in errors))


if __name__ == "__main__":
    unittest.main()
