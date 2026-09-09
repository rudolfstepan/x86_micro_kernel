"""Bounded actual Ring-3 journal/legacy handoff, crash and noncooperative hang."""
from __future__ import annotations
import argparse
import hashlib
import json
import math
import queue
import re
import shutil
import struct
import subprocess
import threading
import time
from pathlib import Path
import build_user_program as builder
from build_system_programs import PROGRAMS
from create_native_boot_image import write_fat32_volume
from measure_cpp_baseline import suppress_windows_test_dialogs
from run_qemu_math import ROOT, kernel_digest
import run_qemu_smoke as smoke
import run_qemu_ext2_symlink as transport
import run_qemu_fat32_recovery_admission as recovery
from run_qemu_file_object_guard import guard_line_position, guard_failure_marker

LIMIT = 2*1024*1024
_base_keys = smoke.monitor_key_commands


class HostContinuity:
    """Qualify host observation, never extend a guest deadline or retry it.

    Fixed scalar state, a100ms sampling interval and a1s maximum observation
    gap. Wall time also catches suspend on platforms whose monotonic clock
    excludes it. A clock correction conservatively invalidates this run.
    """
    def __init__(self):
        self.previous = None
        self.problem = None
        self.samples = 0
        self.max_gap = 0.0
        self.finished = threading.Event()
        self.thread = None

    def observe(self, monotonic, wall):
        current = (monotonic, wall)
        self.samples += 1
        if not all(math.isfinite(value) for value in current):
            self.problem = self.problem or "non-finite host clock"
            return
        if self.previous is not None:
            gaps = tuple(now-before for now, before in zip(current, self.previous))
            self.max_gap = max(self.max_gap, *gaps)
            if min(gaps) < 0 or max(gaps) > 1.0:
                self.problem = self.problem or f"host clock/observation gap mono={gaps[0]:.3f}s wall={gaps[1]:.3f}s"
        self.previous = current

    def start(self):
        if self.thread is not None: raise RuntimeError("host observer already started")
        self.observe(time.monotonic(), time.time())
        def sample():
            try:
                for _ in range(4096):
                    if self.finished.wait(.1): return
                    self.observe(time.monotonic(), time.time())
                self.problem = self.problem or "host observation quota"
            except Exception as error:
                self.problem = self.problem or ("host observer failed: " + str(error)[:160])
        self.thread = threading.Thread(target=sample, daemon=True)
        self.thread.start()

    def stop(self):
        self.finished.set()
        if self.thread is not None:
            self.thread.join(timeout=1)
            if self.thread.is_alive(): self.problem = self.problem or "host observer did not stop"
        self.observe(time.monotonic(), time.time())

    def report(self):
        return {"valid": self.problem is None, "problem": self.problem,
                "samples": self.samples, "max_gap_seconds": round(self.max_gap, 6)}

    def check(self):
        # The coordinating thread may run first after suspend. It must not
        # accept a buffered prompt before the sampler gets CPU time again.
        previous = self.previous
        if self.problem is None and previous is not None:
            gaps = (time.monotonic()-previous[0], time.time()-previous[1])
            if not all(math.isfinite(gap) and 0 <= gap <= 1.0 for gap in gaps):
                self.problem = "host clock/observation gap before verifier progress"
        return self.problem


