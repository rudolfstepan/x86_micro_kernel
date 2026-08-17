#!/usr/bin/env python3
"""Exercise bounded storage administration and resident tools in QEMU."""

from __future__ import annotations

import argparse
import hashlib
import queue
import re
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_qemu_fdd_hotplug as fdd_hotplug
import run_qemu_sata_hotplug as sata_hotplug
import run_qemu_smoke as smoke


RESOURCE_PATTERN = re.compile(
    r"ADMIN RESOURCE (\d+) name=[^\r\n]* type=(FDD|SATA|PART|ATA) "
    r"state=([^ ]+) mount=([^ \r\n]+)([^\r\n]*)"
)
CACHE_EXEC = "REIST_RESCUE CACHE_EXEC /DEVCTL.PRG"


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def qemu_command(qemu: Path, disk: Path, floppy: Path,
                 qmp_port: int) -> list[str]:
    return [
        str(qemu), "-accel", "tcg", "-machine", "pc", "-nodefaults",
        "-m", "512M", "-boot", "c",
        "-device", "ich9-ahci,id=reistahci",
        "-blockdev", f"driver=file,filename={disk},node-name=reistfile",
        "-blockdev", "driver=raw,file=reistfile,node-name=reistdisk",
        "-device", ("ide-hd,drive=reistdisk,bus=reistahci.0,"
                    "id=reistsystemdisk,bootindex=1,werror=report,rerror=report"),
        "-drive", (f"file={floppy},format=raw,if=floppy,index=0,"
                   "media=disk,id=reistadminfloppy"),
        "-display", "none", "-monitor", "none", "-serial", "mon:stdio",
        "-qmp", f"tcp:127.0.0.1:{qmp_port},server=on,wait=off",
        "-no-reboot", "-no-shutdown",
    ]


def wait_marker(process: subprocess.Popen[str], chunks: queue.Queue[str],
                transcript: list[str], finished: threading.Event,
                marker: str, deadline: float, after: int = -1
                ) -> tuple[str | None, int]:
    return smoke.wait_for_line(process, chunks, transcript, finished,
                               marker, deadline, after=after)


def send_and_wait(process: subprocess.Popen[str], chunks: queue.Queue[str],
                  transcript: list[str], finished: threading.Event,
                  command: str, marker: str, deadline: float,
                  after: int = -1) -> tuple[str | None, int]:
    smoke.inject_ps2_command(process, command)
    error, marker_position = wait_marker(
        process, chunks, transcript, finished, marker, deadline, after)
    if error is not None:
        return error, marker_position
    return wait_marker(process, chunks, transcript, finished,
                       smoke.SHELL_PROMPT, deadline, after=marker_position)


def discover_resources(output: str) -> tuple[int, int]:
    root = -1
    floppy = -1
    for match in RESOURCE_PATTERN.finditer(output):
        resource = int(match.group(1))
        drive_type = match.group(2)
        suffix = match.group(5)
        if "ROOT_PROTECTED" in suffix and root < 0:
            root = resource
        if drive_type == "FDD" and floppy < 0:
            floppy = resource
    if root < 0 or floppy < 0:
        raise RuntimeError("admin inventory did not identify root and FDD")
    return root, floppy


