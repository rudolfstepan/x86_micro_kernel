"""Behavior tests for the versioned REIST hazard register."""

from pathlib import Path
import importlib.util
import tempfile
import tomllib
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "hazard_validator", ROOT / "scripts/validate_hazard_register.py")
VALIDATOR = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(VALIDATOR)

TRACE_SPEC = importlib.util.spec_from_file_location(
    "hazard_traceability", ROOT / "scripts/run_hazard_traceability.py")
TRACE = importlib.util.module_from_spec(TRACE_SPEC)
assert TRACE_SPEC.loader is not None
TRACE_SPEC.loader.exec_module(TRACE)


class HazardRegisterTests(unittest.TestCase):
    def test_repository_register_is_traceable(self) -> None:
        self.assertEqual([], VALIDATOR.validate(
            ROOT / "safety/hazards.toml", ROOT))

    def test_complete_v2_baseline_has_explicit_scope_and_full_categories(self):
        scope = tomllib.loads(
            (ROOT / "safety/assurance_scope.toml").read_text("utf-8")
        )
        register = tomllib.loads(
            (ROOT / "safety/hazards.toml").read_text("utf-8")
        )
        self.assertEqual(1, scope["schema_version"])
        self.assertEqual("complete", scope["status"])
        self.assertEqual(["generic-research"], scope["selected_profiles"])
        self.assertGreaterEqual(len(scope["component"]), 30)
        self.assertGreaterEqual(len(register["hazard"]), 19)
        self.assertEqual("complete", register["status"])
        self.assertEqual(
            {"core", "device", "filesystem", "service", "profile"},
            {item["category"] for item in register["hazard"]},
        )

    def test_legacy_v1_register_remains_accepted(self) -> None:
        content = """schema_version = 1
status = "partial"
[[hazard]]
id = "HZ-TEST-009"
title = "legacy"
severity = "critical"
safe_state = "safe"
ftti_ms = 1
controls = ["kernel/init/fatal.c"]
verification = ["test/test_reist_fatal_containment.py"]
residual_risk = "known"
"""
        with tempfile.TemporaryDirectory() as directory:
            register = Path(directory) / "hazards.toml"
            register.write_text(content, encoding="utf-8")
            self.assertEqual([], VALIDATOR.validate(register, ROOT))

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

    def test_traceability_baseline_binds_register_tests_and_results(self) -> None:
        baseline = TRACE.build_baseline(
            ROOT / "safety/hazards.toml", ROOT,
            lambda path: (True, path.name))
        self.assertTrue(baseline["passed"])
        self.assertEqual(2, baseline["schema_version"])
        self.assertEqual(64, len(baseline["register_sha256"]))
        self.assertEqual(64, len(baseline["scope_sha256"]))
        self.assertGreaterEqual(len(baseline["hazards"]), 19)
        self.assertGreaterEqual(len(baseline["requirements"]), 8)
        self.assertGreaterEqual(len(baseline["artifacts"]), 50)
        self.assertTrue(all(len(item["sha256"]) == 64
                            for item in baseline["tests"].values()))

    def test_complete_register_fails_when_scope_component_is_uncovered(self):
        content = (ROOT / "safety/hazards.toml").read_text("utf-8")
        content = content.replace(
            'components = ["CMP-AUDIO"]',
            'components = ["CMP-DISPLAY-GUI"]',
            1,
        )
        with tempfile.TemporaryDirectory() as directory:
            register = Path(directory) / "hazards.toml"
            register.write_text(content, encoding="utf-8")
            errors = VALIDATOR.validate(
                register, ROOT, ROOT / "safety/assurance_scope.toml"
            )
        self.assertTrue(any(
            "uncovered components" in error and "CMP-AUDIO" in error
            for error in errors
        ))

    def test_v2_unknown_references_and_missing_acceptance_fail_closed(self):
        content = (ROOT / "safety/hazards.toml").read_text("utf-8")
        content = content.replace("CMP-MEMORY", "CMP-NOT-IN-SCOPE", 1)
        content = content.replace("acceptance_criteria = [", "criteria = [", 1)
        with tempfile.TemporaryDirectory() as directory:
            register = Path(directory) / "hazards.toml"
            register.write_text(content, encoding="utf-8")
            errors = VALIDATOR.validate(
                register, ROOT, ROOT / "safety/assurance_scope.toml"
            )
        self.assertTrue(any("is unknown: CMP-NOT-IN-SCOPE" in error
                            for error in errors))
        self.assertTrue(any("acceptance_criteria" in error for error in errors))

    def test_failed_verification_fails_hazard_and_baseline(self) -> None:
        failed_path = "test/test_reist_fatal_containment.py"

        def runner(path: Path) -> tuple[bool, str]:
            return path.relative_to(ROOT).as_posix() != failed_path, "injected"

        baseline = TRACE.build_baseline(
            ROOT / "safety/hazards.toml", ROOT, runner)
        self.assertFalse(baseline["passed"])
        self.assertEqual({"HZ-KERNEL-001"},
                         {item["id"] for item in baseline["hazards"]
                          if not item["passed"]})


if __name__ == "__main__":
    unittest.main()