def private_trace_source(path, source):
    """Generate diagnostic-only copies, never rewrite the production sources.

    Each insertion compiles out unless explicitly enabled. Original source
    locations survive, including KASSERT line numbers in the no-define control.
    Records are observational, generation-labelled and modulo2^32, not an ABI
    or synchronization primitive. Guest RAM sampling is read-only.
    """
    signatures = {"kernel/sched/scheduler.c": "void task_exit_status(int status) {",
                  "kernel/syscall/syscall_table.c": "static int syscall_wait(int pid, int *user_status) {",
                  "drivers/char/serial.c": "void serial_write_char(uint16_t port, char ch) {"}
    signature = signatures.get(path)
    if signature is None or source.count(signature) != 1:
        raise ValueError("private trace source/signature drift")
    start = source.index(signature)
    end = source.index("\n}", start) + 2
    body = source[start:end]
    insertions = []

    def after(anchor, text):
        if body.count(anchor) != 1:
            raise ValueError("private trace anchor drift: " + anchor)
        insertions.append((start+body.index(anchor)+len(anchor), text))

    if path == "drivers/char/serial.c":
        helper = """/* attempts/sent/absent/timeout, then last bytes and port. */
static volatile uint32_t r341_serial_trace[9];
static void r341_serial_mark(uint32_t kind, uint16_t port, char ch) {
    if (kind >= 4U) return;
    (void)__atomic_fetch_add(&r341_serial_trace[kind], 1U, __ATOMIC_RELAXED);
    r341_serial_trace[4U+kind] = (uint8_t)ch;
    r341_serial_trace[8] = port;
}
"""
        after(signature+"\n", "    r341_serial_mark(0U, port, ch);\n"
              "    if (port == SERIAL_COM1 && !serial_com1_present) r341_serial_mark(2U, port, ch);\n")
        after("            outb(SERIAL_DATA(port), ch);\n", "            r341_serial_mark(1U, port, ch);\n")
        after('        __asm__ __volatile__("pause");\n    }\n', "    r341_serial_mark(3U, port, ch);\n")
    else:
        kind = "exit" if path == "kernel/sched/scheduler.c" else "wait"
        helper = """/* Per task: owner PID/generation, target PID, phase, visits. */
static volatile uint32_t r341_KIND_trace[MAX_TASKS][5];
static void r341_KIND_mark(int slot, int pid, uint32_t generation,
                           int target, uint32_t phase) {
    if (slot < 0 || slot >= MAX_TASKS) return;
    volatile uint32_t *record = r341_KIND_trace[slot];
    record[3] = 0U;
    record[0] = (uint32_t)pid;
    record[1] = generation;
    record[2] = (uint32_t)target;
    record[4] = record[4]+1U;
    record[3] = phase;
}
""".replace("KIND", kind)
        if kind == "exit":
            anchors = ["    task_table_unlock_irqrestore(snapshot_flags);\n",
                "    irq_enable();\n",
                "        KASSERT(process_begin_exit(process, process_generation));\n",
                "        device_domain_process_cleanup(process->pid, process_generation);\n",
                "        ipc_process_cleanup(process->pid, process_generation);\n",
                "        storage_request_cancel_process(process->pid, process_generation);\n",
                "        framebuffer_frame_process_cleanup(process->pid, process_generation);\n",
                "        process_close_all_files(process);\n",
                "        process_orphan_children(process->pid);\n",
                "    uint64_t now_ms = pit_monotonic_ms();\n", "    irq_disable();\n",
                "    uint32_t process_flags = process_table_lock_irqsave();\n",
                "    spinlock_acquire(&task_table_lock);\n",
                "                &tasks[finished].process->exit_waiters, NULL);\n",
                "    int next = claim_next_runnable(finished, now_ms);\n",
                "        activate_task_address_space(next);\n",
                "        activate_task_address_space(-1);\n"]
            for phase, anchor in enumerate(anchors, 1):
                after(anchor, f"    r341_exit_mark(exiting, process ? process->pid : 0, process_generation, 0, {phase}U);\n")
        else:
            anchors = ["                               sizeof(*user_status), true)) return -14;\n",
                "        uint32_t flags = process_table_lock_irqsave();\n",
                "                                                &wait_queue);\n",
                "        if (result < 0) {\n", "        if (result > 0) {\n",
                "            (void)scheduler_reap_finished_tasks();\n",
                "        if (wait_queue == NULL) {\n",
                "            return -11;\n        }\n",
                "                process_table_lock_ref(), flags) != 0) return -11;\n"]
            for phase, anchor in enumerate(anchors, 1):
                after(anchor, f"    r341_wait_mark(parent->task_id, parent->pid, parent->generation, pid, {phase}U);\n")
    insertions.append((start, helper))
    if len({pos for pos, _ in insertions}) != len(insertions):
        raise ValueError("private trace insertion overlap")
    for pos, text in sorted(insertions, reverse=True):
        line = source[:pos].count("\n")+1
        source = (source[:pos] + "#ifdef REIST_JOURNAL_HANDOFF_TRACE\n" + text +
                  f'#endif\n#line {line} "{path}"\n' + source[pos:])
    return f'#line 1 "{path}"\n' + source


