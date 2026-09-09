"""Actual bounded journal authority/transport behavior, with persistent evidence."""
from pathlib import Path
import subprocess
import sys
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from measure_cpp_baseline import suppress_windows_test_dialogs
from test_reist_probe_domain import function


class JournalHandoffTests(unittest.TestCase):
    def test_lifecycle_record_after_exact_monitor_prompt(self):
        from run_qemu_journal_handoff import handoff_line_position, service_records
        marker = "REIST_STORAGE RESOURCE_QUARANTINED 1"
        for prefix in ("(qemu) ", "C:\\>(qemu) "):
            raw = "before\n"+prefix+marker+"\n"
            position = raw.index(marker)
            self.assertEqual(handoff_line_position(raw, marker), position)
            self.assertEqual(handoff_line_position(raw, marker, position), -1)
            identity = prefix+"REIST_STORAGE SERVICE_IDENTITY pid=11 generation=2\n"
            self.assertEqual(service_records(identity)[0][1:], (11, 2))
        for bad in ("noise (qemu) "+marker+"\n", "(qemu) "+marker+" extra\n",
                    "(qemu) "+marker, "C:\\>(qemu) (qemu) "+marker+"\n",
                    "(qemu) noise "+marker+"\n"):
            self.assertEqual(handoff_line_position(bad, marker), -1)

    def test_deadline_guest_injection_is_private_and_exactly_anchored(self):
        import re
        from run_qemu_journal_handoff import private_deadline_source
        source = (ROOT / "drivers/block/ata.c").read_text(encoding="utf-8")
        private = private_deadline_source(source)
        restored = re.sub(r'#ifdef REIST_JOURNAL_DEADLINE_TEST\n.*?#endif\n#line [0-9]+ "drivers/block/ata.c"\n',
            '', private, flags=re.S)
        self.assertEqual(restored.removeprefix('#line 1 "drivers/block/ata.c"\n'), source)
        for bad in (source+source, source.replace("if (!ata_transaction_begin_until(deadline_ms))", "if (changed)"),
                    source.replace("outb(ATA_COMMAND(base),", "different_command(")):
            with self.assertRaises(ValueError): private_deadline_source(bad)

    def test_ata_artifact_exemptions_reject_unrelated_drift(self):
        from verify_journal_handoff_artifacts import ata_protected_source_equal
        old = subprocess.check_output(["git", "show", "7d87119c:drivers/block/ata.c"],
            cwd=ROOT, timeout=15).decode("utf-8")
        new = (ROOT / "drivers/block/ata.c").read_text(encoding="utf-8")
        self.assertTrue(ata_protected_source_equal(old, new))
        for signature in ("static bool ata_transaction_begin(", "static bool ata_pio_wait_status(",
                          "static bool ata_write_sector_impl(", "static bool ata_wait_flush_complete("):
            body = function(new, signature)
            self.assertFalse(ata_protected_source_equal(old, new.replace(body, body.replace("{", "{ return false;", 1))))
        for bad in (new+"\nint unrelated;\n", new.replace("ATA_PIO_MAX_SECTORS", "UNBOUNDED"),
                    new+function(new, "static bool ata_read_sectors_pio_until(")):
            self.assertFalse(ata_protected_source_equal(old, bad))

    def test_actual_pio_deadlines_and_legacy_batches(self):
        suppress_windows_test_dialogs()
        evidence = ROOT / "build/codex-agent/r341-journal-handoff" / ("pio-" + uuid.uuid4().hex)
        evidence.mkdir(parents=True)
        source = (ROOT / "drivers/block/ata.c").read_text(encoding="utf-8")
        functions = ("static bool ata_pio_wait_status(", "static bool ata_pio_select_read(",
            "static int ata_pio_read_block_size(", "static bool ata_pio_range_valid(",
            "static bool ata_pio_read_range_valid(", "static bool ata_program_pio_batch(",
            "static bool ata_read_sectors_pio_until(", "static bool ata_read_sectors_pio_impl(",
            "static bool ata_write_sectors_pio_deferred_until(", "static bool ata_write_sectors_pio_deferred_impl(",
            "static uint8_t ata_flush_command_for_drive(", "static bool ata_flush_cache_until(",
            "static bool ata_flush_cache_impl(")
        (evidence / "ata_pio.inc").write_text("\n".join(function(source, f) for f in functions), encoding="utf-8")
        for opt in ("-O0", "-O2"):
            exe = evidence / (opt + ".exe")
            command = ["gcc", "-std=c11", opt, "-Wall", "-Wextra", "-Werror", "-fno-builtin",
                "-DJOURNAL_HANDOFF_PIO_TEST", "-I", str(ROOT), "-I", str(ROOT / "include"),
                "-I", str(evidence), str(ROOT / "test/journal_handoff_host.c"), "-o", str(exe)]
            for index, current in enumerate((command, [str(exe)])):
                result = subprocess.run(current, cwd=ROOT, capture_output=True, text=True,
                    timeout=90 if index == 0 else 30,
                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                (evidence / (opt + ("-compile.log" if index == 0 else "-run.log"))).write_text(
                    result.stdout + result.stderr, encoding="utf-8")
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_dead_serial_reader_is_not_a_guest_timeout_or_success(self):
        import queue
        import threading
        import time
        from unittest.mock import Mock, patch
        import run_qemu_journal_handoff as guest
        process = Mock()
        process.poll.return_value = None
        finished = threading.Event()
        finished.set()
        for queued in ([], ["C:\\>"]):
            diagnostics = []
            with patch.object(guest.smoke, "qemu_monitor_command") as monitor:
                problem, position = guest.wait_handoff_line(process, queue.Queue(),
                    queued, finished, "C:\\>", time.monotonic()+.02, diagnostics)
            self.assertEqual(problem, "serial reader ended before verifier cleanup")
            self.assertEqual(position, -1)
            self.assertEqual(diagnostics, [])
            monitor.assert_not_called()

    def test_serial_reader_retains_failures_and_cleanup_state(self):
        import io
        import queue
        import threading
        from unittest.mock import Mock, patch
        import run_qemu_journal_handoff as guest
        for mode in ("read", "log", "queue", "eof", "quota", "cleanup"):
            with self.subTest(mode=mode):
                process = Mock()
                process.stdout = io.StringIO("abc" if mode not in ("eof", "cleanup") else "")
                if mode == "read": process.stdout = Mock(read=Mock(side_effect=OSError("injected read failure")))
                sink = io.StringIO()
                if mode == "log": sink = Mock(write=Mock(side_effect=OSError("injected log failure")))
                context = Mock()
                context.__enter__ = Mock(return_value=sink)
                context.__exit__ = Mock(return_value=False)
                path = Mock(open=Mock(return_value=context))
                chunks = queue.Queue(maxsize=1 if mode == "queue" else 5)
                finished, stopping = threading.Event(), threading.Event()
                if mode == "cleanup": stopping.set()
                state = {"error": None}
                with patch.object(guest, "LIMIT", 3):
                    guest.read_handoff_output(process, path, chunks, finished, stopping, state)
                self.assertTrue(finished.is_set())
                if mode == "cleanup": self.assertIsNone(state["error"])
                else: self.assertIn("serial reader", state["error"])
                if mode in ("read", "log"): self.assertIn("injected", state["error"])
                if mode == "quota": self.assertIn("quota", state["error"])
                self.assertLessEqual(len(state["error"] or ""), 256)

    def test_host_continuity_rejects_pauses_and_clock_changes(self):
        from run_qemu_journal_handoff import HostContinuity
        for bad in ((8.1, 108.1), (1.2, 108.1), (1.0, 101.2), (1.2, 100.0)):
            observer = HostContinuity()
            observer.observe(1.0, 101.0)
            observer.observe(1.1, 101.1)
            self.assertTrue(observer.report()["valid"])
            observer.observe(*bad)
            self.assertFalse(observer.report()["valid"])
            first = observer.problem
            observer.observe(bad[0]+.1, bad[1]+.1)
            self.assertEqual(observer.problem, first)
        observer = HostContinuity()
        for i in range(100): observer.observe(i/10, 100+i/10)
        self.assertTrue(observer.report()["valid"])
        self.assertEqual(observer.report()["samples"], 100)
        for invalid in (float("nan"), float("inf"), -float("inf")):
            observer = HostContinuity()
            observer.observe(invalid, 100)
            self.assertFalse(observer.report()["valid"])

    def test_wait_rejects_host_pause_without_extending_deadline(self):
        import threading
        from unittest.mock import Mock, patch
        import run_qemu_journal_handoff as guest
        observer = guest.HostContinuity()
        observer.observe(10, 110)
        arguments = (Mock(), object(), [], threading.Event(), "C:\\>", 100, [])
        def paused(*args, **kwargs):
            observer.observe(17, 117)
            return ("timeout before C:\\>", -1)
        with patch.object(guest.time, "monotonic", return_value=10), \
             patch.object(guest.time, "time", return_value=110), \
             patch.object(guest.smoke, "wait_for_line", side_effect=paused) as wait, \
             patch.object(guest.smoke, "qemu_monitor_command") as monitor:
            result = guest.wait_handoff_line(*arguments, continuity=observer)
            self.assertIn("host continuity lost", result[0])
            self.assertEqual(result[1], -1)
            self.assertEqual(wait.call_args.args[5], 10.25)
            monitor.assert_not_called()
        observer = guest.HostContinuity()
        observer.observe(0, 100)
        with patch.object(guest.time, "monotonic", return_value=7), \
             patch.object(guest.time, "time", return_value=107), \
             patch.object(guest.smoke, "wait_for_line", return_value=(None, 55)) as wait:
            self.assertIn("host continuity lost", guest.wait_handoff_line(*arguments, continuity=observer)[0])
            wait.assert_not_called()
        # Even a prompt must not turn an interrupted run into success.
        with patch.object(guest.smoke, "wait_for_line", return_value=(None, 55)) as wait:
            self.assertIn("host continuity lost", guest.wait_handoff_line(*arguments, continuity=observer)[0])
            wait.assert_not_called()
        observer = guest.HostContinuity()
        observer.observe(10, 110)
        with patch.object(guest.time, "monotonic", return_value=10), \
             patch.object(guest.time, "time", return_value=110), \
             patch.object(guest.smoke, "wait_for_line", return_value=(None, 55)) as wait:
            self.assertEqual(guest.wait_handoff_line(*arguments, continuity=observer), (None, 55))
            self.assertEqual(wait.call_args.args[5], 10.25)

    def test_private_kernel_trace_is_anchored_and_preserves_source(self):
        from run_qemu_journal_handoff import private_trace_source
        cases = (("kernel/sched/scheduler.c", "void task_exit_status(", "    irq_enable();\n"),
                 ("kernel/syscall/syscall_table.c", "static int syscall_wait(", "        if (wait_queue == NULL) {\n"),
                 ("drivers/char/serial.c", "void serial_write_char(", "            outb(SERIAL_DATA(port), ch);\n"))
        for path, signature, anchor in cases:
            source = (ROOT / path).read_text(encoding="utf-8")
            generated = private_trace_source(path, source)
            # With private sections removed, every original source byte and
            # line remains. No silent replacement of real control flow.
            import re
            restored = re.sub(r'#ifdef REIST_JOURNAL_HANDOFF_TRACE\n.*?#endif\n#line [0-9]+ "[^"]+"\n',
                              '', generated, flags=re.S)
            self.assertEqual(restored.removeprefix(f'#line 1 "{path}"\n'), source)
            for bad in (source+source, source.replace(signature, "void renamed("),
                        source.replace(anchor, anchor.rstrip()+" /* changed */\n")):
                with self.assertRaises(ValueError): private_trace_source(path, bad)
        with self.assertRaises(ValueError): private_trace_source("kernel/proc/process.c", "")

    def test_private_trace_actual_serial_wait_and_generation_records(self):
        import re
        from run_qemu_journal_handoff import private_trace_source
        suppress_windows_test_dialogs()
        evidence = ROOT / "build/codex-agent/r341-journal-handoff" / ("trace-" + uuid.uuid4().hex)
        evidence.mkdir(parents=True)
        parts = []
        for path, signature in (("kernel/sched/scheduler.c", None),
                               ("kernel/syscall/syscall_table.c", "static int syscall_wait("),
                               ("drivers/char/serial.c", "void serial_write_char(")):
            generated = private_trace_source(path, (ROOT / path).read_text(encoding="utf-8"))
            parts.append(re.search(r'#ifdef REIST_JOURNAL_HANDOFF_TRACE\n(.*?)#endif', generated, re.S)[1])
            if signature: parts.append(function(generated, signature))
        (evidence / "handoff_trace.inc").write_text("\n".join(parts), encoding="utf-8")
        for opt in ("-O0", "-O2"):
            exe = evidence / (opt + ".exe")
            command = ["gcc", "-std=c11", opt, "-Wall", "-Wextra", "-Werror", "-fno-builtin",
                "-DJOURNAL_HANDOFF_TRACE_TEST", "-DREIST_JOURNAL_HANDOFF_TRACE", "-I", str(ROOT),
                "-I", str(ROOT / "include"), "-I", str(evidence),
                str(ROOT / "test/journal_handoff_host.c"), "-o", str(exe)]
            for index, current in enumerate((command, [str(exe)])):
                result = subprocess.run(current, cwd=ROOT, capture_output=True, text=True,
                    timeout=90 if index == 0 else 30,
                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                (evidence / (opt + ("-compile.log" if index == 0 else "-run.log"))).write_text(
                    result.stdout + result.stderr, encoding="utf-8")
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_private_errno_preserves_actual_result_without_extra_syscalls(self):
        suppress_windows_test_dialogs()
        evidence = ROOT / "build/codex-agent/r341-journal-handoff" / ("errno-" + uuid.uuid4().hex)
        evidence.mkdir(parents=True)
        source = (ROOT / "userspace/programs/storage_service.c").read_text(encoding="utf-8")
        (evidence / "handoff_private.inc").write_text(
            function(source, "static int handoff_raw("), encoding="utf-8")
        for opt in ("-O0", "-O2"):
            exe = evidence / (opt + ".exe")
            command = ["gcc", "-std=c11", opt, "-Wall", "-Wextra", "-Werror", "-fno-builtin",
                "-DJOURNAL_HANDOFF_FIXTURE_TEST", "-I", str(ROOT), "-I", str(ROOT / "include"),
                "-I", str(evidence), str(ROOT / "test/journal_handoff_host.c"), "-o", str(exe)]
            for index, current in enumerate((command, [str(exe)])):
                result = subprocess.run(current, cwd=ROOT, capture_output=True, text=True,
                    timeout=90 if index == 0 else 30,
                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                (evidence / (opt + ("-compile.log" if index == 0 else "-run.log"))).write_text(
                    result.stdout + result.stderr, encoding="utf-8")
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_stall_diagnostic_preserves_deadline_and_failure(self):
        import threading
        from unittest.mock import Mock, patch
        import run_qemu_journal_handoff as guest
        process = Mock()
        process.poll.return_value = None
        arguments = (process, object(), [], threading.Event(), "C:\\>", 100)
        timeout = ("timeout before C:\\>", -1)
        for final in ((None, 123), timeout):
            diagnostics = []
            clock = [10.0]
            def advance(*args, **kwargs):
                clock[0] = args[5]
                return timeout if clock[0] <= 30 else final
            with patch.object(guest.time, "monotonic", side_effect=lambda: clock[0]), \
                 patch.object(guest.smoke, "wait_for_line", side_effect=advance) as wait, \
                 patch.object(guest.smoke, "qemu_monitor_command") as monitor:
                self.assertEqual(guest.wait_handoff_line(*arguments, diagnostics, after=5), final)
                limits = [call.args[5] for call in wait.call_args_list]
                self.assertEqual(limits[0], 10.25)
                self.assertIn(30, limits)
                self.assertTrue(all(10 < limit <= 100 for limit in limits))
                self.assertEqual(limits[-1], 30.25 if final[0] is None else 100)
                self.assertTrue(all(call.kwargs == {"after": 5} for call in wait.call_args_list))
                self.assertEqual([call.args[1] for call in monitor.call_args_list],
                                 ["info registers", "info pic", "info blockstats"])
                self.assertEqual(len(diagnostics), 1)
        for now, result in ((99, timeout), (10, (None, 123)), (10, ("guest failed", -1))):
            clock = [float(now)]
            def advance_result(*args, **kwargs):
                clock[0] = args[5]
                return result
            with patch.object(guest.time, "monotonic", side_effect=lambda: clock[0]), \
                 patch.object(guest.smoke, "wait_for_line", side_effect=advance_result) as wait, \
                 patch.object(guest.smoke, "qemu_monitor_command") as monitor:
                self.assertEqual(guest.wait_handoff_line(*arguments, []), result)
                self.assertEqual(wait.call_count, 4 if now == 99 else 1)
                monitor.assert_not_called()
        clock = [10.0]
        def advance_timeout(*args, **kwargs):
            clock[0] = args[5]
            return timeout
        with patch.object(guest.time, "monotonic", side_effect=lambda: clock[0]), \
             patch.object(guest.smoke, "wait_for_line", side_effect=advance_timeout), \
             patch.object(guest.smoke, "qemu_monitor_command", side_effect=OSError("monitor closed")):
            diagnostics = []
            self.assertEqual(guest.wait_handoff_line(*arguments, diagnostics), timeout)
            self.assertEqual(diagnostics[0]["error"], "monitor closed")
            self.assertEqual(clock[0], 100)

    def test_guest_uses_reference_timer_policy_and_cleans_failed_start(self):
        from unittest.mock import patch
        import run_qemu_journal_handoff as guest
        process = object()
        with patch.object(guest.subprocess, "Popen", return_value=process), \
             patch.object(guest.smoke, "configure_qemu_host_timers", return_value=True) as timer, \
             patch.object(guest.smoke, "stop_process") as stop:
            self.assertEqual(guest.start_guest(Path("qemu"), Path("system"), Path("disk")), (process, True))
            timer.assert_called_once_with(process)
            stop.assert_not_called()
        with patch.object(guest.subprocess, "Popen", return_value=process), \
             patch.object(guest.smoke, "configure_qemu_host_timers", side_effect=OSError("policy")), \
             patch.object(guest.smoke, "stop_process") as stop:
            with self.assertRaises(OSError): guest.start_guest(Path("qemu"), Path("system"), Path("disk"))
            stop.assert_called_once_with(process)

    def test_command_accepts_only_exact_adjacent_console_wrap(self):
        import run_qemu_journal_handoff as guest
        validate = getattr(guest, "validate_handoff_command", guest.recovery.validate_command)
        marker = guest.recovery.PAYLOAD.decode().strip()
        wrapped = "FAT32_RECOVERY_INDEPENDENT_\nFILE"
        validate(marker+"\n", (marker,), ())
        validate(wrapped+"\n", (marker,), ())
        for bad in ("noise "+wrapped, wrapped+" extra", wrapped.replace("\n", "\nnoise\n"),
                    wrapped.replace("\n", "\n\n"), wrapped.replace("FILE", "FAIL")):
            with self.assertRaises(ValueError): validate(bad, (marker,), ())
        with self.assertRaises(ValueError):
            validate(wrapped+"\nHANDOFF_FAILED", (marker,), ("HANDOFF_FAILED",))
        with self.assertRaises(ValueError):
            validate(guest.recovery.RESTART+"\n generation=2\n", (guest.recovery.RESTART,), ())

    def test_service_identity_requires_complete_exact_generation(self):
        from run_qemu_journal_handoff import service_records, handoff_line_position
        marker = "REIST_STORAGE SERVICE_IDENTITY pid=11 generation=2"
        self.assertEqual(service_records(marker+"\n")[0][1:], (11, 2))
        self.assertEqual(service_records("C:\\>"+marker+"\r\n")[0][1:], (11, 2))
        for bad in (marker, marker+" trailing\n", "noise "+marker+"\n",
                    marker.replace("=2", "=0")+"\n",
                    marker.replace("=2", "=4294967296")+"\n"):
            self.assertFalse(service_records(bad))
        for record in ("RESOURCE_QUARANTINED 1", "SERVICE_READY", "SERVICE_RESTARTED",
                       "SERVICE_IDENTITY pid=11 generation=2"):
            text = "before\nC:\\>REIST_STORAGE "+record+"\n"
            self.assertEqual(handoff_line_position(text, "C:\\>"), 7)
            self.assertEqual(handoff_line_position(text, "C:\\>", 7), -1)
        for text in ("C:\\>junk\n", "noise C:\\>REIST_STORAGE SERVICE_READY\n",
                     "C:\\>REIST_STORAGE RESOURCE_QUARANTINED 32\n",
                     "C:\\>REIST_STORAGE SERVICE_RESTARTED trailing\n"):
            self.assertEqual(handoff_line_position(text, "C:\\>"), -1)

    def test_worker_artifact_allows_only_probe_bound_extension(self):
        from verify_journal_handoff_artifacts import worker_probe_delta
        old = bytearray(4610)
        old[4497:4503] = bytes.fromhex("81fb82000000")
        old[4604:4609] = bytes.fromhex("6882000000")
        new = bytearray(old); new[4499] = new[4605] = 131
        self.assertTrue(worker_probe_delta(old, new))
        self.assertFalse(worker_probe_delta(old, new[:-1]))
        for offset in (0, 4498, 4499, 4503, 4605, 4609):
            bad = bytearray(new); bad[offset] ^= 1
            self.assertFalse(worker_probe_delta(old, bad))

    def test_private_inventory_and_whole_disk_oracle(self):
        from create_native_boot_image import write_fat32_volume
        from run_qemu_journal_handoff import volume_files, create_disk, expected_handoff, handoff_keys
        self.assertEqual(handoff_keys("__"), ["sendkey shift-minus\n", "sendkey shift-minus\n", "sendkey ret\n"])
        with self.assertRaises(ValueError): handoff_keys("a"*257)
        with self.assertRaises(ValueError): handoff_keys("bad\ncommand")
        evidence = ROOT / "build/codex-agent/r341-journal-handoff" / ("fixtures-" + uuid.uuid4().hex)
        evidence.mkdir(parents=True)
        image = evidence / "private.img"
        files = {"libexec/reist/storage.prg": b"program"*97,
                 "htdocs/long-filename.html": b"html", "bin/stat.prg": b"stat"}
        with image.open("w+b") as stream:
            stream.truncate((8192+70000)*512)
            write_fat32_volume(stream, 8192, 70000, 123, files)
        actual, sectors, serial = volume_files(image)
        self.assertEqual((sectors, serial), (70000, 123))
        self.assertEqual({k:v for k,v in actual.items() if k != "readme.txt"}, files)
        disk = evidence / "aux.img"
        create_disk(disk)
        initial = disk.read_bytes()
        normal, failed = expected_handoff(initial, 0), expected_handoff(initial, 1)
        self.assertNotEqual(normal, failed)
        self.assertEqual(failed, expected_handoff(initial, 2))
        self.assertEqual(normal[60000*512:60001*512], bytes([0xa0])*512)
        self.assertEqual(failed[60000*512:60020*512], initial[60000*512:60020*512])
        self.assertEqual(normal[:8*512], initial[:8*512])

    def test_ata_cold_mediator(self):
        suppress_windows_test_dialogs()
        evidence = ROOT / "build/codex-agent/r341-journal-handoff" / ("ata-" + uuid.uuid4().hex)
        evidence.mkdir(parents=True)
        source = (ROOT / "drivers/block/ata.c").read_text(encoding="utf-8")
        (evidence / "ata_bounds.inc").write_text("\n".join(function(source, name) for name in
            ("static bool ata_pio_range_valid(", "static bool ata_pio_read_range_valid(")), encoding="utf-8")
        (evidence / "ata_handoff.inc").write_text("\n".join(function(source, name) for name in
            ("static bool ata_transaction_begin_until(", "int ata_external_journal_handoff(",
             "int ata_external_journal_io(")), encoding="utf-8")
        for opt in ("-O0", "-O2"):
            exe = evidence / (opt + ".exe")
            command = ["gcc", "-std=c11", opt, "-Wall", "-Wextra", "-Werror", "-fno-builtin",
                "-DJOURNAL_HANDOFF_ATA_TEST", "-I", str(ROOT), "-I", str(ROOT / "include"),
                "-I", str(evidence), str(ROOT / "test/journal_handoff_host.c"),
                str(ROOT / "kernel/init/file_object_guard.c"),
                str(ROOT / "kernel/init/critical_object.c"), "-o", str(exe)]
            for index, current in enumerate((command, [str(exe)])):
                result = subprocess.run(current, cwd=ROOT, capture_output=True, text=True,
                    timeout=90 if index == 0 else 30,
                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                (evidence / (opt + ("-compile.log" if index == 0 else "-run.log"))).write_text(
                    result.stdout + result.stderr, encoding="utf-8")
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_native_o0_o2(self):
        suppress_windows_test_dialogs()
        evidence = ROOT / "build/codex-agent/r341-journal-handoff" / ("native-" + uuid.uuid4().hex)
        evidence.mkdir(parents=True)
        failures = []
        for opt in ("-O0", "-O2"):
            exe = evidence / (opt + ".exe")
            command = ["gcc", "-std=c11", opt, "-Wall", "-Wextra", "-Werror",
                       "-fno-builtin", "-I", str(ROOT),
                       "-I", str(ROOT / "include"),
                       str(ROOT / "test/journal_handoff_host.c"),
                       str(ROOT / "kernel/init/file_object_guard.c"),
                       str(ROOT / "userspace/storage/lib/fat32_transaction.c"),
                       str(ROOT / "drivers/block/ata_journal.c"),
                       str(ROOT / "kernel/init/critical_object.c"), "-o", str(exe)]
            for index, current in enumerate((command, [str(exe)])):
                result = subprocess.run(current, cwd=ROOT, capture_output=True, text=True,
                    timeout=90 if index == 0 else 30,
                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                (evidence / (opt + ("-compile.log" if index == 0 else "-run.log"))).write_text(
                    result.stdout + result.stderr, encoding="utf-8")
                if index == 0:
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                elif result.returncode:
                    failures.append(opt + ": " + result.stdout + result.stderr)
                else:
                    print(opt, result.stdout.strip())
        self.assertFalse(failures, "\n".join(failures))


if __name__ == "__main__":
    unittest.main()
