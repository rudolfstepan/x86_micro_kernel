#!/usr/bin/env python3
"""Validate the bounded automated REIST S0 research gate contract."""

from __future__ import annotations

import argparse
import tomllib
from pathlib import Path


GATE_PACKAGE = "S0.6c-automated-research-gate"
TOP_KEYS = {
    "schema_version", "gate_id", "baseline_id", "status",
    "automated_targets", "manual_exclusions", "residuals",
    "cancelled_package", "evidence",
}
MANUAL_EXCLUSIONS = {
    "electrical fence readback",
    "physical external-monitor qualification",
    "target-hardware common-cause failover",
    "target-hardware fault injection",
    "target-hardware storage reconnect",
    "target-hardware WCET",
}
RESIDUALS = {
    "anti-rollback",
    "certification and product-profile qualification",
    "immutable recovery",
    "long-duration soak",
    "online update distribution",
    "production key custody",
    "reproducible builds",
    "signed provenance",
}
CANCELLED = {
    "A0.1-vmware-host-verifier": "not-required-by-direct-local-execution",
    "S0.3c-6f5-vmware-fdd-reconnect":
        "superseded-by-S0.3c-6f6s-fat12-persistence-finish",
}
EVIDENCE = {
    "host-suite": (
        "host", "host",
        'python -m unittest discover -s test -p "test_*.py" -v',
    ),
    "qemu-package-vga": (
        "package", "qemu-i386",
        ".\\scripts\\test-reist-package.ps1 -Target qemu -Video vga",
    ),
    "vmware-package-vga": (
        "package", "vmware-i386",
        ".\\scripts\\test-reist-package.ps1 -Target vmware -Video vga",
    ),
    "qemu-pit": (
        "runtime", "qemu-i386",
        ".\\scripts\\test-reist-runtime.ps1 -Target qemu -Video vga -Mode pit",
    ),
    "qemu-watchdog": (
        "runtime", "qemu-i386",
        ".\\scripts\\test-reist-runtime.ps1 -Target qemu -Video vga -Mode watchdog",
    ),
    "qemu-storage-recovery": (
        "runtime", "qemu-i386",
        ".\\scripts\\test-reist-runtime.ps1 -Target qemu -Video vga -Mode storage-recovery",
    ),
    "qemu-memory": (
        "runtime", "qemu-i386",
        ".\\scripts\\test-reist-runtime.ps1 -Target qemu -Video vga -Mode memory",
    ),
    "qemu-framebuffer": (
        "runtime", "qemu-i386",
        ".\\scripts\\test-reist-runtime.ps1 -Target qemu -Video framebuffer -Mode normal",
    ),
    "vmware-containment": (
        "runtime", "vmware-i386", ".\\scripts\\run_vmware_containment.cmd",
    ),
}


def _load(path: Path) -> dict:
    with path.open("rb") as stream:
        value = tomllib.load(stream)
    if not isinstance(value, dict):
        raise ValueError(f"{path} is not a TOML table")
    return value


def _fixed_string_set(value: object, expected: set[str], field: str) -> None:
    if not isinstance(value, list) or not all(isinstance(item, str) for item in value):
        raise ValueError(f"{field} must be a string array")
    if len(value) != len(set(value)) or set(value) != expected:
        raise ValueError(f"{field} does not match the frozen set")


def validate_gate(gate_path: Path, scope_path: Path, queue_path: Path) -> int:
    gate = _load(gate_path)
    scope = _load(scope_path)
    queue = _load(queue_path)
    if set(gate) != TOP_KEYS:
        raise ValueError("gate fields do not match schema v1")
    identity = (
        gate.get("schema_version"), gate.get("gate_id"),
        gate.get("baseline_id"), gate.get("status"),
    )
    if identity != (
            1, "REIST-S0-AUTOMATED-RESEARCH-v1", "REIST-research",
            "automated-emulator-complete"):
        raise ValueError("gate identity or qualified status is invalid")
    if scope.get("baseline_id") != gate["baseline_id"] or \
            scope.get("status") != "complete":
        raise ValueError("gate is not bound to the complete assurance scope")
    _fixed_string_set(
        gate["automated_targets"], {"qemu-i386", "vmware-i386"},
        "automated_targets",
    )
    if set(scope.get("automated_targets", [])) != set(gate["automated_targets"]):
        raise ValueError("automated targets differ from the assurance scope")
    _fixed_string_set(gate["manual_exclusions"], MANUAL_EXCLUSIONS,
                      "manual_exclusions")
    if not set(scope.get("manual_evidence_only", [])).issubset(MANUAL_EXCLUSIONS):
        raise ValueError("assurance-scope manual evidence is not excluded")
    _fixed_string_set(gate["residuals"], RESIDUALS, "residuals")

    cancelled = gate["cancelled_package"]
    if not isinstance(cancelled, list) or any(
            not isinstance(item, dict) or set(item) != {"id", "disposition"}
            for item in cancelled):
        raise ValueError("cancelled package disposition is malformed")
    actual_cancelled = {item["id"]: item["disposition"] for item in cancelled}
    if len(actual_cancelled) != len(cancelled) or actual_cancelled != CANCELLED:
        raise ValueError("cancelled package disposition is incomplete")

    packages = queue.get("packages")
    if not isinstance(packages, list):
        raise ValueError("automation queue has no package array")
    package_by_id = {item.get("id"): item for item in packages if isinstance(item, dict)}
    if len(package_by_id) != len(packages) or GATE_PACKAGE not in package_by_id:
        raise ValueError("automation queue package IDs are invalid")
    for package_id, package in package_by_id.items():
        status = package.get("status")
        if package_id == GATE_PACKAGE:
            continue
        if status == "cancelled" and package_id in CANCELLED:
            continue
        if status != "done":
            raise ValueError(f"predecessor package is not closed: {package_id}")
    gate_status = package_by_id[GATE_PACKAGE].get("status")
    active_id = queue.get("active_id")
    if not ((gate_status == "active" and active_id == GATE_PACKAGE) or
            (gate_status == "done" and active_id == "")):
        raise ValueError("gate package is not the sole active or final done package")

    evidence = gate["evidence"]
    evidence_keys = {"id", "kind", "target", "command"}
    if not isinstance(evidence, list) or any(
            not isinstance(item, dict) or set(item) != evidence_keys
            for item in evidence):
        raise ValueError("evidence matrix is malformed")
    actual = {
        item["id"]: (item["kind"], item["target"], item["command"])
        for item in evidence
    }
    if len(actual) != len(evidence) or actual != EVIDENCE:
        raise ValueError("evidence matrix differs from the frozen milestone")
    package = package_by_id[GATE_PACKAGE]
    frozen_commands = (package.get("targeted_tests", [])[1:] +
                       package.get("package_tests", []) +
                       package.get("runtime_tests", []))
    if frozen_commands != [EVIDENCE[key][2] for key in EVIDENCE]:
        raise ValueError("automation gates differ from the evidence matrix")
    return len(evidence)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gate", type=Path,
                        default=Path("safety/automated_s0_gate.toml"))
    parser.add_argument("--scope", type=Path,
                        default=Path("safety/assurance_scope.toml"))
    parser.add_argument("--queue", type=Path,
                        default=Path("automation/reist-s03b.toml"))
    args = parser.parse_args()
    try:
        count = validate_gate(args.gate, args.scope, args.queue)
    except (OSError, ValueError, tomllib.TOMLDecodeError) as error:
        print(f"AUTOMATED_S0_GATE FAIL: {error}")
        return 1
    print(f"AUTOMATED_S0_GATE PASS evidence={count} status=automated-emulator-complete")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
