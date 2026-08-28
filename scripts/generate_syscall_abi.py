#!/usr/bin/env python3
"""Generate or verify legacy syscall-name projections from the common ABI."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "include/reist/abi/syscall.h"
KERNEL_HEADER = ROOT / "lib/libc/stdlib.h"
SDK_HEADER = ROOT / "userspace/sdk/include/x86os.h"
SDK_COMMON_HEADER = ROOT / "userspace/sdk/include/reist/abi/syscall.h"
COUNT = 126
ENTRY = re.compile(
    r"^\s*X\(([A-Z][A-Z0-9_]*), ([A-Z][A-Z0-9_]*), ([0-9]+)U\)"
    r"\s*(?:\\)?\s*$",
    re.MULTILINE,
)


def entries() -> list[tuple[str, str, int]]:
    parsed = [(kernel, sdk, int(number))
              for kernel, sdk, number in ENTRY.findall(
                  SOURCE.read_text(encoding="utf-8"))]
    if len(parsed) != COUNT:
        raise ValueError(f"expected {COUNT} syscalls, found {len(parsed)}")
    numbers = [item[2] for item in parsed]
    if numbers != list(range(COUNT)):
        raise ValueError(
            f"syscall indexes must be contiguous 0..{COUNT - 1}")
    kernel_names = [item[0] for item in parsed]
    sdk_names = [item[1] for item in parsed]
    if len(set(kernel_names)) != COUNT or len(set(sdk_names)) != COUNT:
        raise ValueError("syscall names must be unique in each namespace")
    return parsed


def kernel_projection(items: list[tuple[str, str, int]]) -> str:
    lines = ["/* BEGIN GENERATED REIST SYSCALLS - DO NOT EDIT */"]
    for kernel, _sdk, number in items:
        suffix = " /* Reserved: always rejected by the kernel. */" \
            if number == 8 else ""
        lines.append(f"#define SYS_{kernel} {number}{suffix}")
    lines.append("/* END GENERATED REIST SYSCALLS */")
    return "\n".join(lines)


def sdk_projection(items: list[tuple[str, str, int]]) -> str:
    lines = ["/* BEGIN GENERATED REIST SYSCALLS - DO NOT EDIT */", "enum {"]
    for _kernel, sdk, number in items:
        lines.append(f"    X86OS_SYS_{sdk} = {number},")
    lines[-1] = lines[-1].removesuffix(",")
    lines.extend(["};", "/* END GENERATED REIST SYSCALLS */"])
    return "\n".join(lines)


def replace_region(path: Path, projection: str, write: bool) -> bool:
    text = path.read_text(encoding="utf-8")
    pattern = re.compile(
        r"/\* BEGIN GENERATED REIST SYSCALLS - DO NOT EDIT \*/.*?"
        r"/\* END GENERATED REIST SYSCALLS \*/",
        re.DOTALL,
    )
    updated, replacements = pattern.subn(projection, text)
    if replacements != 1:
        raise ValueError(f"{path}: expected exactly one generated region")
    if updated == text:
        return True
    if write:
        path.write_text(updated, encoding="utf-8", newline="\n")
        return True
    print(f"generated syscall ABI drift: {path.relative_to(ROOT)}",
          file=sys.stderr)
    return False


def sync_installed_header(write: bool) -> bool:
    authoritative = SOURCE.read_bytes()
    installed = (SDK_COMMON_HEADER.read_bytes()
                 if SDK_COMMON_HEADER.is_file() else None)
    if installed == authoritative:
        return True
    if write:
        SDK_COMMON_HEADER.parent.mkdir(parents=True, exist_ok=True)
        SDK_COMMON_HEADER.write_bytes(authoritative)
        return True
    print("generated syscall ABI drift: "
          f"{SDK_COMMON_HEADER.relative_to(ROOT)}", file=sys.stderr)
    return False


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--write", action="store_true")
    args = parser.parse_args()
    try:
        parsed = entries()
        valid = replace_region(KERNEL_HEADER, kernel_projection(parsed),
                               args.write)
        valid = replace_region(SDK_HEADER, sdk_projection(parsed),
                               args.write) and valid
        valid = sync_installed_header(args.write) and valid
    except (OSError, ValueError) as error:
        print(f"syscall ABI generation failed: {error}", file=sys.stderr)
        return 1
    if not valid:
        return 1
    print(f"SYSCALL_ABI_{'WRITE' if args.write else 'CHECK'}_OK count={COUNT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
