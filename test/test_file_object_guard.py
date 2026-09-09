"""Bounded native behavior proofs for the shared file-object lifetime guard."""
from pathlib import Path
import subprocess
import sys
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from measure_cpp_baseline import suppress_windows_test_dialogs
from test_reist_probe_domain import function


class FileObjectGuardTests(unittest.TestCase):
    def test_async_storage_diagnostics_after_prompt(self):
        from run_qemu_file_object_guard import guard_line_position
        for marker in ("REIST_STORAGE RESOURCE_QUARANTINED 1", "REIST_STORAGE SERVICE_RESTARTED"):
            for prefix in ("", "C:\\>"):
                text = "before\n" + prefix + marker + "\r\nafter\n"
                expected = text.index(marker)
                self.assertEqual(guard_line_position(text, marker), expected)
                self.assertEqual(guard_line_position(text, marker, expected-1), expected)
                self.assertEqual(guard_line_position(text, marker, expected), -1)
            for text in ("noise " + marker + "\n", "C:\\>" + marker + "0\n",
                         "C:\\>" + marker + " trailing\n", "echo " + marker):
                self.assertEqual(guard_line_position(text, marker), -1)
        self.assertEqual(guard_line_position("C:\\>OBJGUARD RESTART_OK\n", "OBJGUARD RESTART_OK"), -1)

    def test_terminating_owner_cannot_redispatch(self):
        # Regression from the repeated R3.38 guest. The scheduler source is
        # extended only after the user-approved cold lifecycle repair scope.
        suppress_windows_test_dialogs()
        evidence = ROOT / "build/codex-agent/r338-file-lifetime" / ("terminate-" + uuid.uuid4().hex)
        evidence.mkdir(parents=True)
        scheduler = (ROOT / "kernel/sched/scheduler.c").read_text(encoding="utf-8")
        header = (ROOT / "kernel/sched/scheduler.h").read_text(encoding="utf-8")
        (evidence / "scheduler_states.inc").write_text(header[header.index("#define MAX_TASKS"):header.index("#define STACK_SIZE")], encoding="utf-8")
        for name, signature in (("claim", "static bool claim_task_for_current_cpu("),
                                ("cancel", "void wait_queue_cancel_locked("),
                                ("wake", "static bool wait_queue_wake_one_task_locked("),
                                ("wake_all", "static size_t wait_queue_wake_all_task_locked("),
                                ("timeout", "void scheduler_wake_expired_waiters_locked("),
                                ("sleepers", "void scheduler_wake_expired_sleepers_locked("),
                                ("reap", "int scheduler_reap_finished_task_locked("),
                                ("reserve", "bool scheduler_reserve_termination_locked("),
                                ("terminate", "void scheduler_terminate_task(")):
            (evidence / ("scheduler_" + name + ".inc")).write_text(function(scheduler, signature), encoding="utf-8")
        process = (ROOT / "kernel/proc/process.c").read_text(encoding="utf-8")
        (evidence / "process_terminate.inc").write_text(
            function(process, "static int process_terminate_generation(") + "\n" +
            function(process, "int process_terminate("), encoding="utf-8")
        failures = []
        for optimization in ("-O0", "-O2"):
            executable = evidence / (optimization + ".exe")
            commands = [["gcc", "-std=c11", optimization, "-Wall", "-Wextra", "-Werror",
                "-DFILE_OBJECT_GUARD_TERMINATE_TEST", "-I", str(evidence), "-I", str(ROOT),
                str(ROOT / "test/file_object_guard_host.c"), str(ROOT / "kernel/sched/wait_queue.c"),
                "-o", str(executable)], [str(executable)]]
            for index, command in enumerate(commands):
                result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                    timeout=90 if index == 0 else 30,
                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                (evidence / (optimization + ("-compile.log" if index == 0 else "-run.log"))).write_text(
                    result.stdout + result.stderr, encoding="utf-8")
                if index == 0:
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                elif result.returncode:
                    failures.append(optimization + ": " + result.stdout + result.stderr)
        self.assertFalse(failures, "\n".join(failures))

    def test_private_floppy_actual_decoders(self):
        from test_native_boot_image import minimal_kernel
        from run_qemu_file_object_guard import create_guard_floppy, guard_failure_marker
        suppress_windows_test_dialogs()
        self.assertEqual(guard_failure_marker("   KERNEL PANIC\r\n"), "KERNEL PANIC")
        self.assertEqual(guard_failure_marker("   KERNEL ASSERTION FAILED\r\n"), "KERNEL ASSERTION FAILED")
        self.assertIsNone(guard_failure_marker("USER FAULT vector=6\n"))
        evidence = ROOT / "build/codex-agent/r338-file-lifetime" / ("floppy-" + uuid.uuid4().hex)
        evidence.mkdir(parents=True)
        disk = evidence / "fat12.img"
        disk.write_bytes(create_guard_floppy(bytes(510) + b"\x55\xaa", bytes(2048),
                                             minimal_kernel(), bytes([1]) * 256))
        for optimization in ("-O0", "-O2"):
            executable = evidence / (optimization + ".exe")
            commands = [["gcc", "-std=c11", optimization, "-Wall", "-Wextra", "-Werror",
                "-fno-builtin", "-DFILE_OBJECT_GUARD_FLOPPY_TEST", "-I", str(ROOT),
                str(ROOT / "test/file_object_guard_host.c"),
                str(ROOT / "fs/fat12/fat12_journal.c"),
                str(ROOT / "fs/fat12/fat12_remap.c"), "-o", str(executable)],
                [str(executable), str(disk)]]
            for index, command in enumerate(commands):
                result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                    timeout=90 if index == 0 else 30,
                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                (evidence / (optimization + ("-compile.log" if index == 0 else "-run.log"))).write_text(
                    result.stdout + result.stderr, encoding="utf-8")
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_private_image_patch_and_shell_layout(self):
        from create_native_boot_image import write_fat32_volume
        from run_qemu_file_object_guard import patch_private_storage
        from verify_file_object_guard_artifacts import program_paths
        from verify_text_artifacts import read_fat_file
        self.assertEqual(program_paths()["OBJGDTST.PRG"], "bin/objgdtst.prg")
        shell = (ROOT / "userspace/bin/shell.c").read_text(encoding="utf-8")
        self.assertIn("/bin/", shell)
        guest = (ROOT / "userspace/programs/objgdtst.c").read_text(encoding="utf-8")
        windows = (ROOT / "scripts/build-windows.ps1").read_text(encoding="utf-8")
        self.assertIn("'sbin/svcctl.prg' = 'SVCCTL.PRG'", windows)
        self.assertIn('x86os_spawnv("/sbin/svcctl.prg"', guest)
        evidence = ROOT / "build/codex-agent/r338-file-lifetime" / ("image-" + uuid.uuid4().hex)
        evidence.mkdir(parents=True)
        path = evidence / "private.img"
        original = b"fixture-old" * 12
        replacement = b"fixture-new" * 12
        with path.open("w+b") as stream:
            stream.truncate((8192 + 70000) * 512)
            write_fat32_volume(stream, 8192, 70000, 123,
                              {"libexec/reist/storage.prg": original})
        with self.assertRaises(ValueError):
            patch_private_storage(path, replacement, b"wrong baseline")
        with self.assertRaises(ValueError):
            patch_private_storage(path, b"x" * 513, original)
        self.assertEqual(read_fat_file(path, "libexec/reist/storage.prg"), original)
        patch_private_storage(path, replacement, original)
        self.assertEqual(read_fat_file(path, "libexec/reist/storage.prg"), replacement)

    def test_ext2_guarded_transactions_o0_o2(self):
        suppress_windows_test_dialogs()
        evidence = ROOT / "build/codex-agent/r338-file-lifetime" / ("ext2-" + uuid.uuid4().hex)
        evidence.mkdir(parents=True)
        for optimization in ("-O0", "-O2"):
            executable = evidence / (optimization + ".exe")
            command = ["gcc", "-std=c11", optimization, "-Wall", "-Wextra", "-Werror",
                "-DFILE_OBJECT_GUARD_EXT2_TEST", "-I", str(ROOT),
                "-I", str(ROOT / "userspace/sdk/include"), str(ROOT / "test/file_object_guard_host.c"),
                str(ROOT / "kernel/init/file_object_guard.c"), str(ROOT / "kernel/init/critical_object.c"),
                str(ROOT / "userspace/storage/lib/vfs_shadow_ext2.c"),
                str(ROOT / "userspace/storage/lib/vfs_symlink_client.c"),
                str(ROOT / "userspace/storage/lib/vfs_path.c"), "-o", str(executable)]
            for index, current in enumerate((command, [str(executable)])):
                result = subprocess.run(current, cwd=ROOT, capture_output=True, text=True,
                    timeout=90 if index == 0 else 30,
                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                (evidence / (optimization + ("-compile.log" if index == 0 else "-run.log"))).write_text(
                    result.stdout + result.stderr, encoding="utf-8")
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("FILE_OBJECT_GUARD_EXT2_OK", result.stdout)
            print(optimization, result.stdout.strip())

    def test_service_object_lifetime_o0_o2(self):
        suppress_windows_test_dialogs()
        evidence = ROOT / "build/codex-agent/r338-file-lifetime" / ("service-" + uuid.uuid4().hex)
        evidence.mkdir(parents=True)
        source = (ROOT / "userspace/programs/storage_service.c").read_text(encoding="utf-8")
        objects = source[source.index("#define VFS_OBJECT_CAPACITY"):source.index("static int vfs_symlink_reserved_zero(")]
        (evidence / "service_objects.inc").write_text(objects, encoding="utf-8")
        for optimization in ("-O0", "-O2"):
            executable = evidence / (optimization + ".exe")
            command = ["gcc", "-std=c11", optimization, "-Wall", "-Wextra", "-Werror",
                "-Wno-unused-function", "-DFILE_OBJECT_GUARD_SERVICE_TEST",
                "-I", str(ROOT), "-I", str(ROOT / "userspace/sdk/include"), "-I", str(evidence),
                str(ROOT / "test/file_object_guard_host.c"), str(ROOT / "kernel/init/file_object_guard.c"),
                str(ROOT / "kernel/init/critical_object.c"),
                str(ROOT / "userspace/storage/lib/vfs_shadow_fat32.c"),
                str(ROOT / "userspace/storage/lib/vfs_shadow_ext2.c"), "-o", str(executable)]
            for index, current in enumerate((command, [str(executable)])):
                result = subprocess.run(current, cwd=ROOT, capture_output=True, text=True,
                    timeout=90 if index == 0 else 30,
                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                (evidence / (optimization + ("-compile.log" if index == 0 else "-run.log"))).write_text(
                    result.stdout + result.stderr, encoding="utf-8")
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("FILE_OBJECT_GUARD_SERVICE_OK", result.stdout)
            print(optimization, result.stdout.strip())

    def test_shared_vfs_admission_o0_o2(self):
        suppress_windows_test_dialogs()
        evidence = ROOT / "build/codex-agent/r338-file-lifetime" / ("vfs-" + uuid.uuid4().hex)
        evidence.mkdir(parents=True)
        syscall = function((ROOT / "kernel/syscall/syscall_table.c").read_text(encoding="utf-8"),
                           "static int syscall_file_object_guard(")
        (evidence / "syscall_guard.inc").write_text(syscall, encoding="utf-8")
        cleanup = function((ROOT / "kernel/proc/process.c").read_text(encoding="utf-8"),
                           "void process_close_all_files(")
        (evidence / "process_cleanup.inc").write_text(cleanup, encoding="utf-8")
        for optimization in ("-O0", "-O2"):
            executable = evidence / (optimization + ".exe")
            command = ["gcc", "-std=c11", optimization, "-Wall", "-Wextra", "-Werror",
                "-fno-builtin", "-DKERNEL_HOST_TEST", "-DFILE_OBJECT_GUARD_VFS_TEST",
                "-I", str(ROOT), "-I", str(evidence), str(ROOT / "test/file_object_guard_host.c"),
                str(ROOT / "fs/vfs/vfs.c"), str(ROOT / "kernel/init/file_object_guard.c"),
                str(ROOT / "kernel/init/critical_object.c"), "-o", str(executable)]
            for index, current in enumerate((command, [str(executable)])):
                result = subprocess.run(current, cwd=ROOT, capture_output=True, text=True,
                    timeout=90 if index == 0 else 30,
                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                (evidence / (optimization + ("-compile.log" if index == 0 else "-run.log"))).write_text(
                    result.stdout + result.stderr, encoding="utf-8")
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("FILE_OBJECT_GUARD_VFS_OK", result.stdout)
            print(optimization, result.stdout.strip())

    def test_metadata_state_machine_o0_o2(self):
        suppress_windows_test_dialogs()
        evidence = ROOT / "build/codex-agent/r338-file-lifetime" / ("host-" + uuid.uuid4().hex)
        evidence.mkdir(parents=True)
        for optimization in ("-O0", "-O2"):
            with self.subTest(optimization=optimization):
                executable = evidence / (optimization + ".exe")
                commands = [
                    ["gcc", "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-pthread",
                     optimization, "-Wall", "-Wextra", "-Werror",
                     "-I", str(ROOT), "-I", str(ROOT / "userspace/sdk/include"),
                     str(ROOT / "test/file_object_guard_host.c"),
                     str(ROOT / "userspace/storage/lib/vfs_shadow_fat32.c"),
                     str(ROOT / "kernel/init/critical_object.c"), "-o", str(executable)],
                    [str(executable)],
                ]
                for index, command in enumerate(commands):
                    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                        timeout=90 if index == 0 else 30,
                        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                    (evidence / (optimization + ("-compile.log" if index == 0 else "-run.log"))).write_text(
                        result.stdout + result.stderr, encoding="utf-8")
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("FILE_OBJECT_GUARD_CORE_OK", result.stdout)
                print(optimization, result.stdout.strip())


if __name__ == "__main__":
    unittest.main()
