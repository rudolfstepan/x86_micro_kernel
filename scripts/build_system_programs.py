#!/usr/bin/env python3
"""Build the standard Ring-3 system programs into MYPR images."""

from __future__ import annotations

import argparse
from pathlib import Path

from build_user_program import ROOT, build, find_zig


PROGRAMS = {
    "HELLO.PRG": ROOT / "examples/userspace/hello.c",
    "SYSINFO.PRG": ROOT / "examples/userspace/sysinfo.c",
    "REPEAT.PRG": ROOT / "examples/userspace/repeat.c",
    "CALC.PRG": ROOT / "examples/userspace/calc.c",
    "DATE.PRG": ROOT / "examples/userspace/date.c",
    "UPTIME.PRG": ROOT / "examples/userspace/uptime.c",
    "MEMINFO.PRG": ROOT / "examples/userspace/meminfo.c",
    "ASCII.PRG": ROOT / "examples/userspace/ascii.c",
    "CAT.PRG": ROOT / "examples/userspace/cat.c",
    "CHKDSK.PRG": ROOT / "examples/userspace/chkdsk.c",
    "FDISK.PRG": ROOT / "examples/userspace/fdisk.c",
    "FORMAT.PRG": ROOT / "examples/userspace/format.c",
    "LS.PRG": ROOT / "examples/userspace/ls.c",
    "SAVE.PRG": ROOT / "examples/userspace/save.c",
    "BASIC.PRG": ROOT / "userspace/bin/basic.c",
    "SPAWN.PRG": ROOT / "examples/userspace/spawn.c",
    "PS.PRG": ROOT / "examples/userspace/ps.c",
    "KILL.PRG": ROOT / "examples/userspace/kill.c",
    "PWD.PRG": ROOT / "examples/userspace/pwd.c",
    "SHELL.PRG": ROOT / "userspace/bin/shell.c",
    "DESKTOP.PRG": ROOT / "examples/userspace/desktop.c",
    "MKDIR.PRG": ROOT / "examples/userspace/mkdir.c",
    "RMDIR.PRG": ROOT / "examples/userspace/rmdir.c",
    "DEL.PRG": ROOT / "examples/userspace/del.c",
    "COPY.PRG": ROOT / "examples/userspace/copy.c",
    "ECHO.PRG": ROOT / "examples/userspace/echo.c",
    "CLS.PRG": ROOT / "examples/userspace/cls.c",
    "DRIVES.PRG": ROOT / "examples/userspace/drives.c",
    "EDIT.PRG": ROOT / "userspace/bin/edit.c",
    "CHILDEX.PRG": ROOT / "examples/userspace/child_exit.c",
    "FAULTDE.PRG": ROOT / "examples/userspace/fault_de.c",
    "FAULTUD.PRG": ROOT / "examples/userspace/fault_ud.c",
    "FAULTPF.PRG": ROOT / "examples/userspace/fault_pf.c",
    "FAULTSTK.PRG": ROOT / "examples/userspace/fault_stack.c",
    "GTEST.PRG": ROOT / "examples/userspace/guest_test.c",
    "REIST.PRG": ROOT / "examples/userspace/reist_probe.c",
    "STORAGE.PRG": ROOT / "examples/userspace/storage_service.c",
    "SLEEPER.PRG": ROOT / "examples/userspace/sleep_child.c",
}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--zig", type=Path)
    parser.add_argument("--incremental", action="store_true")
    args = parser.parse_args()

    zig = find_zig(args.zig)
    output_dir = args.output_dir.resolve()
    for name, source in PROGRAMS.items():
        output = output_dir / name
        before = output.stat().st_mtime_ns if output.is_file() else None
        build([source], output, zig, incremental=args.incremental)
        action = "Reused" if before is not None and output.stat().st_mtime_ns == before else "Built"
        print(f"System program ({action}): {output}")


if __name__ == "__main__":
    main()
