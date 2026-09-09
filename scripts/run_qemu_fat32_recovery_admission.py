"""Real legacy FAT32 recovery admission on private standard-size auxiliary media."""
from __future__ import annotations
import argparse
import hashlib
import json
import queue
import re
import struct
import subprocess
import threading
import time
import zlib
from pathlib import Path

import run_qemu_ext2_symlink as transport
import run_qemu_smoke as smoke
from measure_cpp_baseline import suppress_windows_test_dialogs

ROOT = Path(__file__).resolve().parents[1]
CASES = ("v1", "v2", "late-crc", "mirror-target", "duplicate-target")
MAGIC = 0x4a545352
DATA_START = 32 + 2 * 512
TOTAL_SECTORS = DATA_START + 65525
LIMIT = 2 * 1024 * 1024
PAYLOAD = b"FAT32_RECOVERY_INDEPENDENT_FILE\n"
MOUNT = "/mnt/hdd1"
REFUSAL = "+++ REIST journal recovery failed; refusing mount +++"
RESTART = "COMPONENT RESTART_OK component=5"


def header_v2(state: int, entries: list[tuple[int, bytes]]) -> bytes:
    header = bytearray(512)
    struct.pack_into("<6I", header, 0, MAGIC, 2, state, 7, len(entries), 0)
    for i, (target, before) in enumerate(entries):
        struct.pack_into("<2I", header, 24 + 8*i, target, zlib.crc32(before))
    struct.pack_into("<I", header, 20, zlib.crc32(header))
    return bytes(header)


def create_disk(path: Path, name: str) -> bytes:
    if path.exists() or name not in CASES:
        raise ValueError("new disk and known case required")
    raw = bytearray(TOTAL_SECTORS * 512)
    boot = bytearray(512)
    boot[:11] = b"\xeb\x58\x90REISTOS "
    struct.pack_into("<H", boot, 11, 512)
    boot[13] = 1
    struct.pack_into("<H", boot, 14, 32)
    boot[16] = 2
    boot[21] = 0xf8
    struct.pack_into("<HH", boot, 24, 63, 16)
    struct.pack_into("<II", boot, 32, TOTAL_SECTORS, 512)
    struct.pack_into("<IHH", boot, 44, 2, 1, 6)
    boot[64], boot[66] = 0x80, 0x29
    struct.pack_into("<I", boot, 67, 0x340340)
    boot[71:82], boot[82:90], boot[510:512] = b"RECOVERY   ", b"FAT32   ", b"\x55\xaa"
    raw[:512] = boot
    raw[6*512:7*512] = boot
    struct.pack_into("<I", raw, 512, 0x41615252)
    struct.pack_into("<4I", raw, 512 + 484, 0x61417272, 0xffffffff, 0xffffffff, 0)
    struct.pack_into("<I", raw, 512 + 508, 0xaa550000)
    for fat in (32, 32 + 512):
        struct.pack_into("<4I", raw, fat*512, 0x0ffffff8, 0xffffffff, 0x0fffffff, 0x0fffffff)
    root = DATA_START * 512
    raw[root:root+11] = b"TARGET  TXT"
    raw[root+11] = 0x20
    struct.pack_into("<HI", raw, root+26, 3, len(PAYLOAD))
    raw[root+512:root+512+len(PAYLOAD)] = PAYLOAD
    entries = [(6, bytes(boot))]
    if name != "v1":
        entries.append((7, bytes(512)))
    if name == "mirror-target":
        entries[0] = (31, bytes(boot))
    if name == "duplicate-target":
        entries[1] = (6, bytes(512))
    # Damage only ordinary recovery targets, never BPB/root/data needed to boot.
    for index, (target, before) in enumerate(entries):
        raw[(9+index)*512:(10+index)*512] = before
        if target != 31:
            raw[target*512:(target+1)*512] = bytes([0xa5 + index])*512
    if name == "v1":
        h = bytearray(512)
        struct.pack_into("<6I", h, 0, MAGIC, 1, 1, 6, zlib.crc32(entries[0][1]), 7)
        struct.pack_into("<I", h, 24, zlib.crc32(h[:24]))
        raw[8*512:9*512] = h
    else:
        h = header_v2(1, entries)
        raw[8*512:9*512] = h
        raw[31*512:32*512] = h
    if name == "late-crc":
        raw[9*512+13] ^= 1  # old reverse loop restored entry1 before rejecting0
    path.write_bytes(raw)
    return bytes(raw)


def expected_disk(initial: bytes, name: str) -> bytes:
    if name not in CASES or len(initial) != TOTAL_SECTORS*512:
        raise ValueError("fixture identity/length")
    if name not in ("v1", "v2"):
        return initial
    expected = bytearray(initial)
    for index in range(1 if name == "v1" else 2):
        target = 6 + index
        expected[target*512:(target+1)*512] = initial[(9+index)*512:(10+index)*512]
    clean = header_v2(0, [])
    expected[8*512:9*512] = clean
    expected[31*512:32*512] = clean
    return bytes(expected)


def verify_disk(initial: bytes, actual: bytes, name: str) -> None:
    if actual != expected_disk(initial, name):
        raise ValueError("whole-disk mismatch: " + name)


