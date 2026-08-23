#!/usr/bin/env python3
"""Validate bounded empirical timing-regression budgets."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BUDGET = ROOT / "safety" / "wcet_budgets.json"
MAX_PATH_NS = 10_000_000


def validate(path: Path) -> list[str]:
    errors: list[str] = []
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        return [f"cannot read timing budget {path}: {error}"]
    if document.get("version") != 1:
        errors.append("timing budget version must be 1")
    if document.get("kind") != "empirical-emulator-regression":
        errors.append("timing budget kind must remain empirical")
    samples = document.get("minimum_samples")
    if not isinstance(samples, int) or isinstance(samples, bool) or not 64 <= samples <= 4096:
        errors.append("minimum_samples must be an integer in [64, 4096]")
    if document.get("maximum_clock_anomalies") != 0:
        errors.append("clock anomalies must fail closed at zero")
    platforms = document.get("platforms")
    if not isinstance(platforms, dict) or set(platforms) != {"qemu", "vmware"}:
        errors.append("platforms must be exactly qemu and vmware")
    else:
        required = {"scheduler_decision_max_ns", "int80_probe_max_ns"}
        for name, budget in platforms.items():
            if not isinstance(budget, dict) or set(budget) != required:
                errors.append(f"{name} timing fields are incomplete")
                continue
            for field in required:
                value = budget[field]
                if (not isinstance(value, int) or isinstance(value, bool) or
                        not 1 <= value <= MAX_PATH_NS):
                    errors.append(f"{name}.{field} must be in [1, {MAX_PATH_NS}]")
    claims = document.get("claims")
    if claims != {"physical_hardware_wcet": False, "certification": False}:
        errors.append("emulator budgets must not claim hardware WCET or certification")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--file", type=Path, default=DEFAULT_BUDGET)
    arguments = parser.parse_args()
    errors = validate(arguments.file.resolve())
    if errors:
        for error in errors:
            print(f"WCET BUDGET FAIL: {error}")
        return 1
    print(f"WCET BUDGET PASS: {arguments.file}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
