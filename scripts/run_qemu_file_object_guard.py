"""Bounded R3.38 guest: shared FAT12/FAT32/EXT2 lifetime and uncertain owner loss."""
from __future__ import annotations
import argparse
import hashlib
import json
import queue
import re
import shutil
import struct
import subprocess
import threading
import time
import zlib
from pathlib import Path

import build_user_program as builder
import run_qemu_ext2_symlink as ext2
import run_qemu_smoke as smoke
from build_system_programs import PROGRAMS
from create_floppy_boot_image import create_floppy_image
from measure_cpp_baseline import suppress_windows_test_dialogs
from run_qemu_math import ROOT, digest, kernel_digest
from verify_text_artifacts import read_fat_file

LIMIT = 2 * 1024 * 1024
_base_failure_marker = smoke.failure_marker
_base_line_position = smoke.exact_line_position


def guard_line_position(text, expected, after=-1):
    position = _base_line_position(text, expected, after)
    if position >= 0 or expected not in (
        "REIST_STORAGE RESOURCE_QUARANTINED 1", "REIST_STORAGE SERVICE_RESTARTED"
    ):
        return position
    # Asynchronous kernel diagnostics may immediately follow a pending prompt.
    # Accept only these complete records, not substrings or arbitrary prefixes.
    pattern = re.compile(rf"(?:^|\n){re.escape(smoke.SHELL_PROMPT + expected)}\r?(?=\n|$)")
    for match in pattern.finditer(text):
        start = match.start() + (1 if text[match.start():].startswith("\n") else 0) + len(smoke.SHELL_PROMPT)
        if start > after:
            return start
    return -1


def guard_failure_marker(text):
    for line in text.splitlines():
        if line.strip() in ("KERNEL PANIC", "KERNEL ASSERTION FAILED"):
            return line.strip()
    return _base_failure_marker(text)


def create_guard_floppy(stage1, stage2, kernel, signature):
    # Reuse the bounded boot/FAT layout, but serialize the PRIVATE fixture's
    # clean metadata using the real packed 32/28-byte decoder contracts. The
    # generic builder's optional journal template hashes padded sectors and
    # gives remap an extra word; neither is accepted by the actual decoders.
    image = bytearray(create_floppy_image(stage1, stage2, kernel, signature, reist_fat12=True))
    reserved = struct.unpack_from("<H", image, 14)[0]
    fingerprint = struct.unpack_from("<I", image, 39)[0]
    base = reserved - 195
    if base < 1 or reserved * 512 >= len(image):
        raise ValueError("private floppy metadata bounds")
    journal = bytearray(struct.pack("<IHHIQIII", 0x524A3132, 2, 32, fingerprint, 1, 0, 0, 0))
    struct.pack_into("<I", journal, 28, zlib.crc32(journal) & 0xffffffff)
    remap = bytearray(struct.pack("<IHHIQII", 0x52504D31, 1, 16, fingerprint, 1, 0, 0))
    struct.pack_into("<I", remap, 24, zlib.crc32(remap) & 0xffffffff)
    for sector, record in ((base, journal), (base+1, journal),
                           (base+130, remap), (base+131, remap)):
        image[sector*512:(sector+1)*512] = record + bytes(512-len(record))
    return bytes(image)


