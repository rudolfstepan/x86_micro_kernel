#!/usr/bin/env python3
"""Fail closed when a kernel C object lacks usable header dependencies."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys


def validate(paths: list[Path], root: Path) -> list[str]:
    errors: list[str] = []
    root = root.resolve()
    if not paths:
        return ["no dependency files were provided"]
    for path in paths:
        resolved = (root / path).resolve() if not path.is_absolute() else path.resolve()
        try:
            resolved.relative_to(root)
        except ValueError:
            errors.append(f"dependency path escapes repository: {path}")
            continue
        if not resolved.is_file():
            errors.append(f"missing dependency file: {path}")
            continue
        try:
            text = resolved.read_text(encoding="utf-8").replace("\\\n", " ")
        except OSError as error:
            errors.append(f"cannot read dependency file {path}: {error}")
            continue
        first = next((line.strip() for line in text.splitlines()
                      if line.strip()), "")
        if ":" not in first:
            errors.append(f"invalid dependency target: {path}")
            continue
        target, dependencies = first.split(":", 1)
        expected = str(path.with_suffix(".o")).replace("\\", "/")
        if target.strip().replace("\\", "/") != expected:
            errors.append(f"dependency target mismatch: {path}")
        tokens = dependencies.split()
        if not any(token.replace("\\", "/").endswith(".c")
                   for token in tokens):
            errors.append(f"dependency source missing: {path}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("dependencies", nargs="+", type=Path)
    args = parser.parse_args()
    errors = validate(args.dependencies, Path.cwd())
    for error in errors:
        print(f"build-dependencies: {error}", file=sys.stderr)
    if errors:
        return 1
    print(f"build-dependencies: PASS ({len(args.dependencies)} C objects)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