def handoff_keys(text):
    if len(text) > 256:
        raise ValueError("private command quota")
    commands = []
    for index, part in enumerate(text.split("_")):
        if index:
            commands.append("sendkey shift-minus\n")
        commands.extend(_base_keys(part)[:-1])
    return commands + ["sendkey ret\n"]


def service_records(text, kind="IDENTITY"):
    if kind not in ("IDENTITY", "RETIRED"):
        raise ValueError("private service record kind")
    pattern = re.compile(r"(?:^|\n)(?:" + re.escape(smoke.SHELL_PROMPT) +
        r")?(?:\(qemu\) )?REIST_STORAGE SERVICE_" + kind + r" pid=([1-9][0-9]{0,9}) generation=([1-9][0-9]{0,9})\r?\n")
    return [(m.start(), int(m[1]), int(m[2])) for m in pattern.finditer(text)
            if int(m[1]) <= 0x7fffffff and int(m[2]) <= 0xffffffff]


def handoff_line_position(text, expected, after=-1):
    # Read-only HMP snapshots can leave exactly one monitor prompt immediately
    # before a lifecycle line. Preserve offsets and require the complete line;
    # no generic prefix stripping or arbitrary substring admission.
    if expected in ("REIST_STORAGE RESOURCE_QUARANTINED 1", "REIST_STORAGE SERVICE_RESTARTED"):
        pattern = re.compile(r"(?:^|\n)(?:"+re.escape(smoke.SHELL_PROMPT)+
            r")?\(qemu\) ("+re.escape(expected)+r")\r?\n")
        for match in pattern.finditer(text):
            if match.start(1) > after: return match.start(1)
    position = guard_line_position(text, expected, after)
    if position >= 0 or expected != smoke.SHELL_PROMPT:
        return position
    # A real shell prompt can be immediately followed by an asynchronous
    # kernel record. Recognize only the complete Storage lifecycle grammar;
    # don't mistake the missing line break for a stalled userspace shell.
    record = (r"REIST_STORAGE (?:RESOURCE_QUARANTINED (?:[0-9]|[12][0-9]|3[01])|"
              r"SERVICE_RESTARTED|SERVICE_READY|SERVICE_(?:IDENTITY|RETIRED) "
              r"pid=[1-9][0-9]{0,9} generation=[1-9][0-9]{0,9})")
    pattern = re.compile(r"(?:^|\n)" + re.escape(smoke.SHELL_PROMPT) + r"(?=" + record + r"\r?\n)")
    for match in pattern.finditer(text):
        position = match.start() + (text[match.start():].startswith("\n"))
        if position > after:
            return position
    return -1


def wait_replacement(process, chunks, transcript, before, deadline, continuity=None, finished=None):
    previous = service_records("".join(transcript)[:before])
    if not previous:
        raise ValueError("initial Storage identity missing")
    _, pid, generation = previous[-1]
    while time.monotonic() < deadline:
        if finished is not None and finished.is_set():
            raise ValueError("serial reader ended before verifier cleanup")
        if continuity is not None and continuity.check():
            raise ValueError("host continuity lost: " + continuity.problem)
        smoke.drain(chunks, transcript)
        raw = "".join(transcript)
        retired = any(pos > before and p == pid and g == generation
                      for pos, p, g in service_records(raw, "RETIRED"))
        fresh = any(pos > before and g > generation
                    for pos, p, g in service_records(raw))
        if retired and fresh:
            return
        if process.poll() is not None or guard_failure_marker(raw):
            raise ValueError("guest stopped before actual reap/replacement")
        try:
            transcript.append(chunks.get(timeout=0.05))
        except queue.Empty:
            pass
    raise ValueError("actual reap/replacement identity deadline")