def patch_private_storage(image: Path, program: bytes, expected: bytes) -> None:
    """Patch only an existing private copy, never the reference or FAT chains."""
    target = "libexec/reist/storage.prg"
    if read_fat_file(image, target) != expected:
        raise ValueError("private storage baseline mismatch")
    with image.open("r+b") as stream:
        length = image.stat().st_size
        def read(offset, size):
            if offset < 0 or offset + size > length:
                raise ValueError("private FAT bounds")
            stream.seek(offset)
            data = stream.read(size)
            if len(data) != size:
                raise ValueError("private FAT short read")
            return data
        base = 8192 * 512
        boot = read(base, 512)
        reserved = struct.unpack_from("<H", boot, 14)[0]
        sectors = struct.unpack_from("<I", boot, 36)[0]
        unit = boot[13] * 512
        fat = base + reserved * 512
        data_base = fat + boot[16] * sectors * 512
        def chain(cluster, bound):
            offsets, seen = [], set()
            for _ in range(bound):
                if cluster < 2 or cluster in seen or cluster * 4 + 4 > sectors * 512:
                    raise ValueError("private FAT chain")
                seen.add(cluster)
                offset = data_base + (cluster - 2) * unit
                read(offset, unit)
                offsets.append(offset)
                cluster = struct.unpack("<I", read(fat + cluster * 4, 4))[0] & 0xfffffff
                if cluster >= 0xffffff8:
                    return offsets
            raise ValueError("private FAT quota")
        cluster = struct.unpack_from("<I", boot, 44)[0]
        entry_offset = 0
        for name in (b"LIBEXEC    ", b"REIST      ", b"STORAGE PRG"):
            selected = None
            for block in chain(cluster, 128):
                data = read(block, unit)
                for i in range(0, unit, 32):
                    if data[i:i+11] == name and data[i+11] != 15:
                        selected = data[i:i+32]
                        entry_offset = block + i
                        break
                if selected is not None:
                    break
            if selected is None:
                raise ValueError("private storage entry missing")
            cluster = (struct.unpack_from("<H", selected, 20)[0] << 16) | struct.unpack_from("<H", selected, 26)[0]
        offsets = chain(cluster, 512)
        if len(program) > len(offsets) * unit or not program:
            raise ValueError("private fault program exceeds reserved storage extent")
        padded = program + b"\0" * (len(offsets) * unit - len(program))
        for index, offset in enumerate(offsets):
            stream.seek(offset)
            stream.write(padded[index*unit:(index+1)*unit])
        stream.seek(entry_offset + 28)
        stream.write(struct.pack("<I", len(program)))
    if read_fat_file(image, target) != program:
        raise ValueError("private storage readback mismatch")


def expected_recovery(data: bytes) -> bytes:
    """Exact expected effects of the existing one-entry ACTIVE fixture."""
    if len(data) != ext2.BLOCKS * ext2.BLOCK_SIZE:
        raise ValueError("private EXT2 fixture size")
    base = 24 * ext2.BLOCK_SIZE
    magic, version, state, sequence, count, signature = struct.unpack_from("<6I", data, base)
    target = struct.unpack_from("<I", data, base + 32)[0]
    if (magic, version, state, count, target) != (0x4b4e4c53, 1, 1, 1, 42):
        raise ValueError("private EXT2 fixture shape")
    expected = bytearray(data)
    expected[target*512:(target+1)*512] = data[25*1024:25*1024+512]
    clean = bytearray(512)
    struct.pack_into("<6I", clean, 0, magic, version, 0, sequence, 0, signature)
    struct.pack_into("<I", clean, 24, zlib.crc32(clean) & 0xffffffff)
    expected[base:base+512] = clean
    expected[base+512:base+1024] = clean
    return bytes(expected)


