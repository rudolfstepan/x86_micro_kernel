#!/usr/bin/env python3
"""Fail-closed structural, scope-coverage and traceability validation."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys
import tomllib
from typing import Any

HAZARD_ID = re.compile(r"^HZ-[A-Z0-9]+-[0-9]{3}$")
COMPONENT_ID = re.compile(r"^CMP-[A-Z0-9-]+$")
FUNCTION_ID = re.compile(r"^EF-[A-Z0-9-]+-[0-9]{3}$")
REQUIREMENT_ID = re.compile(r"^REQ-[A-Z0-9-]+-[0-9]{3}$")
SEVERITIES = {"minor", "major", "critical", "catastrophic"}
CATEGORIES = {"core", "device", "filesystem", "service", "profile"}
COMPONENT_CLASSES = CATEGORIES | {"application"}
HAZARD_STATUSES = {"partially_verified", "verified", "accepted"}


def _load(path: Path, prefix: str, errors: list[str]) -> dict[str, Any] | None:
    try:
        document = tomllib.loads(path.read_text(encoding="utf-8"))
    except (OSError, tomllib.TOMLDecodeError) as error:
        errors.append(f"cannot read {prefix}: {error}")
        return None
    if not isinstance(document, dict):
        errors.append(f"{prefix} must be a TOML document")
        return None
    return document


def _text(table: dict[str, Any], field: str, prefix: str,
          errors: list[str]) -> None:
    if not isinstance(table.get(field), str) or not table[field].strip():
        errors.append(f"{prefix}.{field} must be non-empty")


def _text_list(table: dict[str, Any], field: str, prefix: str,
               errors: list[str], allow_empty: bool = False) -> list[str]:
    values = table.get(field)
    if (not isinstance(values, list) or
            (not allow_empty and not values) or
            any(not isinstance(value, str) or not value.strip()
                for value in values)):
        suffix = "must be a text list" if allow_empty else "must not be empty"
        errors.append(f"{prefix}.{field} {suffix}")
        return []
    return values


def _path(root: Path, relative: object, prefix: str, errors: list[str],
          test_only: bool = False) -> None:
    if (not isinstance(relative, str) or Path(relative).is_absolute() or
            ".." in Path(relative).parts):
        errors.append(f"{prefix} contains an unsafe path")
        return
    resolved = (root / relative).resolve()
    try:
        resolved.relative_to(root.resolve())
    except ValueError:
        errors.append(f"{prefix} escapes repository")
        return
    if not resolved.is_file():
        errors.append(f"{prefix} target does not exist: {relative}")
    if test_only and not relative.startswith("test/"):
        errors.append(f"{prefix} must reference test/: {relative}")


def validate_scope(scope_path: Path, root: Path
                   ) -> tuple[list[str], dict[str, Any] | None]:
    errors: list[str] = []
    scope = _load(scope_path, "assurance scope", errors)
    if scope is None:
        return errors, None
    if scope.get("schema_version") != 1:
        errors.append("assurance_scope.schema_version must be 1")
    if scope.get("status") != "complete":
        errors.append("assurance_scope.status must be complete")
    for field in ("baseline_id", "intended_purpose", "system_boundary",
                  "operating_environment"):
        _text(scope, field, "assurance_scope", errors)
    selected = _text_list(scope, "selected_profiles", "assurance_scope", errors)
    excluded = _text_list(scope, "excluded_profiles", "assurance_scope", errors)
    _text_list(scope, "foreseeable_misuse", "assurance_scope", errors)
    _text_list(scope, "unsupported_claims", "assurance_scope", errors)
    overlap = set(selected) & set(excluded)
    if overlap:
        errors.append(f"assurance profiles selected and excluded: {sorted(overlap)}")

    components = scope.get("component")
    component_ids: set[str] = set()
    if not isinstance(components, list) or not components:
        errors.append("assurance_scope.component must not be empty")
        components = []
    for index, component in enumerate(components, 1):
        prefix = f"component[{index}]"
        if not isinstance(component, dict):
            errors.append(f"{prefix} must be a table")
            continue
        identifier = component.get("id")
        if not isinstance(identifier, str) or not COMPONENT_ID.fullmatch(identifier):
            errors.append(f"{prefix}.id has invalid format")
        elif identifier in component_ids:
            errors.append(f"{prefix}.id is duplicated: {identifier}")
        else:
            component_ids.add(identifier)
        _text(component, "name", prefix, errors)
        if component.get("class") not in COMPONENT_CLASSES:
            errors.append(f"{prefix}.class is invalid")
        for relative in _text_list(component, "code", prefix, errors):
            _path(root, relative, f"{prefix}.code", errors)

    functions = scope.get("essential_function")
    function_ids: set[str] = set()
    if not isinstance(functions, list) or not functions:
        errors.append("assurance_scope.essential_function must not be empty")
        functions = []
    for index, function in enumerate(functions, 1):
        prefix = f"essential_function[{index}]"
        if not isinstance(function, dict):
            errors.append(f"{prefix} must be a table")
            continue
        identifier = function.get("id")
        if not isinstance(identifier, str) or not FUNCTION_ID.fullmatch(identifier):
            errors.append(f"{prefix}.id has invalid format")
        elif identifier in function_ids:
            errors.append(f"{prefix}.id is duplicated: {identifier}")
        else:
            function_ids.add(identifier)
        for field in ("title", "description", "degraded_state", "safe_state"):
            _text(function, field, prefix, errors)
        ftti = function.get("ftti_ms")
        if not isinstance(ftti, int) or isinstance(ftti, bool) or ftti <= 0:
            errors.append(f"{prefix}.ftti_ms must be a positive integer")
        for identifier in _text_list(function, "components", prefix, errors):
            if identifier not in component_ids:
                errors.append(f"{prefix}.components is unknown: {identifier}")

    requirements = scope.get("requirement")
    requirement_ids: set[str] = set()
    if not isinstance(requirements, list) or not requirements:
        errors.append("assurance_scope.requirement must not be empty")
        requirements = []
    for index, requirement in enumerate(requirements, 1):
        prefix = f"requirement[{index}]"
        if not isinstance(requirement, dict):
            errors.append(f"{prefix} must be a table")
            continue
        identifier = requirement.get("id")
        if (not isinstance(identifier, str) or
                not REQUIREMENT_ID.fullmatch(identifier)):
            errors.append(f"{prefix}.id has invalid format")
        elif identifier in requirement_ids:
            errors.append(f"{prefix}.id is duplicated: {identifier}")
        else:
            requirement_ids.add(identifier)
        _text(requirement, "statement", prefix, errors)
        for relative in _text_list(requirement, "design", prefix, errors):
            _path(root, relative, f"{prefix}.design", errors)
    return errors, scope


def validate(register: Path, root: Path,
             scope_path: Path | None = None) -> list[str]:
    errors: list[str] = []
    document = _load(register, "hazard register", errors)
    if document is None:
        return errors
    schema = document.get("schema_version")
    if schema not in {1, 2}:
        errors.append("schema_version must be 1 or 2")
    if document.get("status") not in {"partial", "complete"}:
        errors.append("status must be partial or complete")

    scope: dict[str, Any] | None = None
    component_ids: set[str] = set()
    function_ids: set[str] = set()
    requirement_ids: set[str] = set()
    if schema == 2:
        relative_scope = document.get("scope")
        if scope_path is None:
            if (not isinstance(relative_scope, str) or
                    Path(relative_scope).is_absolute() or
                    ".." in Path(relative_scope).parts):
                errors.append("scope contains an unsafe path")
            else:
                scope_path = (root / relative_scope).resolve()
        if scope_path is not None:
            try:
                scope_path.resolve().relative_to(root.resolve())
            except ValueError:
                errors.append("scope escapes repository")
            else:
                scope_errors, scope = validate_scope(scope_path.resolve(), root)
                errors.extend(scope_errors)
        if scope is not None:
            if document.get("baseline") != scope.get("baseline_id"):
                errors.append("hazard baseline does not match assurance scope")
            component_ids = {item["id"] for item in scope.get("component", [])
                             if isinstance(item, dict) and isinstance(item.get("id"), str)}
            function_ids = {item["id"] for item in scope.get("essential_function", [])
                            if isinstance(item, dict) and isinstance(item.get("id"), str)}
            requirement_ids = {item["id"] for item in scope.get("requirement", [])
                               if isinstance(item, dict) and isinstance(item.get("id"), str)}

    hazards = document.get("hazard")
    if not isinstance(hazards, list) or not hazards:
        return errors + ["at least one [[hazard]] is required"]
    seen: set[str] = set()
    covered_components: set[str] = set()
    covered_functions: set[str] = set()
    covered_requirements: set[str] = set()
    categories: set[str] = set()
    for index, hazard in enumerate(hazards, 1):
        prefix = f"hazard[{index}]"
        if not isinstance(hazard, dict):
            errors.append(f"{prefix} must be a table")
            continue
        identifier = hazard.get("id")
        if not isinstance(identifier, str) or not HAZARD_ID.fullmatch(identifier):
            errors.append(f"{prefix}.id has invalid format")
        elif identifier in seen:
            errors.append(f"{prefix}.id is duplicated: {identifier}")
        else:
            seen.add(identifier)
        for field in ("title", "safe_state", "residual_risk"):
            _text(hazard, field, prefix, errors)
        if hazard.get("severity") not in SEVERITIES:
            errors.append(f"{prefix}.severity is invalid")
        ftti = hazard.get("ftti_ms")
        if not isinstance(ftti, int) or isinstance(ftti, bool) or ftti <= 0:
            errors.append(f"{prefix}.ftti_ms must be a positive integer")
        for field in ("controls", "verification"):
            for relative in _text_list(hazard, field, prefix, errors):
                _path(root, relative, f"{prefix}.{field}", errors,
                      test_only=field == "verification")
        if schema != 2:
            continue
        for field in ("degraded_state", "ftti_rationale", "owner"):
            _text(hazard, field, prefix, errors)
        for field in ("causes", "effects", "phases", "acceptance_criteria",
                      "assumptions"):
            _text_list(hazard, field, prefix, errors)
        category = hazard.get("category")
        if category not in CATEGORIES:
            errors.append(f"{prefix}.category is invalid")
        else:
            categories.add(category)
        if hazard.get("status") not in HAZARD_STATUSES:
            errors.append(f"{prefix}.status is invalid")
        for field, known, covered in (
            ("components", component_ids, covered_components),
            ("essential_functions", function_ids, covered_functions),
            ("requirements", requirement_ids, covered_requirements),
        ):
            for reference in _text_list(hazard, field, prefix, errors):
                if reference not in known:
                    errors.append(f"{prefix}.{field} is unknown: {reference}")
                else:
                    covered.add(reference)
        for relative in _text_list(hazard, "design", prefix, errors):
            _path(root, relative, f"{prefix}.design", errors)

    if schema == 2 and document.get("status") == "complete" and scope is not None:
        for label, expected, covered in (
            ("components", component_ids, covered_components),
            ("essential functions", function_ids, covered_functions),
            ("requirements", requirement_ids, covered_requirements),
        ):
            missing = sorted(expected - covered)
            if missing:
                errors.append(f"complete register has uncovered {label}: {missing}")
        missing_categories = sorted(CATEGORIES - categories)
        if missing_categories:
            errors.append(f"complete register has no hazards for categories: {missing_categories}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("register", nargs="?", default="safety/hazards.toml")
    parser.add_argument("--scope", default=None)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    scope = (root / args.scope).resolve() if args.scope else None
    errors = validate((root / args.register).resolve(), root, scope)
    for error in errors:
        print(f"hazard-register: {error}", file=sys.stderr)
    if errors:
        return 1
    print("hazard-register: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
