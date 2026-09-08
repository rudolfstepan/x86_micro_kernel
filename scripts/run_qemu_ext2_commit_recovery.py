#!/usr/bin/env python3
"""Real Ring-3 committed-journal cleanup/refusal, never modifying the system image."""
from __future__ import annotations

import argparse
import json
import queue
import subprocess
import threading
import time
import zlib
from pathlib import Path

import run_qemu_ext2_symlink as ext2
import run_qemu_smoke as smoke
from measure_cpp_baseline import suppress_windows_test_dialogs

ROOT = Path(__file__).resolve().parents[1]
HEADER = 24 * ext2.BLOCK_SIZE
TARGET = 21 * ext2.BLOCK_SIZE
OUTPUT_LIMIT = 2 * 1024 * 1024


def create_committed_disk(path: Path, coherent: bool) -> bytes:
    if path.exists():
        raise ValueError("refusing to overwrite previous disk evidence")
    ext2.create_ext2_image(path)
    raw = bytearray(path.read_bytes())
    # Reuse the independent fixture's pre-transaction directory sector; build
    # a valid same-record rename (target.txt -> saved.txt) as the final sector.
    before = bytes(raw[HEADER + 1024:HEADER + 1536])
    final = bytearray(before)
    cursor = 0
    for _ in range(64):
        name_length = final[cursor + 6]
        record = ext2.get16(final, cursor + 4)
        if record < 8 or cursor + record > 512:
            raise ValueError("invalid fixture directory")
        if final[cursor + 8:cursor + 8 + name_length] == b"target.txt":
            final[cursor + 6] = 9
            final[cursor + 8:cursor + record] = b"\0" * (record - 8)
            final[cursor + 8:cursor + 17] = b"saved.txt"
            break
        cursor += record
        if cursor >= 512:
            raise ValueError("fixture source entry missing")
    else:
        raise ValueError("fixture directory budget")
    header = bytearray(ext2.journal_header(
        ext2.volume_signature(bytes(raw[1024:2048])), TARGET // 512,
        before, bytes(final)))
    ext2.put32(header, 8, 2)  # COMMITTED, not the original ACTIVE fixture.
    ext2.put32(header, 24, 0)
    ext2.put32(header, 24, zlib.crc32(header) & 0xFFFFFFFF)
    raw[HEADER:HEADER + 512] = header
    raw[HEADER + 512:HEADER + 1024] = header
    raw[TARGET:TARGET + 512] = final if coherent else before
    path.write_bytes(raw)
    return bytes(raw)


def verify_disk(initial: bytes, final: bytes, coherent: bool) -> None:
    if not coherent:
        if final != initial:
            raise ValueError("contradictory COMMITTED disk was changed")
        return
    if len(final) != len(initial) or final[:HEADER] != initial[:HEADER] or \
            final[HEADER + 1024:] != initial[HEADER + 1024:]:
        raise ValueError("committed cleanup changed non-header bytes")
    for offset in (HEADER, HEADER + 512):
        header = bytearray(final[offset:offset + 512])
        recorded_crc = int.from_bytes(header[24:28], "little")
        ext2.put32(header, 24, 0)
        if (int.from_bytes(header[0:4], "little") != 0x4B4E4C53 or
                int.from_bytes(header[4:8], "little") != 1 or
                int.from_bytes(header[8:12], "little") != 0 or
                int.from_bytes(header[12:16], "little") != 7 or
                int.from_bytes(header[16:20], "little") != 0 or
                header[20:24] != initial[offset + 20:offset + 24] or
                any(header[28:]) or
                zlib.crc32(header) & 0xFFFFFFFF != recorded_crc):
            raise ValueError("cleanup did not leave an exact v1 CLEAN header")
    if final[HEADER:HEADER + 512] != final[HEADER + 512:HEADER + 1024]:
        raise ValueError("cleanup headers disagree")