def validate_command(output: str, required: tuple[str, ...], forbidden: tuple[str, ...]) -> None:
    lines = output.replace("\r", "").splitlines()
    # Serial prompt may prefix a kernel diagnostic; match only that exact prefix.
    lines = [line.removeprefix(smoke.SHELL_PROMPT) for line in lines]
    def present(marker):
        if marker != RESTART:
            return marker in lines
        # svcctl includes the supervisor generation, distinct from the Storage
        # worker generation. Require the complete actual record, not a prefix.
        for line in lines:
            match = re.fullmatch(re.escape(RESTART) + r" generation=([1-9][0-9]{0,9})", line)
            if match and int(match[1]) <= 0xffffffff:
                return True
        return False
    if any(not present(marker) for marker in required) or any(marker in output for marker in forbidden):
        diagnostic = "\n".join(line for line in lines if "\x1b" not in line and not line.startswith("(qemu)"))
        raise ValueError("unexpected command output: " + repr(diagnostic[-1400:]))


def run_case(qemu: Path, image: Path, evidence: Path, name: str, deadline: float) -> dict:
    disk, log = evidence / (name + ".img"), evidence / (name + ".log")
    initial = create_disk(disk, name)
    process = subprocess.Popen(transport.qemu_command(qemu, image, disk),
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, encoding="utf-8", errors="replace", bufsize=0,
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    chunks = queue.Queue(maxsize=LIMIT + 1)
    transcript = []
    finished, overflow = threading.Event(), threading.Event()
    def read_output():
        try:
            with (evidence / (name + "-live.log")).open("w", encoding="utf-8") as live:
                for _ in range(LIMIT):
                    chunk = process.stdout.read(1)
                    if not chunk:
                        return
                    live.write(chunk)
                    if chunk == "\n":
                        live.flush()
                    chunks.put_nowait(chunk)
            overflow.set()
            process.kill()
        finally:
            finished.set()
    reader = threading.Thread(target=read_output, daemon=True)
    reader.start()
    error, prompt, commands = None, -1, []
    def execute(command, required, forbidden=()):
        nonlocal prompt
        if time.monotonic() >= deadline:
            raise ValueError("aggregate guest deadline")
        smoke.inject_ps2_command(process, command)
        problem, next_prompt = smoke.wait_for_line(process, chunks, transcript, finished,
            smoke.SHELL_PROMPT, deadline, after=prompt)
        if problem:
            raise ValueError(problem)
        validate_command("".join(transcript)[prompt+1:next_prompt], required, forbidden)
        prompt = next_prompt
        commands.append(command)
        print("FAT32_RECOVERY_STEP", name, command, "PASS", flush=True)
    try:
        problem, boot = smoke.wait_for_line(process, chunks, transcript, finished,
            smoke.BOOT_MARKER, deadline)
        if problem:
            raise ValueError(problem)
        problem, prompt = smoke.wait_for_line(process, chunks, transcript, finished,
            smoke.SHELL_PROMPT, deadline, after=boot)
        if problem:
            raise ValueError(problem)
        if "REIST OS userspace shell" not in "".join(transcript).splitlines():
            raise ValueError("normal userspace shell did not start")
        valid = name in ("v1", "v2")
        if not valid and REFUSAL not in "".join(transcript).splitlines():
            raise ValueError("missing real legacy journal mount refusal")
        for cycle in range(2):
            if valid:
                execute(f"cat {MOUNT}/target.txt", (PAYLOAD.decode().strip(),), ("cat: cannot open file",))
            else:
                execute("mount 1 fat32 /mnt/probe", ("ADMIN MOUNT_FAILED",), ("ADMIN MOUNT_OK", "ADMIN ROOT_PROTECTED"))
            execute("cat /htdocs/hello.js", ("print('Hello from REIST JavaScript');",), ("cat: cannot open file",))
            if cycle == 0:
                execute("svcctl restart 5", (RESTART,))
    except (OSError, RuntimeError, ValueError) as caught:
        error = str(caught)
    finally:
        smoke.stop_process(process)
        finished.wait(timeout=1)
        smoke.drain(chunks, transcript)
        reader.join(timeout=1)
        raw_log = "".join(transcript)
        log.write_text(raw_log, encoding="utf-8")
    if overflow.is_set() or smoke.failure_marker(raw_log):
        error = error or "guest quota/fatal marker"
    try:
        verify_disk(initial, disk.read_bytes(), name)
    except ValueError as caught:
        error = error or str(caught)
    report = {"case": name, "passed": error is None, "error": error,
        "commands": commands, "initial_sha256": hashlib.sha256(initial).hexdigest(),
        "final_sha256": transport.file_sha256(disk), "log": str(log)}
    print("FAT32_RECOVERY_GUEST", name, "PASS" if error is None else "FAIL: " + error, flush=True)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=300)
    args = parser.parse_args()
    qemu, image, evidence = args.qemu.resolve(), args.image.resolve(), args.evidence.resolve()
    allowed = (ROOT / "build/codex-agent/r340-fat32-recovery").resolve()
    if (not qemu.is_file() or not image.is_file() or not 0 < args.timeout <= 300 or
            evidence == allowed or not evidence.is_relative_to(allowed) or evidence.exists()):
        parser.error("existing qemu/image, new r340 subdirectory and timeout <=300 required")
    suppress_windows_test_dialogs()
    evidence.mkdir(parents=True)
    started = time.monotonic()
    baseline = transport.file_sha256(image)
    report = {"passed": False, "cases": [], "reference_sha256": baseline}
    try:
        for name in CASES:
            result = run_case(qemu, image, evidence, name, min(started + args.timeout, time.monotonic() + 75))
            report["cases"].append(result)
            if not result["passed"]:
                break
        report["passed"] = len(report["cases"]) == len(CASES) and all(case["passed"] for case in report["cases"])
    except (OSError, ValueError, RuntimeError) as caught:
        report["error"] = str(caught)
    finally:
        if transport.file_sha256(image) != baseline:
            report["passed"] = False
            report["error"] = "reference image changed"
        report["elapsed_seconds"] = round(time.monotonic() - started, 3)
        (evidence / "result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
