"""Bounded real FAT32 journal regression; no native crash dialogs."""
import pathlib
import subprocess
import sys
import tempfile
import time
import unittest
import uuid

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from measure_cpp_baseline import suppress_windows_test_dialogs


class Fat32RecoveryAdmissionTests(unittest.TestCase):
    def test_guest_shell_start_accepts_only_exact_serial_records(self):
        import run_qemu_fat32_recovery_admission as guest
        shell = "REIST OS userspace shell"
        kernel = "Starting userspace command interpreter"
        for text in (shell+"\n", kernel+"\n"+shell+"\r\n",
                     kernel+shell+"\n", "BOOT_OK\n"+kernel+shell+"\r\nC:\\>"):
            self.assertTrue(guest.shell_started(text), repr(text))
        for text in ("", kernel+"\n", shell, kernel+shell,
                     "noise "+shell+"\n", "noise "+kernel+shell+"\n",
                     kernel+" "+shell+"\n", shell+" suffix\n",
                     kernel+shell+" suffix\n", shell.replace("userspace", "rescue")+"\n"):
            self.assertFalse(guest.shell_started(text), repr(text))

    def test_guest_fixtures_and_whole_disk_oracle(self):
        import run_qemu_fat32_recovery_admission as guest
        self.assertEqual(guest.MOUNT, guest.transport.MOUNT)
        with tempfile.TemporaryDirectory() as temporary:
            for case in guest.CASES:
                with self.subTest(case=case):
                    path = pathlib.Path(temporary) / (case + ".img")
                    initial = guest.create_disk(path, case)
                    with self.assertRaises(ValueError):
                        guest.create_disk(path, case)
                    clusters = guest.TOTAL_SECTORS - guest.DATA_START
                    self.assertGreaterEqual(clusters, 65525)
                    self.assertGreaterEqual(512*512//4, clusters+2)
                    expected = guest.expected_disk(initial, case)
                    guest.verify_disk(initial, expected, case)
                    if case in ("v1", "v2"):
                        with self.assertRaises(ValueError):
                            guest.verify_disk(initial, initial, case)
                    for offset in (0, 6*512, 8*512+20, 31*512+20, len(expected)-1):
                        corrupted = bytearray(expected)
                        corrupted[offset] ^= 1
                        with self.assertRaises(ValueError):
                            guest.verify_disk(initial, bytes(corrupted), case)

    def test_guest_command_rejects_false_positive(self):
        import run_qemu_fat32_recovery_admission as guest
        required = ("ADMIN MOUNT_FAILED",)
        guest.validate_command("ADMIN MOUNT_FAILED\n", required, ("ADMIN MOUNT_OK",))
        for output in ("", "prefix ADMIN MOUNT_FAILED\n", "ADMIN MOUNT_FAILED_suffix\n",
                       "ADMIN MOUNT_FAILED\nADMIN MOUNT_OK resource=1\n"):
            with self.assertRaises(ValueError):
                guest.validate_command(output, required, ("ADMIN MOUNT_OK",))
        guest.validate_command(guest.RESTART + " generation=3\n", (guest.RESTART,), ())
        for output in (guest.RESTART, guest.RESTART + " generation=0",
                       guest.RESTART + " generation=4294967296",
                       guest.RESTART + " generation=3 extra",
                       "COMPONENT RESTART_OK component=6 generation=3"):
            with self.assertRaises(ValueError):
                guest.validate_command(output, (guest.RESTART,), ())

    def test_actual_core_o0_o2(self):
        suppress_windows_test_dialogs()
        evidence = ROOT / "build/codex-agent/r340-fat32-recovery" / ("native-" + uuid.uuid4().hex)
        evidence.mkdir(parents=True)
        for optimization in ("-O0", "-O2"):
            with self.subTest(optimization=optimization):
                executable = evidence / (optimization + ".exe")
                flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
                started = time.monotonic()
                subprocess.run(["gcc", "-std=c11", optimization, "-Wall", "-Wextra",
                    "-Werror", "-fno-builtin", f"-I{ROOT}",
                    str(ROOT / "test/fat32_recovery_admission_host.c"),
                    str(ROOT / "drivers/block/ata_journal.c"), "-o", str(executable)],
                    check=True, cwd=ROOT, timeout=90, creationflags=flags)
                result = subprocess.run([str(executable)], cwd=ROOT, timeout=30,
                    creationflags=flags, capture_output=True, text=True)
                (evidence / (optimization + ".log")).write_text(result.stdout + result.stderr, encoding="utf-8")
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("failures=0", result.stdout)
                print(optimization, result.stdout.strip(), f"{time.monotonic() - started:.3f}s")


if __name__ == "__main__":
    unittest.main()