def volume_files(image: Path) -> tuple[dict[str, bytes], int, int]:
    """Bounded private-fixture inventory, retaining long names and file bytes."""
    with image.open("rb") as stream:
        size = image.stat().st_size
        base = 8192*512
        def read(offset, length):
            if offset < 0 or length < 0 or offset+length > size:
                raise ValueError("private image range")
            stream.seek(offset)
            data = stream.read(length)
            if len(data) != length:
                raise ValueError("private short read")
            return data
        boot = read(base, 512)
        reserved = struct.unpack_from("<H", boot, 14)[0]
        sectors, fat_sectors = struct.unpack_from("<II", boot, 32)
        unit = boot[13]*512
        if unit != 512 or boot[16] != 2 or boot[510:] != b"\x55\xaa" or base+sectors*512 > size:
            raise ValueError("private reference geometry")
        fat = read(base+reserved*512, fat_sectors*512)
        first = base+(reserved+2*fat_sectors)*512
        used, files = set(), {}
        total = 0
        def chain(cluster):
            blocks = []
            for _ in range(131072):
                if cluster in used or cluster < 2 or cluster*4+4 > len(fat):
                    raise ValueError("private crosslinked/invalid FAT chain")
                used.add(cluster)
                blocks.append(read(first+(cluster-2)*unit, unit))
                cluster = struct.unpack_from("<I", fat, cluster*4)[0] & 0xfffffff
                if cluster >= 0xffffff8:
                    return b"".join(blocks)
            raise ValueError("private chain quota")
        def walk(cluster, path, depth):
            nonlocal total
            if depth > 32:
                raise ValueError("private directory depth")
            data = chain(cluster)
            long_name = []
            for pos in range(0, len(data), 32):
                e = data[pos:pos+32]
                if e[0] == 0: break
                if e[0] == 0xe5:
                    long_name = []; continue
                if e[11] == 15:
                    long_name.insert(0, e[1:11]+e[14:26]+e[28:32]); continue
                if long_name:
                    name = b"".join(long_name).decode("utf-16-le").split("\0")[0].rstrip("\uffff")
                else:
                    name = e[:8].decode("ascii").rstrip()
                    ext = e[8:11].decode("ascii").rstrip()
                    if ext: name += "."+ext
                    name = name.lower()
                long_name = []
                if e[11] & 8 or name in (".", ".."): continue
                if not name or "/" in name or "\\" in name or len(files) >= 8192:
                    raise ValueError("private path/quota")
                child = struct.unpack_from("<H", e, 26)[0] | struct.unpack_from("<H", e, 20)[0] << 16
                target = path+name
                if e[11] & 16:
                    walk(child, target+"/", depth+1)
                else:
                    length = struct.unpack_from("<I", e, 28)[0]
                    total += length
                    if length > 64*1024*1024 or total > 256*1024*1024 or target in files:
                        raise ValueError("private file quota/duplicate")
                    raw = chain(child) if child else b""
                    if length > len(raw): raise ValueError("private truncated file")
                    files[target] = raw[:length]
        walk(struct.unpack_from("<I", boot, 44)[0], "", 0)
        return files, sectors, struct.unpack_from("<I", boot, 67)[0]


def private_deadline_source(source):
    """Private fault injection only; no-define source is exactly recoverable.

    Suspend the admitted stage5 read once while it owns the ATA lock. Record
    actual PIO command submissions only during that call, not later root IO.
    """
    from test_reist_probe_domain import function
    changes = []
    def insert_at(position, text):
        line = source.count("\n", 0, position) + 1
        changes.append((position, "#ifdef REIST_JOURNAL_DEADLINE_TEST\n" + text +
            '#endif\n#line ' + str(line) + ' "drivers/block/ata.c"\n'))
    anchor = "#include <stdbool.h>\n"
    if source.count(anchor) != 1: raise ValueError("deadline includes drift")
    insert_at(source.index(anchor)+len(anchor),
        "static volatile uint32_t r341_deadline_probe[5]; /* active/fired/expired/commands/failed */\n")
    body = function(source, "int ata_external_journal_io(")
    start = source.index(body)
    anchor = "    if (!ata_transaction_begin_until(deadline_ms)) return -REIST_EBUSY;\n"
    if body.count(anchor) != 1: raise ValueError("deadline lock anchor drift")
    insert_at(start+body.index(anchor)+len(anchor), """    if (resource == 1U && operation == REIST_STORAGE_JOURNAL_READ &&
        sector == 60000U && count == 256U && !r341_deadline_probe[1]) {
        r341_deadline_probe[0] = r341_deadline_probe[1] = 1U;
        uint64_t now = pit_monotonic_ms();
        if (now < deadline_ms && deadline_ms-now <= 5000U)
            (void)scheduler_sleep_ms((uint32_t)(deadline_ms-now));
        r341_deadline_probe[2] = pit_monotonic_ms() >= deadline_ms;
    }
""")
    anchor = "    ata_transaction_end();\n    return ok ? 0 : -REIST_EIO;\n"
    if body.count(anchor) != 1: raise ValueError("deadline completion anchor drift")
    insert_at(start+body.index(anchor), """    if (r341_deadline_probe[0]) {
        r341_deadline_probe[4] = !ok;
        r341_deadline_probe[0] = 0U;
    }
""")
    for signature, expected in (("static bool ata_program_pio_batch(", 1),
                                ("static int ata_pio_read_block_size(", 2),
                                ("static bool ata_flush_cache_until(", 1)):
        body = function(source, signature)
        start = source.index(body)
        anchor = "    outb(ATA_COMMAND(base),"
        if body.count(anchor) != expected: raise ValueError("deadline command anchor drift")
        offset = 0
        for _ in range(expected):
            at = body.index(anchor, offset)
            insert_at(start+at, "    if (r341_deadline_probe[0]) ++r341_deadline_probe[3];\n")
            offset = at+len(anchor)
    for position, text in sorted(changes, reverse=True):
        source = source[:position] + text + source[position:]
    return '#line 1 "drivers/block/ata.c"\n' + source


