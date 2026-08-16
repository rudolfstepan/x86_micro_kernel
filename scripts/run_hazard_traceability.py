#!/usr/bin/env python3
"""Run each hazard verification and emit a content-bound JSON baseline."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tomllib
from typing import Callable

RunTest = Callable[[Path], tuple[bool, str]]


def file_sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def default_runner(path: Path) -> tuple[bool, str]:
    pattern = path.name
    result = subprocess.run(
        [sys.executable, "-m", "unittest", "discover", "-s", "test",
         "-p", pattern, "-q"], cwd=path.parents[1],
        capture_output=True, text=True, timeout=180,
    )
    lines = (result.stdout + result.stderr).strip().splitlines()
    return result.returncode == 0, lines[-1] if lines else "no output"


def build_baseline(register: Path, root: Path, runner: RunTest) -> dict:
    document = tomllib.loads(register.read_text(encoding="utf-8"))
    hazards = document.get("hazard", [])
    tests: dict[str, dict] = {}
    for hazard in hazards:
        for relative in hazard.get("verification", []):
            if relative in tests:
                continue
            path = (root / relative).resolve()
            path.relative_to(root)
            passed, detail = runner(path)
            tests[relative] = {"sha256": file_sha256(path),
                               "passed": passed, "detail": detail[:240]}
    traced = []
    for hazard in hazards:
        verification = hazard.get("verification", [])
        traced.append({"id": hazard.get("id"), "verification": verification,
                       "passed": bool(verification) and all(
                           tests[path]["passed"] for path in verification)})
    return {"schema_version": 1,
            "register": register.relative_to(root).as_posix(),
            "register_sha256": file_sha256(register),
            "passed": bool(traced) and all(item["passed"] for item in traced),
            "hazards": traced, "tests": tests}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--register", default="safety/hazards.toml")
    parser.add_argument("--output",
                        default="build/codex-agent/hazard-traceability.json")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    register, output = (root / args.register).resolve(), (root / args.output).resolve()
    try:
        register.relative_to(root)
        output.relative_to(root)
        baseline = build_baseline(register, root, default_runner)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(baseline, indent=2, sort_keys=True) + "\n",
                          encoding="utf-8")
    except (OSError, ValueError, tomllib.TOMLDecodeError,
            subprocess.SubprocessError) as error:
        print(f"hazard-traceability: {error}", file=sys.stderr)
        return 1
    print(f"hazard-traceability: {'PASS' if baseline['passed'] else 'FAIL'} "
          f"hazards={len(baseline['hazards'])} tests={len(baseline['tests'])} "
          f"output={output}")
    return 0 if baseline["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
