#!/usr/bin/env python3
"""Deactivate and reactivate a disposable AHCI backend during a guest write."""

from __future__ import annotations

import argparse
import hashlib
import json
import queue
import shutil
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_qemu_smoke as smoke


ACTIVE = "SATA_WRITE ACTIVE"
IO_ERROR = "SATA_WRITE IO_ERROR_DETECTED"
REINTEGRATED = "REIST_STORAGE RESOURCE_REINTEGRATED_RW 0"
INDEPENDENT_PROGRESS = "SATA_WRITE INDEPENDENT_PROGRESS_OK"
RECOVERY_RW = "SATA_WRITE RECOVERY_RW_OK"
TEST_OK = "SATA_WRITE TEST_OK"
EXHAUSTED = "RECOVERY_EXHAUSTED"


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def reserve_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return int(listener.getsockname()[1])


class QmpClient:
    def __init__(self, port: int, deadline: float):
        self.connection = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        while True:
            try:
                self.connection.connect(("127.0.0.1", port))
                break
            except OSError:
                if time.monotonic() >= deadline:
                    self.connection.close()
                    raise TimeoutError("timeout connecting to QMP")
                time.sleep(0.05)
        self.stream = self.connection.makefile("rwb", buffering=0)
        if "QMP" not in self._read(deadline):
            raise RuntimeError("invalid QMP greeting")
        self.execute("qmp_capabilities", None, deadline)

    def _read(self, deadline: float) -> dict:
        self.connection.settimeout(max(0.05, deadline - time.monotonic()))
        line = self.stream.readline()
        if not line:
            raise ConnectionError("QMP connection closed")
        return json.loads(line.decode("utf-8"))

    def execute(self, command: str, arguments: dict | None,
                deadline: float) -> object:
        request: dict[str, object] = {"execute": command, "id": command}
        if arguments is not None:
            request["arguments"] = arguments
        self.stream.write(json.dumps(request).encode("utf-8") + b"\r\n")
        while True:
            reply = self._read(deadline)
            if "event" in reply:
                continue
            if reply.get("id") != command:
                continue
            if "error" in reply:
                raise RuntimeError(f"QMP {command} failed: {reply['error']}")
            return reply.get("return")

    def close(self) -> None:
        try:
            self.stream.close()
        finally:
            self.connection.close()


def qemu_command(qemu: Path, disk: Path, qmp_port: int) -> list[str]:
    return [
        str(qemu), "-accel", "tcg", "-machine", "pc", "-nodefaults",
        "-m", "1024M", "-boot", "c",
        "-device", "ich9-ahci,id=reistahci",
        "-blockdev", (f"driver=file,filename={disk},"
                      "node-name=reistfile"),
        "-blockdev", "driver=raw,file=reistfile,node-name=reistdisk",
        "-device", ("ide-hd,drive=reistdisk,bus=reistahci.0,"
                    "id=reistsystemdisk,bootindex=1,"
                    "werror=report,rerror=report"),
        "-display", "none", "-monitor", "none", "-serial", "mon:stdio",
        "-qmp", f"tcp:127.0.0.1:{qmp_port},server=on,wait=off",
        "-no-reboot", "-no-shutdown",
    ]


def wait_marker(process: subprocess.Popen[str], chunks: queue.Queue[str],
                transcript: list[str], finished: threading.Event,
                marker: str, deadline: float, after: int = -1
                ) -> tuple[str | None, int]:
    while time.monotonic() < deadline:
        smoke.drain(chunks, transcript)
        output = "".join(transcript)
        if "SATA_WRITE TEST_FAIL" in output:
            return "guest emitted SATA_WRITE TEST_FAIL", -1
        position = smoke.exact_line_position(output, marker, after)
        if position >= 0:
            return None, position
        if process.poll() is not None:
            finished.wait(timeout=0.25)
            smoke.drain(chunks, transcript)
            return f"QEMU exited before {marker}", -1
        try:
            transcript.append(chunks.get(timeout=0.05))
        except queue.Empty:
            pass
    return f"timeout before {marker}", -1


def run(qemu: Path, reference_image: Path, disk: Path, timeout: float,
        log: Path) -> int:
    if reference_image == disk:
        raise ValueError("disposable SATA disk must differ from reference image")
    reference_digest = file_sha256(reference_image)
    disk.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(reference_image, disk)
    qmp_port = reserve_port()
    process = subprocess.Popen(
        qemu_command(qemu, disk, qmp_port), stdin=subprocess.PIPE,
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
    qmp: QmpClient | None = None
    error: str | None = None
    try:
        qmp = QmpClient(qmp_port, deadline)
        error, _ = wait_marker(process, chunks, transcript, finished,
                               smoke.SHELL_PROMPT, deadline)
        if error is None:
            smoke.inject_ps2_command(process, "satawr")
            error, _ = wait_marker(process, chunks, transcript, finished,
                                   ACTIVE, deadline)
        if error is None:
            qmp.execute("blockdev-set-active", {
                "node-name": "reistdisk", "active": False,
            }, deadline)
            error, _ = wait_marker(process, chunks, transcript, finished,
                                   IO_ERROR, deadline)
        if error is None:
            qmp.execute("blockdev-set-active", {
                "node-name": "reistdisk", "active": True,
            }, deadline)
            for marker in (REINTEGRATED, INDEPENDENT_PROGRESS, RECOVERY_RW):
                error, _ = wait_marker(process, chunks, transcript, finished,
                                       marker, deadline)
                if error is not None:
                    break
        test_position = -1
        if error is None:
            error, test_position = wait_marker(
                process, chunks, transcript, finished, TEST_OK, deadline)
        if error is None:
            error, _ = wait_marker(process, chunks, transcript, finished,
                                   smoke.SHELL_PROMPT, deadline,
                                   after=test_position)
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
    output = "".join(transcript)
    if EXHAUSTED in output:
        error = "automatic SATA recovery exhausted before reintegration"
    if file_sha256(reference_image) != reference_digest:
        print(f"SATA HOTPLUG FAIL: reference image changed; log={log}")
        return 1
    if error is not None:
        print(f"SATA HOTPLUG FAIL: {error}; log={log}")
        return 1
    print(f"SATA HOTPLUG PASS log={log}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--disk", type=Path,
                        default=Path("build/sata-hotplug.img"))
    parser.add_argument("--log", type=Path,
                        default=Path("build/test-results/sata-hotplug.log"))
    parser.add_argument("--timeout", type=float, default=90.0)
    args = parser.parse_args()
    if not args.qemu.is_file() or not args.image.is_file() or args.timeout <= 0:
        parser.error("qemu/image must exist and timeout must be positive")
    return run(args.qemu.resolve(), args.image.resolve(), args.disk.resolve(),
               args.timeout, args.log.resolve())


if __name__ == "__main__":
    raise SystemExit(main())
