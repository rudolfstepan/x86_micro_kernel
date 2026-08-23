#!/usr/bin/env python3
"""Execute hazard evidence and emit a content-bound assurance baseline."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tomllib
from typing import Callable

_VALIDATOR_SPEC = importlib.util.spec_from_file_location(
    "reist_hazard_validator", Path(__file__).with_name("validate_hazard_register.py"))
if _VALIDATOR_SPEC is None or _VALIDATOR_SPEC.loader is None:
    raise RuntimeError("hazard validator cannot be loaded")
VALIDATOR = importlib.util.module_from_spec(_VALIDATOR_SPEC)
_VALIDATOR_SPEC.loader.exec_module(VALIDATOR)

RunTest = Callable[[Path], tuple[bool, str]]


def file_sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def default_runner(path: Path) -> tuple[bool, str]:
    source = path.read_text(encoding="utf-8")
    if "unittest" in source:
        command = [sys.executable, "-m", "unittest", "discover", "-s", "test",
                   "-p", path.name, "-q"]
    elif 'if __name__ == "__main__"' in source:
        command = [sys.executable, str(path)]
    else:
        return False, "no executable unittest or __main__ harness"
    result = subprocess.run(
        command,
        cwd=path.parents[1], capture_output=True, text=True, timeout=180,
    )
    lines = (result.stdout + result.stderr).strip().splitlines()
    return result.returncode == 0, lines[-1] if lines else "no output"


def _add_artifact(artifacts: dict[str, dict[str, str]], root: Path,
                  relative: str, kind: str) -> None:
    path = (root / relative).resolve()
    if not path.is_file():
        return
    current = artifacts.get(relative)
    if current is None:
        artifacts[relative] = {"kind": kind, "sha256": file_sha256(path)}
    elif kind not in current["kind"].split("+"):
        current["kind"] += "+" + kind


def build_baseline(register: Path, root: Path, runner: RunTest,
                   scope_path: Path | None = None) -> dict:
    document = tomllib.loads(register.read_text(encoding="utf-8"))
    if scope_path is None and isinstance(document.get("scope"), str):
        scope_path = (root / document["scope"]).resolve()
    validation_errors = VALIDATOR.validate(register, root, scope_path)
    scope = ({"component": [], "essential_function": [], "requirement": []}
             if scope_path is None or not scope_path.is_file()
             else tomllib.loads(scope_path.read_text(encoding="utf-8")))
    artifacts: dict[str, dict[str, str]] = {}
    tests: dict[str, dict[str, object]] = {}

    if scope_path is not None and scope_path.is_file():
        _add_artifact(artifacts, root,
                      scope_path.relative_to(root).as_posix(), "scope")
    _add_artifact(artifacts, root, register.relative_to(root).as_posix(),
                  "hazard-register")
    for requirement in scope.get("requirement", []):
        for relative in requirement.get("design", []):
            _add_artifact(artifacts, root, relative, "requirement-design")
    for component in scope.get("component", []):
        for relative in component.get("code", []):
            _add_artifact(artifacts, root, relative, "scope-code")
    for hazard in document.get("hazard", []):
        for relative in hazard.get("design", []):
            _add_artifact(artifacts, root, relative, "hazard-design")
        for relative in hazard.get("controls", []):
            _add_artifact(artifacts, root, relative, "control")
        for relative in hazard.get("verification", []):
            _add_artifact(artifacts, root, relative, "verification")
            if relative in tests or not (root / relative).is_file():
                continue
            passed, detail = runner((root / relative).resolve())
            tests[relative] = {
                "sha256": file_sha256(root / relative),
                "passed": passed,
                "detail": detail[:240],
            }

    requirements = [
        {"id": item.get("id"), "design": item.get("design", []),
         "passed": bool(item.get("design")) and all(
             relative in artifacts for relative in item.get("design", []))}
        for item in scope.get("requirement", [])
    ]
    hazards = []
    for item in document.get("hazard", []):
        verification = item.get("verification", [])
        references = (item.get("design", []) + item.get("controls", []) +
                      verification)
        hazards.append({
            "id": item.get("id"),
            "components": item.get("components", []),
            "requirements": item.get("requirements", []),
            "design": item.get("design", []),
            "controls": item.get("controls", []),
            "verification": verification,
            "passed": bool(verification) and all(
                relative in artifacts for relative in references) and all(
                tests.get(relative, {}).get("passed") is True
                for relative in verification),
        })
    passed = (not validation_errors and bool(hazards) and
              all(item["passed"] for item in hazards) and
              bool(requirements) and all(item["passed"] for item in requirements))
    return {
        "schema_version": 2,
        "baseline": document.get("baseline"),
        "register": register.relative_to(root).as_posix(),
        "register_sha256": file_sha256(register),
        "scope": (scope_path.relative_to(root).as_posix()
                  if scope_path is not None and scope_path.is_file() else None),
        "scope_sha256": (file_sha256(scope_path)
                         if scope_path is not None and scope_path.is_file()
                         else None),
        "validation_errors": validation_errors,
        "passed": passed,
        "requirements": requirements,
        "hazards": hazards,
        "artifacts": dict(sorted(artifacts.items())),
        "tests": dict(sorted(tests.items())),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--register", default="safety/hazards.toml")
    parser.add_argument("--scope", default=None)
    parser.add_argument("--output",
                        default="build/codex-agent/hazard-traceability.json")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    register = (root / args.register).resolve()
    scope = (root / args.scope).resolve() if args.scope else None
    output = (root / args.output).resolve()
    try:
        register.relative_to(root)
        if scope is not None:
            scope.relative_to(root)
        output.relative_to(root)
        baseline = build_baseline(register, root, default_runner, scope)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(baseline, indent=2, sort_keys=True) + "\n",
                          encoding="utf-8")
    except (OSError, ValueError, tomllib.TOMLDecodeError,
            subprocess.SubprocessError) as error:
        print(f"hazard-traceability: {error}", file=sys.stderr)
        return 1
    print(f"hazard-traceability: {'PASS' if baseline['passed'] else 'FAIL'} "
          f"hazards={len(baseline['hazards'])} "
          f"requirements={len(baseline['requirements'])} "
          f"tests={len(baseline['tests'])} artifacts={len(baseline['artifacts'])} "
          f"output={output}")
    if baseline["validation_errors"]:
        for error in baseline["validation_errors"]:
            print(f"hazard-traceability: {error}", file=sys.stderr)
    return 0 if baseline["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