def private_image(reference, evidence, mode):
    program = evidence / f"storage-{mode}.prg"
    sources = list(PROGRAMS["STORAGE.PRG"])
    if mode == 3:
        # The IO expiry witness must finish inside the unchanged outer STAT
        # request budget, not wait out the unrelated RPC deadline first.
        original = ROOT / "userspace/programs/storage_service.c"
        source = original.read_text(encoding="utf-8")
        anchor = "                                      0, 66581, 32, now+5000)) return 2;"
        if source.count(anchor) != 1: raise ValueError("private short reservation anchor drift")
        generated = evidence / "storage-short-reservation.c"
        generated.write_text('#line 1 "userspace/programs/storage_service.c"\n' +
            source.replace(anchor, anchor.replace("now+5000", "now+100")), encoding="utf-8")
        matches = [i for i, path in enumerate(sources) if (ROOT / path).resolve() == original]
        if len(matches) != 1: raise ValueError("private Storage source inventory mismatch")
        sources[matches[0]] = generated
    old_run = builder.run
    def bounded_run(command, environment=None):
        with (evidence / f"compile-{mode}.log").open("a", encoding="utf-8") as log:
            subprocess.run(command, cwd=ROOT, env=environment, stdout=log, stderr=subprocess.STDOUT,
                check=True, timeout=90, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    try:
        builder.run = bounded_run
        builder.build(sources, program, builder.find_zig(),
            include_dirs=[ROOT / "userspace/sdk/include", ROOT / "userspace/storage/include", ROOT],
            runtime_objects=[ROOT / "build/sdk/usr/lib/crt0.o"],
            runtime_libraries=[ROOT / "build/sdk/usr/lib/libreistos.a", ROOT / "build/sdk/usr/lib/libreistc.a"],
            compile_flags=["-fno-inline-functions", "-DREIST_JOURNAL_HANDOFF_TEST="+str(mode)] +
                (["-iquote", str(ROOT / "userspace/programs")] if mode == 3 else []),
            dependency_files=[ROOT / "drivers/block/ata_journal.h", ROOT / "lib/libc/string.h"],
            cache_directory=ROOT / "build/zig-global-cache")
    finally:
        builder.run = old_run
    if program.stat().st_size > 224*1024:
        raise ValueError("private program exceeds existing rescue-cache per-image budget")
    files, sectors, serial = volume_files(reference)
    target = "libexec/reist/storage.prg"
    if files[target] != (ROOT / "build/programs/STORAGE.PRG").read_bytes():
        raise ValueError("private Storage baseline mismatch")
    files[target] = program.read_bytes()
    image = evidence / f"private-{mode}.img"
    if image.exists(): raise ValueError("private image exists")
    shutil.copyfile(reference, image)
    # Rebuild the private data volume: larger PRGs receive complete FAT chains.
    # The boot partition, stage2, kernel and all other file bytes stay identical.
    expected = dict(files)
    files.pop("readme.txt")
    with image.open("r+b") as stream:
        write_fat32_volume(stream, 8192, sectors, serial, files)
    actual, _, _ = volume_files(image)
    if actual != expected or kernel_digest(image) != kernel_digest(reference):
        raise ValueError("private rebuild changed kernel/payload inventory")
    return image


def create_disk(path):
    initial = recovery.create_disk(path, "v2")
    clean = recovery.expected_disk(initial, "v2")
    path.write_bytes(clean)


def expected_handoff(initial, mode):
    raw = bytearray(initial)
    sequence = struct.unpack_from("<I", raw, 8*512+12)[0] + 1
    entries = [(60000+i, initial[(60000+i)*512:(60001+i)*512]) for i in range(20)]
    for i, (_, before) in enumerate(entries): raw[(9+i)*512:(10+i)*512] = before
    header = bytearray(recovery.header_v2(1 if mode else 0, entries if mode else []))
    struct.pack_into("<I", header, 12, sequence)
    struct.pack_into("<I", header, 20, 0)
    import zlib
    struct.pack_into("<I", header, 20, zlib.crc32(header))
    raw[8*512:9*512] = raw[31*512:32*512] = header
    if not mode:
        for i in range(20): raw[(60000+i)*512:(60001+i)*512] = bytes([0xa0+i])*512
    return bytes(raw)


def validate_handoff_command(output, required, forbidden):
    # Text-console wrapping may split a known fixture line. Accept only two
    # nonempty adjacent fragments that reproduce the entire expected line;
    # never join dynamic supervisor generations or discard intervening noise.
    lines = output.replace("\r", "").splitlines()
    recovered = []
    for marker in required:
        if marker == recovery.RESTART or marker in lines:
            continue
        matches = [index for index in range(len(lines)-1)
                   if lines[index] and lines[index+1] and
                   lines[index]+lines[index+1] == marker]
        if len(matches) == 1:
            recovered.append(marker)
    recovery.validate_command(output+"\n"+"\n".join(recovered), required, forbidden)


def start_guest(qemu, image, disk):
    process = subprocess.Popen(transport.qemu_command(qemu, image, disk), cwd=ROOT,
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, encoding="utf-8", errors="replace", bufsize=0,
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    try:
        # Same per-child Windows timer policy as the reference smoke/browser
        # harness. No global timer request, scaled clock or larger guest limit.
        return process, smoke.configure_qemu_host_timers(process)
    except BaseException:
        smoke.stop_process(process)
        raise


def wait_handoff_line(process, chunks, transcript, finished, expected, deadline,
                      diagnostics, *, after=-1, continuity=None):
    # Capture a stalled guest before cleanup; retain the original deadline
    # and result checks. These HMP queries inspect state, never resume a task,
    # inject guest input, restart a service or turn a failure into success.
    def wait_until(limit):
        while True:
            if finished.is_set():
                return "serial reader ended before verifier cleanup", -1
            if continuity is not None and continuity.check():
                return "host continuity lost: " + continuity.problem, -1
            boundary = min(limit, time.monotonic()+.25)
            result = smoke.wait_for_line(process, chunks, transcript, finished, expected, boundary, after=after)
            if finished.is_set():
                return "serial reader ended before verifier cleanup", -1
            if continuity is not None and continuity.check():
                return "host continuity lost: " + continuity.problem, -1
            if result[0] != f"timeout before {expected}" or boundary >= limit:
                return result
    first = min(deadline, time.monotonic()+20)
    result = wait_until(first)
    if result[0] != f"timeout before {expected}" or first >= deadline:
        return result
    diagnostic = {"waiting_for": expected, "after": after, "commands": []}
    if process.poll() is None and time.monotonic()+1 < deadline:
        try:
            for command in ("info registers", "info pic", "info blockstats"):
                smoke.qemu_monitor_command(process, command)
                diagnostic["commands"].append(command)
        except (OSError, RuntimeError) as error:
            diagnostic["error"] = str(error)
        diagnostics.append(diagnostic)
    return wait_until(deadline)


def read_handoff_output(process, path, chunks, finished, stopping, state):
    """Retain the first transport/log failure instead of silently losing input.

    EOF is expected only after the verifier begins cleanup, not when the
    guest is still being qualified. No retry, recovery input or guest rights.
    """
    try:
        with path.open("w", encoding="utf-8") as live:
            for _ in range(LIMIT):
                char = process.stdout.read(1)
                if not char:
                    if not stopping.is_set():
                        state["error"] = "serial reader EOF before verifier cleanup"
                    return
                live.write(char)
                if char == "\n": live.flush()
                chunks.put_nowait(char)
        state["error"] = "serial reader quota exceeded"
    except Exception as error:
        state["error"] = ("serial reader failed: " + type(error).__name__ + ": " + str(error))[:256]
    finally:
        finished.set()


def run_case(qemu, image, evidence, mode, deadline):
    name = ("normal", "fault", "hang", "deadline")[mode]
    disk = evidence / f"{name}-fat32.img"
    create_disk(disk)
    started = time.monotonic()
    deadline = min(deadline, started+120)
    continuity = HostContinuity()
    continuity.start()
    try:
        process, host_timer_policy = start_guest(qemu, image, disk)
    except BaseException:
        continuity.stop()
        raise
    chunks, transcript = queue.Queue(maxsize=LIMIT+1), []
    finished, stopping = threading.Event(), threading.Event()
    reader_state = {"error": None}
    thread = threading.Thread(target=read_handoff_output,
        args=(process, evidence / f"{name}-live.log", chunks, finished, stopping, reader_state), daemon=True)
    thread.start()
    error, prompt, commands, initial = None, -1, [], None
    diagnostics = []
    def execute(command, required=(), forbidden=()):
        nonlocal prompt
        if time.monotonic() >= deadline: raise ValueError("guest deadline")
        if finished.is_set(): raise ValueError("serial reader ended before verifier cleanup")
        if continuity.check(): raise ValueError("host continuity lost: " + continuity.problem)
        smoke.inject_ps2_command(process, command)
        problem, following = wait_handoff_line(process, chunks, transcript, finished,
            smoke.SHELL_PROMPT, deadline, diagnostics, after=prompt, continuity=continuity)
        if problem: raise ValueError(problem)
        output = "".join(transcript)[prompt+1:following]
        validate_handoff_command(output, required, forbidden)
        prompt = following; commands.append(command)
        print("JOURNAL_HANDOFF_STEP", name, command, "PASS", flush=True)
        return output
    try:
        problem, boot = wait_handoff_line(process, chunks, transcript, finished,
            smoke.BOOT_MARKER, deadline, diagnostics, continuity=continuity)
        if problem: raise ValueError(problem)
        problem, prompt = wait_handoff_line(process, chunks, transcript, finished,
            smoke.SHELL_PROMPT, deadline, diagnostics, after=boot, continuity=continuity)
        if problem: raise ValueError(problem)
        if "REIST OS userspace shell" not in "".join(transcript): raise ValueError("userspace dispatch missing")
        execute("cat /mnt/hdd1/target.txt", (recovery.PAYLOAD.decode().strip(),))
        execute("copy /htdocs/hello.js /mnt/hdd1/before.txt", ("        1 file(s) copied.",))
        execute("cat /mnt/hdd1/before.txt", ("print('Hello from REIST JavaScript');",))
        initial = disk.read_bytes()
        (evidence / f"{name}-before-handoff.img").write_bytes(initial)
        before = prompt
        output = execute("stat /mnt/hdd1/__handoff",
            ("Name: HANDOFF_FAILED", "Size: 5 bytes") if mode == 3 else
            ("stat: path not found",) if mode else ("Name: HANDOFF_RECOVERY_COMMIT_OK", "Size: 0 bytes"),
            () if mode == 3 else ("HANDOFF_FAILED",))
        if mode in (1, 2):
            if mode == 1 and "EAX=0x341FA017" not in output: raise ValueError("actual Ring-3 fault witness missing")
            for marker in ("REIST_STORAGE RESOURCE_QUARANTINED 1", "REIST_STORAGE SERVICE_RESTARTED"):
                problem, _ = wait_handoff_line(process, chunks, transcript, finished,
                    marker, deadline, diagnostics, after=before, continuity=continuity)
                if problem: raise ValueError(problem)
            if mode == 2 and "Exception: Invalid Opcode" in "".join(transcript)[before:]:
                raise ValueError("hang ended via test fault rather than bounded reap")
            wait_replacement(process, chunks, transcript, before, deadline, continuity, finished)
        if mode == 3:
            problem, _ = wait_handoff_line(process, chunks, transcript, finished,
                "REIST_STORAGE RESOURCE_QUARANTINED 1", deadline, diagnostics,
                after=before, continuity=continuity)
            if problem: raise ValueError(problem)
        actual = disk.read_bytes()
        if actual != (initial if mode == 3 else expected_handoff(initial, mode)):
            raise ValueError("exact whole-disk handoff outcome mismatch")
        (evidence / f"{name}-after-handoff.img").write_bytes(actual)
        if mode:
            execute("svcctl restart 5", (recovery.RESTART,))
            if mode == 3:
                wait_replacement(process, chunks, transcript, before, deadline, continuity, finished)
            execute("cat /mnt/hdd1/target.txt", ("cat: cannot open file",))
            if disk.read_bytes() != actual: raise ValueError("restart changed fenced media")
        else:
            execute("copy /htdocs/hello.js /mnt/hdd1/after.txt", ("        1 file(s) copied.",))
            execute("cat /mnt/hdd1/after.txt", ("print('Hello from REIST JavaScript');",))
        execute("cat /htdocs/hello.js", ("print('Hello from REIST JavaScript');",))
    except (OSError, ValueError, RuntimeError) as caught:
        error = str(caught)
    finally:
        stopping.set()
        smoke.stop_process(process); finished.wait(timeout=1)
        smoke.drain(chunks, transcript); thread.join(timeout=1)
        if thread.is_alive(): reader_state["error"] = reader_state["error"] or "serial reader did not stop"
        raw = "".join(transcript)
        (evidence / f"{name}.log").write_text(raw, encoding="utf-8")
        continuity.stop()
    if continuity.problem: error = "host continuity lost: " + continuity.problem
    if reader_state["error"]: error = reader_state["error"]
    if guard_failure_marker(raw): error = error or "fatal marker"
    report = {"case": name, "passed": error is None, "error": error, "commands": commands,
        "host_timer_policy": host_timer_policy, "host_continuity": continuity.report(),
        "serial_reader_error": reader_state["error"], "diagnostics": diagnostics,
        "elapsed_seconds": round(time.monotonic()-started, 3), "sha256": transport.file_sha256(disk)}
    print("JOURNAL_HANDOFF_GUEST", name, "PASS" if error is None else "FAIL: "+error, flush=True)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    args = parser.parse_args()
    qemu, image, evidence = args.qemu.resolve(), args.image.resolve(), args.evidence.resolve()
    allowed = (ROOT / "build/codex-agent/r341-journal-handoff").resolve()
    if not qemu.is_file() or not image.is_file() or evidence.exists() or evidence == allowed or not evidence.is_relative_to(allowed):
        parser.error("existing qemu/image and new r341 evidence subdirectory required")
    evidence.mkdir(parents=True); suppress_windows_test_dialogs()
    started = time.monotonic(); baseline = transport.file_sha256(image)
    report = {"passed": False, "cases": [], "reference_sha256": baseline}
    old_line, old_failure = smoke.exact_line_position, smoke.failure_marker
    smoke.exact_line_position, smoke.failure_marker = handoff_line_position, guard_failure_marker
    smoke.monitor_key_commands = handoff_keys
    try:
        for mode in range(3):
            if time.monotonic() >= started+360: raise ValueError("aggregate360s deadline")
            private = private_image(image, evidence, mode)
            result = run_case(qemu, private, evidence, mode, started+360)
            report["cases"].append(result)
            if not result["passed"]: break
        report["passed"] = len(report["cases"]) == 3 and all(c["passed"] for c in report["cases"])
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as caught:
        report["error"] = str(caught)
    finally:
        smoke.exact_line_position, smoke.failure_marker = old_line, old_failure
        smoke.monitor_key_commands = _base_keys
        if transport.file_sha256(image) != baseline:
            report["passed"] = False; report["error"] = "reference image changed"
        report["elapsed_seconds"] = round(time.monotonic()-started, 3)
        (evidence / "result.json").write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
    print("JOURNAL_HANDOFF", "PASS" if report["passed"] else "FAIL", report.get("error", ""))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