def run(qemu: Path, reference_image: Path, disk: Path, floppy: Path,
        timeout: float, log: Path) -> int:
    if reference_image in (disk, floppy) or disk == floppy:
        raise ValueError("all disposable images must differ from reference")
    reference_digest = file_sha256(reference_image)
    disk.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(reference_image, disk)
    fdd_hotplug.create_test_floppy(floppy)
    qmp_port = sata_hotplug.reserve_port()
    process = subprocess.Popen(
        qemu_command(qemu, disk, floppy, qmp_port), stdin=subprocess.PIPE,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
        encoding="utf-8", errors="replace", bufsize=0,
    )
    assert process.stdout is not None
    chunks: queue.Queue[str] = queue.Queue()
    transcript: list[str] = []
    finished = threading.Event()
    reader = threading.Thread(target=smoke.reader,
                              args=(process.stdout, chunks, finished),
                              daemon=True)
    reader.start()
    deadline = time.monotonic() + timeout
    qmp: sata_hotplug.QmpClient | None = None
    error: str | None = None
    try:
        qmp = sata_hotplug.QmpClient(qmp_port, deadline)
        error, shell_position = wait_marker(
            process, chunks, transcript, finished, smoke.SHELL_PROMPT,
            deadline)
        if error is None:
            smoke.inject_ps2_command(process, "devctl list")
            error, list_position = wait_marker(
                process, chunks, transcript, finished, smoke.SHELL_PROMPT,
                deadline, after=shell_position)
        if error is None:
            smoke.drain(chunks, transcript)
            root, auxiliary = discover_resources("".join(transcript))
            commands = [
                (f"umount {auxiliary}",
                 f"ADMIN UMOUNT_OK resource={auxiliary}"),
                (f"mount {auxiliary} fat12 /mnt/admin",
                 f"ADMIN MOUNT_OK resource={auxiliary} path=/mnt/admin"),
                (f"devctl down {auxiliary}",
                 f"ADMIN DEVICE_DOWN_OK resource={auxiliary}"),
                (f"devctl status {auxiliary}",
                 f"ADMIN STATUS_STATE resource={auxiliary} state=ADMIN_DOWN"),
                (f"devctl up {auxiliary}",
                 f"ADMIN DEVICE_UP_OK resource={auxiliary}"),
                (f"devctl status {auxiliary}",
                 f"ADMIN STATUS_MOUNT resource={auxiliary} path=/mnt/admin"),
                (f"devctl down {root}", "ADMIN ROOT_PROTECTED"),
            ]
            position = list_position
            for command, marker in commands:
                error, position = send_and_wait(
                    process, chunks, transcript, finished, command, marker,
                    deadline, after=position)
                if error is not None:
                    break
        if error is None:
            qmp.execute("blockdev-set-active", {
                "node-name": "reistdisk", "active": False,
            }, deadline)
            smoke.inject_ps2_command(process, f"devctl status {auxiliary}")
            error, cache_position = wait_marker(
                process, chunks, transcript, finished, CACHE_EXEC, deadline,
                after=position)
            if error is None:
                error, position = wait_marker(
                    process, chunks, transcript, finished,
                    f"ADMIN STATUS_STATE resource={auxiliary} state=ONLINE",
                    deadline,
                    after=cache_position)
            qmp.execute("blockdev-set-active", {
                "node-name": "reistdisk", "active": True,
            }, deadline)
        if error is None:
            error, _ = wait_marker(process, chunks, transcript, finished,
                                   smoke.SHELL_PROMPT, deadline,
                                   after=position)
    except (ConnectionError, OSError, RuntimeError, TimeoutError,
            ValueError) as caught:
        error = str(caught)
    finally:
        if qmp is not None:
            qmp.close()
        smoke.stop_process(process)
        finished.wait(timeout=1.0)
        smoke.drain(chunks, transcript)
        reader.join(timeout=1.0)
        log.parent.mkdir(parents=True, exist_ok=True)
        log.write_text("".join(transcript), encoding="utf-8")
    if file_sha256(reference_image) != reference_digest:
        error = "reference image changed"
    if error is not None:
        print(f"ADMIN MAINTENANCE FAIL: {error}; log={log}")
        return 1
    print(f"ADMIN MAINTENANCE PASS log={log}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--disk", type=Path,
                        default=Path("build/admin-maintenance.img"))
    parser.add_argument("--floppy", type=Path,
                        default=Path("build/admin-maintenance-fdd.img"))
    parser.add_argument("--log", type=Path,
                        default=Path("build/test-results/admin-maintenance.log"))
    parser.add_argument("--timeout", type=float, default=90.0)
    args = parser.parse_args()
    if not args.qemu.is_file() or not args.image.is_file() or args.timeout <= 0:
        parser.error("qemu/image must exist and timeout must be positive")
    return run(args.qemu.resolve(), args.image.resolve(), args.disk.resolve(),
               args.floppy.resolve(), args.timeout, args.log.resolve())


if __name__ == "__main__":
    raise SystemExit(main())