def private_image(reference: Path, evidence: Path, mode: int) -> Path:
    program = evidence / ("storage-fault" + str(mode) + ".prg")
    old_run = builder.run
    def bounded_run(command, environment=None):
        with (evidence / ("compile-fault" + str(mode) + ".log")).open("a", encoding="utf-8") as log:
            subprocess.run(command, cwd=ROOT, env=environment, stdout=log, stderr=subprocess.STDOUT,
                check=True, timeout=90, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    try:
        builder.run = bounded_run
        builder.build(list(PROGRAMS["STORAGE.PRG"]), program, builder.find_zig(),
            include_dirs=[ROOT / "userspace/sdk/include", ROOT / "userspace/storage/include"],
            runtime_objects=[ROOT / "build/sdk/usr/lib/crt0.o"],
            runtime_libraries=[ROOT / "build/sdk/usr/lib/libreistos.a"],
            compile_flags=["-fno-inline-functions", "-DREIST_OBJECT_GUARD_FAULT_TEST=" + str(mode)],
            cache_directory=ROOT / "build/zig-global-cache")
    finally:
        builder.run = old_run
    image = evidence / ("private" + str(mode) + ".img")
    if image.exists():
        raise ValueError("private image already exists")
    shutil.copyfile(reference, image)
    patch_private_storage(image, program.read_bytes(), (ROOT / "build/programs/STORAGE.PRG").read_bytes())
    if kernel_digest(image) != kernel_digest(reference):
        raise ValueError("private fixture changed the kernel")
    return image


def run_case(qemu: Path, image: Path, evidence: Path, name: str, mode: int, deadline: float):
    disk = evidence / (name + "-ext2.img")
    ext2.create_ext2_image(disk)  # actual ACTIVE journal, not a fabricated syscall reply
    initial = digest(disk)
    expected = expected_recovery(disk.read_bytes()) if mode else None
    command = ext2.qemu_command(qemu, image, disk)
    if not mode:
        floppy = evidence / "fat12.img"
        floppy.write_bytes(create_guard_floppy(
            (ROOT / "build/stage1_floppy.bin").read_bytes(),
            (ROOT / "build/stage2_bios.bin").read_bytes(),
            (ROOT / "build/kernel.bin").read_bytes(),
            (ROOT / "build/kernel.bin.sig").read_bytes()))
        command += ["-drive", f"file={floppy},format=raw,if=floppy,index=0"]
    process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace", bufsize=0,
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
    prompt, error, commands = -1, None, []
    def execute(text, required=(), forbidden=("OBJGUARD FAIL",)):
        nonlocal prompt
        if time.monotonic() >= deadline:
            raise ValueError("aggregate guest deadline")
        smoke.inject_ps2_command(process, text)
        problem, next_prompt = smoke.wait_for_line(process, chunks, transcript, finished,
            smoke.SHELL_PROMPT, deadline, after=prompt)
        if problem:
            raise ValueError(problem)
        output = "".join(transcript)[prompt+1:next_prompt]
        if any(marker not in output for marker in required) or any(marker in output for marker in forbidden):
            diagnostic = "\n".join(line for line in output.splitlines()
                if "\x1b" not in line and not line.startswith("(qemu)"))
            raise ValueError("unexpected result " + repr(text) + ": " + repr(diagnostic[-1400:]))
        prompt = next_prompt
        commands.append(text)
        print("FILE_OBJECT_GUARD_STEP PASS " + text, flush=True)
        return output
    try:
        problem, boot = smoke.wait_for_line(process, chunks, transcript, finished, smoke.BOOT_MARKER, deadline)
        if problem:
            raise ValueError(problem)
        problem, prompt = smoke.wait_for_line(process, chunks, transcript, finished,
            smoke.SHELL_PROMPT, deadline, after=boot)
        if problem:
            raise ValueError(problem)
        if not mode:
            execute("objgdtst fat /", ("OBJGUARD FAT_OK /", "OBJGUARD OWNER_FAULT_OK"))
            execute("objgdtst fat12 /mnt/fdd0", ("OBJGUARD FAT_OK /mnt/fdd0",
                "OBJGUARD OWNER_FAULT_OK", "OBJGUARD FAT12_RENAME_UNSUPPORTED"))
            execute("objgdtst ext2", ("OBJGUARD EXT2_OK",))
            execute("objgdtst restart", ("OBJGUARD RESTART_OK",))
        else:
            before_loss = prompt
            output = execute("cat /mnt/hdd1/target.txt", ("cat: cannot open file",))
            if mode == 1 and "EAX=0x338FA017" not in output:
                raise ValueError("private fault register witness missing")
            # Wait for AUTOMATIC expiry/reap/restart before manual recovery.
            # Otherwise svcctl itself could hide a missing hang watchdog.
            for marker in ("REIST_STORAGE RESOURCE_QUARANTINED 1", "REIST_STORAGE SERVICE_RESTARTED"):
                problem, _ = smoke.wait_for_line(process, chunks, transcript, finished,
                    marker, deadline, after=before_loss)
                if problem:
                    raise ValueError(problem)
            if mode == 2 and "Exception: Invalid Opcode" in "".join(transcript)[before_loss:]:
                raise ValueError("lease did not terminate sleeping owner before its delayed fault")
            # Manual recovery shares the supervisor path; it must not clear
            # the already fenced uncertain medium or grant a replacement pin.
            execute("svcctl restart 5", ("COMPONENT RESTART_OK component=5",))
            execute("cat /mnt/hdd1/target.txt", ("cat: cannot open file",), (ext2.PAYLOAD,))
        execute("cat /htdocs/hello.js", ("Hello from REIST JavaScript",), ("cat: cannot open file",))
        raw = "".join(transcript)
        if mode and ("REIST_STORAGE RESOURCE_QUARANTINED" not in raw or
                     "REIST_STORAGE SERVICE_RESTARTED" not in raw):
            raise ValueError("uncertain owner did not quarantine/restart")
        generations = re.findall(r"REIST_STORAGE SERVICE_IDENTITY pid=(\d+) generation=(\d+)", raw)
        if len(set(generations)) < 2:
            raise ValueError("fresh Storage process generation not demonstrated")
    except (OSError, RuntimeError, ValueError) as caught:
        error = str(caught)
    finally:
        smoke.stop_process(process)
        finished.wait(timeout=1)
        reader.join(timeout=1)
        smoke.drain(chunks, transcript)
        raw = "".join(transcript)
        (evidence / (name + ".log")).write_text(raw, encoding="utf-8")
    error = error or ("guest output quota" if overflow.is_set() else None) or smoke.failure_marker(raw)
    if expected is not None and disk.read_bytes() != expected:
        error = error or "private recovery changed bytes outside its exact durable result"
    report = {"case": name, "passed": error is None, "error": error, "commands": commands,
        "initial_ext2_sha256": initial, "final_ext2_sha256": digest(disk)}
    print("FILE_OBJECT_GUARD_GUEST", name, "PASS" if error is None else "FAIL: " + error, flush=True)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    args = parser.parse_args()
    qemu, image, evidence = args.qemu.resolve(), args.image.resolve(), args.evidence.resolve()
    allowed = (ROOT / "build/codex-agent/r338-file-lifetime").resolve()
    if not qemu.is_file() or not image.is_file() or evidence.exists() or \
            evidence == allowed or not evidence.is_relative_to(allowed) or image.is_relative_to(evidence):
        parser.error("reference image, qemu and a new r338 evidence subdirectory required")
    suppress_windows_test_dialogs()
    smoke.failure_marker = guard_failure_marker
    smoke.exact_line_position = guard_line_position
    evidence.mkdir(parents=True)
    started = time.monotonic()
    original = digest(image)
    report = {"passed": False, "cases": [], "reference_sha256": original}
    try:
        for mode, name in enumerate(("normal", "fault", "hang")):
            target = image if not mode else private_image(image, evidence, mode)
            result = run_case(qemu, target, evidence, name, mode, started + 180)
            report["cases"].append(result)
            if not result["passed"]:
                break
        report["passed"] = len(report["cases"]) == 3 and all(case["passed"] for case in report["cases"])
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
        report["error"] = str(error)
    finally:
        if digest(image) != original:
            report["passed"] = False
            report["error"] = "reference image changed"
        report["elapsed_seconds"] = round(time.monotonic() - started, 3)
        (evidence / "result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("FILE_OBJECT_GUARD_GUEST " + ("PASS" if report["passed"] else "FAIL"), flush=True)
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
