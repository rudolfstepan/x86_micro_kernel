import copy
import sys
import tempfile
import tomllib
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from scripts.validate_automated_s0_gate import EVIDENCE, validate_gate


class AutomatedS0GateTests(unittest.TestCase):
    def setUp(self):
        self.gate_path = ROOT / "safety/automated_s0_gate.toml"
        self.scope_path = ROOT / "safety/assurance_scope.toml"
        self.queue_path = ROOT / "automation/reist-s03b.toml"

    def test_repository_gate_is_complete_and_bounded(self):
        self.assertEqual(
            validate_gate(self.gate_path, self.scope_path, self.queue_path),
            len(EVIDENCE),
        )

    def test_manual_exclusion_or_residual_cannot_disappear(self):
        gate = self._load(self.gate_path)
        gate["manual_exclusions"].pop()
        with self.assertRaisesRegex(ValueError, "manual_exclusions"):
            self._validate_mutated_gate(gate)
        gate = self._load(self.gate_path)
        gate["residuals"].pop()
        with self.assertRaisesRegex(ValueError, "residuals"):
            self._validate_mutated_gate(gate)

    def test_evidence_command_and_target_are_frozen(self):
        gate = self._load(self.gate_path)
        gate["evidence"][0]["command"] = "true"
        with self.assertRaisesRegex(ValueError, "evidence matrix"):
            self._validate_mutated_gate(gate)
        gate = self._load(self.gate_path)
        gate["automated_targets"] = ["qemu-i386", "real-hardware"]
        with self.assertRaisesRegex(ValueError, "automated_targets"):
            self._validate_mutated_gate(gate)

    def test_open_predecessor_and_unexpected_active_package_fail(self):
        queue = self._load(self.queue_path)
        predecessor = next(
            item for item in queue["packages"] if item["status"] == "done"
        )
        predecessor["status"] = "queued"
        with self.assertRaisesRegex(ValueError, "predecessor"):
            self._validate_mutated_queue(queue)
        queue = self._load(self.queue_path)
        queue["active_id"] = predecessor["id"]
        with self.assertRaisesRegex(ValueError, "sole active"):
            self._validate_mutated_queue(queue)

    def test_explicit_post_s0_package_may_be_active(self):
        queue = self._load(self.queue_path)
        active = next(
            item for item in queue["packages"]
            if item.get("phase") == "post-s0" and item["status"] == "active"
        )
        self.assertEqual(queue["active_id"], active["id"])
        self.assertEqual(
            validate_gate(self.gate_path, self.scope_path, self.queue_path),
            len(EVIDENCE),
        )
        active.pop("phase")
        with self.assertRaisesRegex(ValueError, "predecessor"):
            self._validate_mutated_queue(queue)

    def test_validator_does_not_import_gate_data_as_code(self):
        source = (ROOT / "scripts/validate_automated_s0_gate.py").read_text("utf-8")
        self.assertNotIn("import automated_s0_gate", source)
        self.assertNotIn("exec(", source)

    @staticmethod
    def _load(path):
        with path.open("rb") as stream:
            return copy.deepcopy(tomllib.load(stream))

    def _validate_mutated_gate(self, gate):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "gate.toml"
            self._write_gate(path, gate)
            return validate_gate(path, self.scope_path, self.queue_path)

    def _validate_mutated_queue(self, queue):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "queue.toml"
            self._write_queue(path, queue)
            return validate_gate(self.gate_path, self.scope_path, path)

    @staticmethod
    def _quote(value):
        return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'

    @classmethod
    def _write_gate(cls, path, gate):
        lines = [
            f"schema_version = {gate['schema_version']}",
            f"gate_id = {cls._quote(gate['gate_id'])}",
            f"baseline_id = {cls._quote(gate['baseline_id'])}",
            f"status = {cls._quote(gate['status'])}",
        ]
        for key in ("automated_targets", "manual_exclusions", "residuals"):
            values = ", ".join(cls._quote(item) for item in gate[key])
            lines.append(f"{key} = [{values}]")
        for item in gate["cancelled_package"]:
            lines += ["", "[[cancelled_package]]",
                      f"id = {cls._quote(item['id'])}",
                      f"disposition = {cls._quote(item['disposition'])}"]
        for item in gate["evidence"]:
            lines += ["", "[[evidence]]"] + [
                f"{key} = {cls._quote(item[key])}"
                for key in ("id", "kind", "target", "command")
            ]
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")

    @classmethod
    def _write_queue(cls, path, queue):
        lines = [f"active_id = {cls._quote(queue['active_id'])}"]
        for package in queue["packages"]:
            lines += ["", "[[packages]]",
                      f"id = {cls._quote(package['id'])}",
                      f"status = {cls._quote(package['status'])}"]
            if "phase" in package:
                lines.append(f"phase = {cls._quote(package['phase'])}")
            if package["id"] == "S0.6c-automated-research-gate":
                for key in ("targeted_tests", "package_tests", "runtime_tests"):
                    values = ", ".join(cls._quote(item) for item in package[key])
                    lines.append(f"{key} = [{values}]")
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")


if __name__ == "__main__":
    unittest.main()