def run_case(qemu: Path, image: Path, evidence: Path,
             coherent: bool, deadline: float) -> dict:
    name = "coherent" if coherent else "contradictory"
    disk, log = evidence / (name + ".img"), evidence / (name + ".log")
    initial = create_committed_disk(disk, coherent)
    initial_hash = ext2.file_sha256(disk)
    process = subprocess.Popen(ext2.qemu_command(qemu, image, disk),
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, encoding="utf-8", errors="replace", bufsize=0,
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    chunks: queue.Queue[str] = queue.Queue(maxsize=OUTPUT_LIMIT + 1)
    transcript: list[str] = []
    finished, overflow = threading.Event(), threading.Event()

    def bounded_reader() -> None:
        try:
            assert process.stdout is not None
            for _ in range(OUTPUT_LIMIT):
                chunk = process.stdout.read(1)
                if not chunk:
                    return
                chunks.put_nowait(chunk)
            overflow.set()
            process.kill()
        finally:
            finished.set()

    reader = threading.Thread(target=bounded_reader, daemon=True)
    reader.start()
    error, prompt, commands = None, -1, 0

    def command(text: str, required: tuple[str, ...], forbidden: tuple[str, ...] = ()) -> None:
        nonlocal prompt, commands
        if time.monotonic() >= deadline:
            raise ValueError("aggregate guest deadline")
        smoke.inject_ps2_command(process, text)
        detail, next_prompt = smoke.wait_for_line(process, chunks, transcript,
            finished, smoke.SHELL_PROMPT, deadline, after=prompt)
        if detail:
            raise ValueError(detail)
        output = "".join(transcript)[prompt + 1:next_prompt]
        if any(token not in output for token in required) or any(token in output for token in forbidden):
            raise ValueError(f"unexpected command result: {text!r}: {output[-1200:]!r}")
        prompt = next_prompt
        commands += 1

    try:
        detail, boot = smoke.wait_for_line(process, chunks, transcript,
            finished, smoke.BOOT_MARKER, deadline)
        if detail:
            raise ValueError(detail)
        detail, prompt = smoke.wait_for_line(process, chunks, transcript,
            finished, smoke.SHELL_PROMPT, deadline, after=boot)
        if detail:
            raise ValueError(detail)
        for cycle in range(2):
            if coherent:
                command(f"cat {ext2.MOUNT}/saved.txt", (ext2.PAYLOAD,), ("cat: cannot open file",))
                command(f"cat {ext2.MOUNT}/target.txt", ("cat: cannot open file",), (ext2.PAYLOAD,))
            else:
                command(f"ls {ext2.MOUNT}", ("ls: path not found",), ("target.txt", "saved.txt"))
                command(f"ln -s target.txt {ext2.MOUNT}/new-link",
                        ("ln: unable to create symbolic link",))
                command(f"cat {ext2.MOUNT}/target.txt", ("cat: cannot open file",), (ext2.PAYLOAD,))
            # Independent normal FAT volume remains readable through the same
            # actual Ring-3 service; this must not merely be a rescue prompt.
            command("cat /htdocs/hello.js", ("Hello from REIST JavaScript",), ("cat: cannot open file",))
            if cycle == 0:
                command("svcctl restart 5", ("COMPONENT RESTART_OK component=5",))
    except (OSError, RuntimeError, ValueError) as caught:
        error = str(caught)
    finally:
        smoke.stop_process(process)
        finished.wait(timeout=1)
        smoke.drain(chunks, transcript)
        reader.join(timeout=1)
        raw_log = "".join(transcript)
        log.write_text(raw_log, encoding="utf-8")
    if overflow.is_set():
        error = error or "guest output quota"
    fatal = smoke.failure_marker(raw_log)
    if fatal:
        error = error or ("fatal guest marker: " + fatal)
    try:
        verify_disk(initial, disk.read_bytes(), coherent)
    except ValueError as caught:
        error = error or str(caught)
    report = {"case": name, "passed": error is None, "error": error,
              "commands": commands, "initial_sha256": initial_hash,
              "final_sha256": ext2.file_sha256(disk), "log": str(log)}
    print("EXT2_COMMIT_GUEST", name, "PASS" if error is None else "FAIL: " + error, flush=True)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=180)
    args = parser.parse_args()
    qemu, image, evidence = args.qemu.resolve(), args.image.resolve(), args.evidence.resolve()
    allowed = (ROOT / "build/codex-agent").resolve()
    if (not qemu.is_file() or not image.is_file() or not 0 < args.timeout <= 180 or
            evidence == allowed or not evidence.is_relative_to(allowed) or
            evidence.exists() or image.is_relative_to(evidence)):
        parser.error("existing qemu/image, new evidence directory under build/codex-agent and timeout <=180 required")
    suppress_windows_test_dialogs()
    evidence.mkdir(parents=True)
    started = time.monotonic()
    baseline = ext2.file_sha256(image)
    report = {"passed": False, "cases": [], "reference_sha256": baseline}
    try:
        for coherent in (False, True):
            result = run_case(qemu, image, evidence, coherent, started + args.timeout)
            report["cases"].append(result)
            if not result["passed"]:
                break
        report["passed"] = len(report["cases"]) == 2 and all(case["passed"] for case in report["cases"])
    except (OSError, ValueError, RuntimeError) as caught:
        report["error"] = str(caught)
    finally:
        if ext2.file_sha256(image) != baseline:
            report["passed"] = False
            report["error"] = "reference image changed"
        report["elapsed_seconds"] = round(time.monotonic() - started, 3)
        (evidence / "result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
