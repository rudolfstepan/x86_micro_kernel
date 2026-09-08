"""Real bounded Ring-3 EXT2 journal behavior, without native error dialogs."""
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from measure_cpp_baseline import suppress_windows_test_dialogs
import run_qemu_ext2_commit_recovery as guest


class Ext2CommitRecoveryTests(unittest.TestCase):
    def test_guest_fixtures_and_independent_disk_verifier(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "coherent.img"
            initial = guest.create_committed_disk(path, True)
            with self.assertRaises(ValueError):
                guest.create_committed_disk(path, True)
            with self.assertRaises(ValueError):
                guest.verify_disk(initial, initial, True)  # no cleanup yet
            final = bytearray(initial)
            for offset in (guest.HEADER, guest.HEADER + 512):
                header = bytearray(final[offset:offset + 512])
                guest.ext2.put32(header, 8, 0)
                guest.ext2.put32(header, 16, 0)
                header[24:] = b"\0" * (512 - 24)
                guest.ext2.put32(header, 24, guest.zlib.crc32(header) & 0xFFFFFFFF)
                final[offset:offset + 512] = header
            guest.verify_disk(initial, bytes(final), True)
            final[guest.TARGET + 35] ^= 1
            with self.assertRaises(ValueError):
                guest.verify_disk(initial, bytes(final), True)
            corrupt = guest.create_committed_disk(path.with_name("corrupt.img"), False)
            guest.verify_disk(corrupt, corrupt, False)
            with self.assertRaises(ValueError):
                guest.verify_disk(corrupt, initial, False)

    def test_actual_journal_terminal_states_o0_o2(self):
        suppress_windows_test_dialogs()
        with tempfile.TemporaryDirectory() as directory:
            for optimization in ("-O0", "-O2"):
                with self.subTest(optimization=optimization):
                    executable = pathlib.Path(directory) / (optimization + ".exe")
                    flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
                    subprocess.run([
                        "gcc", "-std=c11", optimization, "-Wall", "-Wextra", "-Werror",
                        f"-I{ROOT}", f"-I{ROOT / 'userspace/sdk/include'}",
                        str(ROOT / "test/test_ext2_commit_recovery_host.c"),
                        str(ROOT / "userspace/storage/lib/vfs_shadow_ext2.c"),
                        str(ROOT / "userspace/storage/lib/vfs_symlink_client.c"),
                        str(ROOT / "userspace/storage/lib/vfs_path.c"),
                        "-o", str(executable),
                    ], check=True, cwd=ROOT, timeout=90, creationflags=flags)
                    completed = subprocess.run([str(executable)], cwd=ROOT,
                        timeout=30, creationflags=flags, capture_output=True, text=True)
                    self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
                    self.assertIn("EXT2_COMMIT_HOST_OK", completed.stdout)
                    print(optimization, completed.stdout.strip())


if __name__ == "__main__":
    unittest.main()
