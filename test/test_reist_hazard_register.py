"""Behavior tests for the versioned REIST hazard register."""

from pathlib import Path
import importlib.util
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "hazard_validator", ROOT / "scripts/validate_hazard_register.py")
VALIDATOR = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(VALIDATOR)


class HazardRegisterTests(unittest.TestCase):
    def test_repository_register_is_traceable(self) -> None:
        self.assertEqual([], VALIDATOR.validate(
            ROOT / "safety/hazards.toml", ROOT))

    def test_duplicate_and_missing_evidence_fail_closed(self) -> None:
        content = """schema_version = 1
status = "partial"
[[hazard]]
id = "HZ-TEST-001"
title = "x"
severity = "critical"
safe_state = "safe"
ftti_ms = 1
controls = ["missing.c"]
verification = ["test/missing.py"]
residual_risk = "known"
[[hazard]]
id = "HZ-TEST-001"
title = "y"
severity = "critical"
safe_state = "safe"
ftti_ms = 1
controls = ["missing.c"]
verification = ["test/missing.py"]
residual_risk = "known"
"""
        with tempfile.TemporaryDirectory() as directory:
            register = Path(directory) / "hazards.toml"
            register.write_text(content, encoding="utf-8")
            errors = VALIDATOR.validate(register, ROOT)
        self.assertTrue(any("duplicated" in error for error in errors))
        self.assertTrue(any("does not exist" in error for error in errors))

    def test_invalid_budget_and_path_escape_are_rejected(self) -> None:
        content = """schema_version = 1
status = "partial"
[[hazard]]
id = "HZ-TEST-002"
title = "x"
severity = "critical"
safe_state = "safe"
ftti_ms = 0
controls = ["../outside"]
verification = ["docs/not-a-test.md"]
residual_risk = "known"
"""
        with tempfile.TemporaryDirectory() as directory:
            register = Path(directory) / "hazards.toml"
            register.write_text(content, encoding="utf-8")
            errors = VALIDATOR.validate(register, ROOT)
        self.assertTrue(any("positive integer" in error for error in errors))
        self.assertTrue(any("unsafe path" in error for error in errors))
        self.assertTrue(any("must reference test/" in error for error in errors))


if __name__ == "__main__":
    unittest.main()
