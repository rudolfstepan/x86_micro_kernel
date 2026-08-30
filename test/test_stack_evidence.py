"""Regression tests for compiler-generated kernel stack evidence."""

from pathlib import Path
import importlib.util
import json
import subprocess
import sys
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
        self.assertIn("-Werror=frame-larger-than=4096", makefile)
        self.assertIn("scripts/validate_stack_usage.py", makefile)
        self.assertIn("STACK_ANALYSIS_OUTPUT_DIR", makefile)
        self.assertIn("check-kernel-stack-c-objects: $(C_OBJ)", makefile)
        self.assertIn("--expected $(words $(C_OBJ))", makefile)
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

    def test_gcc_clone_uses_canonical_indirect_binding(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "unit.su").write_text(
                "u.c:1:1:entry.part.0\t32\tstatic\n"
                "u.c:2:1:leaf\t64\tstatic\n", encoding="utf-8")
            (root / "unit.ci").write_text(
                'graph: {\n'
                'node: { title: "entry.part.0" '
                'label: "entry.part.0\\n32 bytes (static)" }\n'
                'node: { title: "leaf" '
                'label: "leaf\\n64 bytes (static)" }\n'
                'node: { title: "__indirect_call" '
                'label: "Indirect Call Placeholder" }\n'
                'edge: { sourcename: "entry.part.0" '
                'targetname: "__indirect_call" }\n}\n',
                encoding="utf-8")
            budget = root / "budgets.json"
            budget.write_text(json.dumps({
                "entry_budgets": [{
                    "name": "entry", "root": "entry.part.0", "limit": 96,
                }],
                "indirect_calls": {"entry": ["leaf"]},
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

    def test_shared_callgraph_is_evaluated_in_bounded_time(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            levels = 28
            names = ["entry"] + [
                f"level_{level}_{branch}"
                for level in range(levels)
                for branch in ("a", "b")
            ]
            (root / "shared.su").write_text(
                "".join(
                    f"u.c:{index + 1}:1:{name}\t32\tstatic\n"
                    for index, name in enumerate(names)
                ),
                encoding="utf-8",
            )
            graph = ["graph: {\n"]
            for name in names:
                graph.append(
                    f'node: {{ title: "{name}" '
                    f'label: "{name}\\n32 bytes (static)" }}\n'
                )
            graph.extend([
                'edge: { sourcename: "entry" '
                'targetname: "level_0_a" }\n',
                'edge: { sourcename: "entry" '
                'targetname: "level_0_b" }\n',
            ])
            for level in range(levels - 1):
                for branch in ("a", "b"):
                    for target in ("a", "b"):
                        graph.append(
                            'edge: { sourcename: '
                            f'"level_{level}_{branch}" targetname: '
                            f'"level_{level + 1}_{target}" }}\n'
                        )
            graph.append("}\n")
            (root / "shared.ci").write_text(
                "".join(graph), encoding="utf-8"
            )
            budget = root / "budgets.json"
            expected_cost = (levels + 1) * 32
            budget.write_text(json.dumps({
                "entry_budgets": [{
                    "name": "shared", "root": "entry",
                    "limit": expected_cost,
                }],
            }), encoding="utf-8")
            command = [
                sys.executable,
                str(ROOT / "scripts" / "validate_stack_usage.py"),
                "--root", str(root),
                "--expected", "1",
                "--local-limit", "128",
                "--budget-file", str(budget),
            ]
            completed = subprocess.run(
                command, check=False, capture_output=True, text=True,
                timeout=3,
            )
        self.assertEqual(0, completed.returncode, completed.stderr)
        self.assertIn(
            f"entry-budgets=shared={expected_cost}/{expected_cost}",
            completed.stdout,
        )

    def test_indirect_callgraph_cycle_is_not_hidden(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "unit.su").write_text(
                "u.c:1:1:entry\t32\tstatic\n"
                "u.c:2:1:leaf\t32\tstatic\n",
                encoding="utf-8",
            )
            (root / "unit.ci").write_text(
                'graph: {\n'
                'node: { title: "entry" label: "entry" }\n'
                'node: { title: "leaf" label: "leaf" }\n'
                'node: { title: "__indirect_call" '
                'label: "Indirect Call Placeholder" }\n'
                'edge: { sourcename: "entry" '
                'targetname: "__indirect_call" }\n'
                'edge: { sourcename: "leaf" targetname: "entry" }\n'
                '}\n',
                encoding="utf-8",
            )
            budget = root / "budgets.json"
            budget.write_text(json.dumps({
                "entry_budgets": [{
                    "name": "entry", "root": "entry", "limit": 128,
                }],
                "indirect_calls": {"entry": ["leaf"]},
            }), encoding="utf-8")
            errors, _, _ = VALIDATOR.validate(root, 1, 128, budget)
        self.assertTrue(any(
            "cycle in budgeted path" in error for error in errors
        ))

    def test_atomic_panic_guard_bounds_exactly_one_terminal_reentry(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "unit.su").write_text(
                "u.c:1:1:entry\t32\tstatic\n"
                "u.c:2:1:work\t40\tstatic\n"
                "u.c:3:1:panic\t48\tstatic\n",
                encoding="utf-8",
            )
            (root / "unit.ci").write_text(
                'graph: {\n'
                'node: { title: "entry" label: "entry\\n32 bytes (static)" }\n'
                'node: { title: "work" label: "work\\n40 bytes (static)" }\n'
                'node: { title: "panic" label: "panic\\n48 bytes (static)" }\n'
                'edge: { sourcename: "entry" targetname: "work" }\n'
                'edge: { sourcename: "work" targetname: "panic" }\n'
                'edge: { sourcename: "panic" targetname: "work" }\n'
                '}\n',
                encoding="utf-8",
            )
            budget = root / "budgets.json"
            budget.write_text(json.dumps({
                "entry_budgets": [{
                    "name": "entry", "root": "entry", "limit": 208,
                }],
                "guarded_reentry_groups": [{
                    "name": "panic_in_progress",
                    "members": ["panic"],
                }],
            }), encoding="utf-8")
            errors, _, summary = VALIDATOR.validate(root, 1, 256, budget)
        self.assertEqual([], errors)
        self.assertIn("entry-budgets=entry=208/208", summary)

    def test_panic_guard_does_not_hide_cycle_after_guard_activation(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            names = ("entry", "panic", "first", "second")
            (root / "unit.su").write_text(
                "".join(
                    f"u.c:{index}:1:{name}\t32\tstatic\n"
                    for index, name in enumerate(names, 1)
                ),
                encoding="utf-8",
            )
            graph = ["graph: {\n"]
            for name in names:
                graph.append(
                    f'node: {{ title: "{name}" '
                    f'label: "{name}\\n32 bytes (static)" }}\n'
                )
            graph.extend([
                'edge: { sourcename: "entry" targetname: "panic" }\n',
                'edge: { sourcename: "panic" targetname: "first" }\n',
                'edge: { sourcename: "first" targetname: "second" }\n',
                'edge: { sourcename: "second" targetname: "first" }\n',
                '}\n',
            ])
            (root / "unit.ci").write_text("".join(graph), encoding="utf-8")
            budget = root / "budgets.json"
            budget.write_text(json.dumps({
                "entry_budgets": [{
                    "name": "entry", "root": "entry", "limit": 512,
                }],
                "guarded_reentry_groups": [{
                    "name": "panic_in_progress",
                    "members": ["panic"],
                }],
            }), encoding="utf-8")
            errors, _, _ = VALIDATOR.validate(root, 1, 256, budget)
        self.assertTrue(any(
            "recursive callgraph cycle is forbidden" in error
            for error in errors
        ))


if __name__ == "__main__":
    unittest.main()
