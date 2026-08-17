#!/usr/bin/env python3
"""Build the standard Ring-3 system programs into MYPR images."""

from __future__ import annotations

import argparse
from pathlib import Path

from build_user_program import ROOT, build, find_zig


PROGRAMS = {
    "HELLO.PRG": ROOT / "userspace/programs/hello.c",
    "SYSINFO.PRG": ROOT / "userspace/programs/sysinfo.c",
    "REPEAT.PRG": ROOT / "userspace/programs/repeat.c",
    "CALC.PRG": ROOT / "userspace/programs/calc.c",
    "DATE.PRG": ROOT / "userspace/programs/date.c",
    "UPTIME.PRG": ROOT / "userspace/programs/uptime.c",
    "MEMINFO.PRG": ROOT / "userspace/programs/meminfo.c",
    "ASCII.PRG": ROOT / "userspace/programs/ascii.c",
    "CAT.PRG": ROOT / "userspace/programs/cat.c",
    "CHKDSK.PRG": ROOT / "userspace/programs/chkdsk.c",
    "FDISK.PRG": ROOT / "userspace/programs/fdisk.c",
    "FORMAT.PRG": ROOT / "userspace/programs/format.c",
    "LS.PRG": ROOT / "userspace/programs/ls.c",
    "SAVE.PRG": ROOT / "userspace/programs/save.c",
    "BASIC.PRG": ROOT / "userspace/bin/basic.c",
    "SPAWN.PRG": ROOT / "userspace/programs/spawn.c",
    "PS.PRG": ROOT / "userspace/programs/ps.c",
    "KILL.PRG": ROOT / "userspace/programs/kill.c",
    "PWD.PRG": ROOT / "userspace/programs/pwd.c",
    "SHELL.PRG": ROOT / "userspace/bin/shell.c",
    "DESKTOP.PRG": ROOT / "userspace/programs/desktop.c",
    "MKDIR.PRG": ROOT / "userspace/programs/mkdir.c",
    "RMDIR.PRG": ROOT / "userspace/programs/rmdir.c",
    "DEL.PRG": ROOT / "userspace/programs/del.c",
    "COPY.PRG": ROOT / "userspace/programs/copy.c",
    "ECHO.PRG": ROOT / "userspace/programs/echo.c",
    "CLS.PRG": ROOT / "userspace/programs/cls.c",
    "DRIVES.PRG": ROOT / "userspace/programs/drives.c",
    "DEVCTL.PRG": ROOT / "userspace/programs/devctl.c",
    "MOUNT.PRG": ROOT / "userspace/programs/mount.c",
    "UMOUNT.PRG": ROOT / "userspace/programs/umount.c",
    "SVCCTL.PRG": ROOT / "userspace/programs/svcctl.c",
    "EDIT.PRG": ROOT / "userspace/bin/edit.c",
    "CHILDEX.PRG": ROOT / "userspace/programs/child_exit.c",
    "FAULTDE.PRG": ROOT / "userspace/programs/fault_de.c",
    "FAULTUD.PRG": ROOT / "userspace/programs/fault_ud.c",
    "FAULTPF.PRG": ROOT / "userspace/programs/fault_pf.c",
    "FAULTSTK.PRG": ROOT / "userspace/programs/fault_stack.c",
    "GTEST.PRG": ROOT / "userspace/programs/guest_test.c",
    "REIST.PRG": ROOT / "userspace/programs/reist_probe.c",
    "STORAGE.PRG": ROOT / "userspace/programs/storage_service.c",
    "SLEEPER.PRG": ROOT / "userspace/programs/sleep_child.c",
    "SATAWR.PRG": ROOT / "userspace/programs/sata_write_test.c",
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
