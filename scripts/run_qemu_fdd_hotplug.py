#!/usr/bin/env python3
"""Exercise live FDD removal and safe reintegration through QMP."""

from __future__ import annotations

import argparse
import hashlib
import json
import queue
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_qemu_smoke as smoke


ARMED = "REIST_FDD HOTPLUG_ARMED"
DISCONNECTED = "REIST_FDD DISCONNECT_DETECTED"
REINTEGRATED = "TEST_STAGE FDD_HOTPLUG_REINTEGRATED_OK"


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def create_test_floppy(path: Path) -> None:
    image = bytearray(1_474_560)
    boot = memoryview(image)[:512]
    boot[0:3] = b"\xEB\x3C\x90"
    boot[3:11] = b"REISTOS "
    boot[11:13] = (512).to_bytes(2, "little")
    boot[13] = 1
    boot[14:16] = (1).to_bytes(2, "little")
    boot[16] = 2
    boot[17:19] = (224).to_bytes(2, "little")
    boot[19:21] = (2880).to_bytes(2, "little")
    boot[21] = 0xF0
    boot[22:24] = (9).to_bytes(2, "little")
    boot[24:26] = (18).to_bytes(2, "little")
    boot[26:28] = (2).to_bytes(2, "little")
    boot[36] = 0
    boot[38] = 0x29
    boot[39:43] = (0x52454953).to_bytes(4, "little")
    boot[43:54] = b"REIST TEST "
    boot[54:62] = b"FAT12   "
    boot[510:512] = b"\x55\xAA"

    for fat_sector in (1, 10):
        offset = fat_sector * 512
        image[offset:offset + 3] = b"\xF0\xFF\xFF"
        image[offset + 3:offset + 5] = b"\xFF\x0F"
    root = 19 * 512
    image[root:root + 11] = b"HOTPLUG TXT"
    image[root + 11] = 0x20
    image[root + 26:root + 28] = (2).to_bytes(2, "little")
    payload = b"REIST-HOTPLUG\n"
    image[root + 28:root + 32] = len(payload).to_bytes(4, "little")
    data = 33 * 512
    image[data:data + len(payload)] = payload
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(image)


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
        greeting = self._read(deadline)
        if "QMP" not in greeting:
            raise RuntimeError("invalid QMP greeting")
        self._execute("qmp_capabilities", None, deadline)

    def _read(self, deadline: float) -> dict:
        self.connection.settimeout(max(0.05, deadline - time.monotonic()))
        line = self.stream.readline()
        if not line:
            raise ConnectionError("QMP connection closed")
        return json.loads(line.decode("utf-8"))

    def _execute(self, command: str, arguments: dict | None,
                 deadline: float) -> dict:
        request: dict[str, object] = {"execute": command, "id": command}
        if arguments is not None:
            request["arguments"] = arguments
        self.stream.write(json.dumps(request).encode("utf-8") + b"\r\n")
        while True:
            reply = self._read(deadline)
            if reply.get("id") != command:
                continue
            if "error" in reply:
                raise RuntimeError(f"QMP {command} failed: {reply['error']}")
            return reply

    def hmp(self, command: str, deadline: float) -> None:
        self._execute("human-monitor-command", {"command-line": command},
                      deadline)

    def close(self) -> None:
        try:
            self.stream.close()
        finally:
            self.connection.close()


def qemu_command(qemu: Path, image: Path, floppy: Path,
                 qmp_port: int) -> list[str]:
    return [
        str(qemu), "-accel", "tcg", "-machine", "pc", "-nodefaults",
        "-m", "512M", "-boot", "c",
        "-drive", f"file={image},format=raw,if=ide,index=0,media=disk",
        "-drive", (f"file={floppy},format=raw,if=floppy,index=0,"
                   "media=disk,id=reistfloppy"),
        "-display", "none", "-monitor", "none", "-serial", "stdio",
        "-qmp", f"tcp:127.0.0.1:{qmp_port},server=on,wait=off",
        "-snapshot", "-no-reboot", "-no-shutdown",
    ]


def send_paced(process: subprocess.Popen[str], command: str) -> None:
    assert process.stdin is not None
    for character in command + "\n":
        process.stdin.write(character)
        process.stdin.flush()
        time.sleep(0.075)


def run(qemu: Path, image: Path, floppy: Path, timeout: float,
        log: Path) -> int:
    if image == floppy:
        raise ValueError("test floppy must not replace the reference image")
    reference_digest = file_sha256(image)
    create_test_floppy(floppy)
    qmp_port = reserve_port()
    process = subprocess.Popen(
        qemu_command(qemu, image, floppy, qmp_port),
        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, text=True, encoding="utf-8",
        errors="replace", bufsize=0,
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
        error, _ = smoke.wait_for_line(process, chunks, transcript, finished,
                                       smoke.SHELL_PROMPT, deadline)
        if error is None:
            send_paced(process, "GTEST FDD_HOTPLUG")
            error, _ = smoke.wait_for_line(process, chunks, transcript,
                                           finished, ARMED, deadline)
        if error is None:
            qmp.hmp("eject reistfloppy", deadline)
            error, _ = smoke.wait_for_line(process, chunks, transcript,
                                           finished, DISCONNECTED, deadline)
        if error is None:
            media = str(floppy.resolve()).replace("\\", "/")
            qmp.hmp(f'change reistfloppy "{media}" raw', deadline)
            error, _ = smoke.wait_for_line(process, chunks, transcript,
                                           finished, REINTEGRATED, deadline)
        if error is None:
            error, marker = smoke.wait_for_line(
                process, chunks, transcript, finished, smoke.TEST_MARKER,
                deadline)
            if error is None:
                error, _ = smoke.wait_for_line(
                    process, chunks, transcript, finished, smoke.SHELL_PROMPT,
                    deadline, after=marker)
    except (OSError, RuntimeError, TimeoutError, ValueError) as caught:
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
    if file_sha256(image) != reference_digest:
        print(f"FDD HOTPLUG FAIL: reference image changed; log={log}")
        return 1
    if error is not None:
        print(f"FDD HOTPLUG FAIL: {error}; log={log}")
        return 1
    print(f"FDD HOTPLUG PASS log={log}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--floppy", type=Path,
                        default=Path("build/fdd-hotplug.img"))
    parser.add_argument("--log", type=Path,
                        default=Path("build/test-results/fdd-hotplug.log"))
    parser.add_argument("--timeout", type=float, default=45.0)
    args = parser.parse_args()
    if not args.qemu.is_file() or not args.image.is_file() or args.timeout <= 0:
        parser.error("qemu/image must exist and timeout must be positive")
    return run(args.qemu.resolve(), args.image.resolve(),
               args.floppy.resolve(), args.timeout, args.log.resolve())


if __name__ == "__main__":
    raise SystemExit(main())
