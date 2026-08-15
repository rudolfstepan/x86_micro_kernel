"""Behavior tests for the machine-verifiable REIST resource budgets."""

from pathlib import Path
import importlib.util
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "resource_budget_validator", ROOT / "scripts/validate_resource_budgets.py")
VALIDATOR = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(VALIDATOR)


class ResourceBudgetTests(unittest.TestCase):
    def test_repository_register_matches_source_limits(self) -> None:
        self.assertEqual([], VALIDATOR.validate(
            ROOT / "safety/resource_budgets.toml", ROOT))

    def _fixture(self, directory: str, *, limit: int = 8,
                 duplicate: bool = False, source: str = "include/limits.h") -> Path:
        root = Path(directory)
        (root / "include").mkdir()
        (root / "test").mkdir()
        (root / "include/limits.h").write_text(
            "#define TEST_LIMIT (2U * 4U)\n", encoding="utf-8")
        (root / "test/proof.py").write_text("# proof\n", encoding="utf-8")
        entry = f'''[[budget]]
id = "RB-TEST-LIMIT"
description = "test"
source = "{source}"
symbol = "TEST_LIMIT"
limit = {limit}
unit = "slots"
verification = ["test/proof.py"]
'''
        register = root / "budgets.toml"
        register.write_text(
            'schema_version = 1\nstatus = "partial"\n' + entry +
            (entry if duplicate else ""), encoding="utf-8")
        return register

    def test_safe_integer_expression_is_evaluated_without_drift(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            register = self._fixture(directory)
            self.assertEqual([], VALIDATOR.validate(register, Path(directory)))

    def test_limit_drift_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            register = self._fixture(directory, limit=7)
            errors = VALIDATOR.validate(register, Path(directory))
        self.assertTrue(any("limit drift" in error for error in errors))

    def test_duplicate_id_and_symbol_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            register = self._fixture(directory, duplicate=True)
            errors = VALIDATOR.validate(register, Path(directory))
        self.assertTrue(any("duplicated" in error for error in errors))
        self.assertTrue(any("registered more than once" in error for error in errors))

    def test_path_escape_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            register = self._fixture(directory, source="../limits.h")
            errors = VALIDATOR.validate(register, Path(directory))
        self.assertTrue(any("unsafe path" in error for error in errors))

    def test_unknown_macro_expression_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            register = self._fixture(directory)
            (Path(directory) / "include/limits.h").write_text(
                "#define TEST_LIMIT OTHER_LIMIT\n", encoding="utf-8")
            errors = VALIDATOR.validate(register, Path(directory))
        self.assertTrue(any("unsupported value" in error for error in errors))

    def test_oversized_shift_is_rejected_without_evaluation(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            register = self._fixture(directory)
            (Path(directory) / "include/limits.h").write_text(
                "#define TEST_LIMIT (1U << 999999999U)\n", encoding="utf-8")
            errors = VALIDATOR.validate(register, Path(directory))
        self.assertTrue(any("shift exceeds" in error for error in errors))


if __name__ == "__main__":
    unittest.main()
