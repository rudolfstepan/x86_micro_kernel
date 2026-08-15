"""Regression tests for compiler-generated kernel stack evidence."""

from pathlib import Path
import importlib.util
import json
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "stack_evidence", ROOT / "scripts/validate_stack_usage.py")
VALIDATOR = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(VALIDATOR)


class StackEvidenceTests(unittest.TestCase):
    def test_make_builds_separate_gcc_evidence(self) -> None:
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        self.assertIn("check-kernel-stack-analysis:", makefile)
        self.assertIn("-fstack-usage -fcallgraph-info=su", makefile)
        self.assertIn("scripts/validate_stack_usage.py", makefile)
        self.assertIn("STACK_ANALYSIS_OUTPUT_DIR", makefile)
        target = makefile[makefile.index("check-kernel-stack-analysis:"):]
        self.assertLess(target.index("$(MAKE) clean"),
                        target.index("$(MAKE) check-kernel-stack"))

    def test_complete_static_evidence_is_accepted(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "unit.su").write_text(
                "kernel/unit.c:1:1:entry\t64\tstatic\n", encoding="utf-8")
            (root / "unit.ci").write_text(
                'graph: { node: { title: "entry" label: "entry" } }\n',
                encoding="utf-8")
            errors, maximum, _ = VALIDATOR.validate(root, 1, 128)
        self.assertEqual([], errors)
        self.assertEqual(64, maximum)

    def test_missing_dynamic_oversize_and_recursion_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "bad.su").write_text(
                "kernel/bad.c:1:1:loop\t256\tdynamic\n", encoding="utf-8")
            (root / "bad.ci").write_text(
                'graph: {\nnode: { title: "loop" label: "loop" }\n'
                'node: { title: "peer" label: "peer" }\n'
                'edge: { sourcename: "loop" targetname: "loop" }\n'
                'edge: { sourcename: "loop" targetname: "peer" }\n'
                'edge: { sourcename: "peer" targetname: "loop" }\n}\n',
                encoding="utf-8")
            errors, _, _ = VALIDATOR.validate(root, 2, 128)
        self.assertTrue(any("expected 2" in error for error in errors))
        self.assertTrue(any("non-static" in error for error in errors))
        self.assertTrue(any("exceeds" in error for error in errors))
        self.assertTrue(any("recursion" in error for error in errors))
        self.assertTrue(any("callgraph cycle" in error for error in errors))

    def test_pci_scan_has_no_recursive_topology_walk(self) -> None:
        source = (ROOT / "drivers/bus/pci.c").read_text(encoding="utf-8")
        function = source[source.index("void pci_scan_function("):
                          source.index("// Scan a specific slot")]
        self.assertNotIn("pci_scan_bus(", function)
        self.assertEqual(1, function.count("pci_scan_function("))
        self.assertIn("for (unsigned int bus = 1; bus < 256; ++bus)", source)

    def test_cumulative_entry_budget_resolves_declared_indirect_call(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "unit.su").write_text(
                "u.c:1:1:entry\t32\tstatic\n"
                "u.c:2:1:leaf\t64\tstatic\n", encoding="utf-8")
            (root / "unit.ci").write_text(
                'graph: {\n'
                'node: { title: "entry" label: "entry\\nu.c:1:1\\n32 bytes (static)" }\n'
                'node: { title: "leaf" label: "leaf\\nu.c:2:1\\n64 bytes (static)" }\n'
                'node: { title: "__indirect_call" label: "Indirect Call Placeholder" }\n'
                'edge: { sourcename: "entry" targetname: "__indirect_call" }\n}\n',
                encoding="utf-8")
            budget = root / "budgets.json"
            budget.write_text(json.dumps({
                "entry_budgets": [
                    {"name": "irq", "root": "entry", "limit": 96}],
                "indirect_calls": {"entry": ["leaf"]},
                "external_costs": {},
            }), encoding="utf-8")
            errors, _, summary = VALIDATOR.validate(root, 1, 128, budget)
        self.assertEqual([], errors)
        self.assertIn("irq=96/96", summary)

    def test_budget_fails_for_unknown_cost_indirect_or_overrun(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "unit.su").write_text(
                "u.c:1:1:entry\t32\tstatic\n", encoding="utf-8")
            (root / "unit.ci").write_text(
                'graph: {\n'
                'node: { title: "entry" label: "entry\\nu.c:1:1\\n32 bytes (static)" }\n'
                'node: { title: "missing" label: "missing" }\n'
                'node: { title: "__indirect_call" label: "Indirect Call Placeholder" }\n'
                'edge: { sourcename: "entry" targetname: "missing" }\n'
                'edge: { sourcename: "entry" targetname: "__indirect_call" }\n}\n',
                encoding="utf-8")
            budget = root / "budgets.json"
            budget.write_text(json.dumps({
                "entry_budgets": [
                    {"name": "entry", "root": "entry", "limit": 16}],
            }), encoding="utf-8")
            errors, _, _ = VALIDATOR.validate(root, 1, 128, budget)
        self.assertTrue(any("missing stack cost" in error for error in errors))
        self.assertTrue(any("unbound indirect call" in error for error in errors))
        self.assertTrue(any("exceeds" in error for error in errors))

    def test_declared_indirect_cost_is_counted(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "unit.su").write_text(
                "u.c:1:1:entry\t32\tstatic\n", encoding="utf-8")
            (root / "unit.ci").write_text(
                'graph: {\n'
                'node: { title: "entry" label: "entry\\nu.c:1:1\\n32 bytes (static)" }\n'
                'node: { title: "__indirect_call" label: "Indirect Call Placeholder" }\n'
                'edge: { sourcename: "entry" targetname: "__indirect_call" }\n}\n',
                encoding="utf-8")
            budget = root / "budgets.json"
            budget.write_text(json.dumps({
                "entry_budgets": [
                    {"name": "entry", "root": "entry", "limit": 96}],
                "indirect_costs": {"entry": 64},
            }), encoding="utf-8")
            errors, _, summary = VALIDATOR.validate(root, 1, 128, budget)
        self.assertEqual([], errors)
        self.assertIn("entry=96/96", summary)

    def test_entry_reserve_is_counted_and_validated(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "unit.su").write_text(
                "u.c:1:1:entry\t32\tstatic\n", encoding="utf-8")
            (root / "unit.ci").write_text(
                'graph: { node: { title: "entry" '
                'label: "entry\\nu.c:1:1\\n32 bytes (static)" } }\n',
                encoding="utf-8")
            budget = root / "budgets.json"
            budget.write_text(json.dumps({
                "entry_budgets": [{
                    "name": "entry", "root": "entry", "limit": 96,
                    "entry_reserve": 64}],
            }), encoding="utf-8")
            errors, _, summary = VALIDATOR.validate(root, 1, 128, budget)
        self.assertEqual([], errors)
        self.assertIn("entry=96/96", summary)

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "unit.su").write_text(
                "u.c:1:1:entry\t32\tstatic\n", encoding="utf-8")
            (root / "unit.ci").write_text(
                'graph: { node: { title: "entry" '
                'label: "entry\\nu.c:1:1\\n32 bytes (static)" } }\n',
                encoding="utf-8")
            budget = root / "budgets.json"
            budget.write_text(json.dumps({
                "entry_budgets": [{
                    "name": "entry", "root": "entry", "limit": 96,
                    "entry_reserve": -1}],
            }), encoding="utf-8")
            errors, _, _ = VALIDATOR.validate(root, 1, 128, budget)
        self.assertTrue(any("invalid entry budget" in error for error in errors))

    def test_registered_irq_handler_drift_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source_root = root / "source"
            source_root.mkdir()
            (source_root / "irq.c").write_text(
                "register_interrupt_handler(1, (void*)new_handler);\n",
                encoding="utf-8")
            (root / "unit.su").write_text(
                "u.c:1:1:entry\t32\tstatic\n", encoding="utf-8")
            (root / "unit.ci").write_text(
                'graph: { node: { title: "entry" '
                'label: "entry\\nu.c:1:1\\n32 bytes (static)" } }\n',
                encoding="utf-8")
            budget = root / "budgets.json"
            budget.write_text(json.dumps({
                "entry_budgets": [
                    {"name": "entry", "root": "entry", "limit": 32}],
                "registered_irq_handlers": ["old_handler"],
                "exception_handlers": ["old_exception"],
            }), encoding="utf-8")
            errors, _, _ = VALIDATOR.validate(
                root, 1, 128, budget, source_root)
        self.assertTrue(any("unbudgeted" in error for error in errors))
        self.assertTrue(any("stale" in error for error in errors))

    def test_vfs_operation_handler_drift_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source_root = root / "source"
            source_root.mkdir()
            (source_root / "unit_vfs_adapter.c").write_text(
                "struct ops table = { .read = new_read };\n",
                encoding="utf-8")
            (root / "unit.su").write_text(
                "u.c:1:1:entry\t32\tstatic\n", encoding="utf-8")
            (root / "unit.ci").write_text(
                'graph: { node: { title: "entry" '
                'label: "entry\\nu.c:1:1\\n32 bytes (static)" } }\n',
                encoding="utf-8")
            budget = root / "budgets.json"
            budget.write_text(json.dumps({
                "entry_budgets": [{
                    "name": "entry", "root": "entry", "limit": 32}],
                "vfs_read_handlers": ["old_read"],
            }), encoding="utf-8")
            errors, _, _ = VALIDATOR.validate(
                root, 1, 128, budget, source_root)
        self.assertTrue(any(
            "unbudgeted VFS read handler: new_read" in error
            for error in errors))
        self.assertTrue(any(
            "stale VFS read handler budget: old_read" in error
            for error in errors))


if __name__ == "__main__":
    unittest.main()
