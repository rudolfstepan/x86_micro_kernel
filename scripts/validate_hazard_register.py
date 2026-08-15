#!/usr/bin/env python3
"""Fail-closed structural and traceability validation for REIST hazards."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys
import tomllib

ID_PATTERN = re.compile(r"^HZ-[A-Z0-9]+-[0-9]{3}$")
SEVERITIES = {"minor", "major", "critical", "catastrophic"}


def validate(register: Path, root: Path) -> list[str]:
    errors: list[str] = []
    try:
        document = tomllib.loads(register.read_text(encoding="utf-8"))
    except (OSError, tomllib.TOMLDecodeError) as error:
        return [f"cannot read hazard register: {error}"]
    if document.get("schema_version") != 1:
        errors.append("schema_version must be 1")
    if document.get("status") not in {"partial", "complete"}:
        errors.append("status must be partial or complete")
    hazards = document.get("hazard")
    if not isinstance(hazards, list) or not hazards:
        return errors + ["at least one [[hazard]] is required"]
    seen: set[str] = set()
    required_text = ("title", "safe_state", "residual_risk")
    for index, hazard in enumerate(hazards, 1):
        prefix = f"hazard[{index}]"
        if not isinstance(hazard, dict):
            errors.append(f"{prefix} must be a table")
            continue
        hazard_id = hazard.get("id")
        if not isinstance(hazard_id, str) or not ID_PATTERN.fullmatch(hazard_id):
            errors.append(f"{prefix}.id has invalid format")
        elif hazard_id in seen:
            errors.append(f"{prefix}.id is duplicated: {hazard_id}")
        else:
            seen.add(hazard_id)
        for field in required_text:
            if not isinstance(hazard.get(field), str) or not hazard[field].strip():
                errors.append(f"{prefix}.{field} must be non-empty")
        if hazard.get("severity") not in SEVERITIES:
            errors.append(f"{prefix}.severity is invalid")
        ftti = hazard.get("ftti_ms")
        if not isinstance(ftti, int) or isinstance(ftti, bool) or ftti <= 0:
            errors.append(f"{prefix}.ftti_ms must be a positive integer")
        for field in ("controls", "verification"):
            paths = hazard.get(field)
            if not isinstance(paths, list) or not paths:
                errors.append(f"{prefix}.{field} must not be empty")
                continue
            for path in paths:
                if not isinstance(path, str) or Path(path).is_absolute() or ".." in Path(path).parts:
                    errors.append(f"{prefix}.{field} contains an unsafe path")
                    continue
                resolved = (root / path).resolve()
                try:
                    resolved.relative_to(root.resolve())
                except ValueError:
                    errors.append(f"{prefix}.{field} escapes repository")
                    continue
                if not resolved.is_file():
                    errors.append(f"{prefix}.{field} target does not exist: {path}")
                if field == "verification" and not path.startswith("test/"):
                    errors.append(f"{prefix}.verification must reference test/: {path}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("register", nargs="?", default="safety/hazards.toml")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    errors = validate((root / args.register).resolve(), root)
    for error in errors:
        print(f"hazard-register: {error}", file=sys.stderr)
    if errors:
        return 1
    print("hazard-register: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
