import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class ReistStorageHandoverTests(unittest.TestCase):
    def test_real_volume_fingerprint_and_output_gate_host(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = pathlib.Path(directory) / "storage-handover-test.exe"
            subprocess.run([
                "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-DREIST_HOST_TEST", "-I", str(ROOT),
                str(ROOT / "test/test_storage_handover_host.c"),
                str(ROOT / "kernel/init/storage_handover.c"),
                "-o", str(executable),
            ], check=True, timeout=20)
            subprocess.run([str(executable)], check=True, timeout=10)

    def test_standby_hold_precedes_mount_and_release_follows_takeover(self):
        kernel = (ROOT / "kernel/init/kernel.c").read_text(encoding="utf-8")
        hold = kernel.index("storage_handover_hold()")
        mount = kernel.index("auto_mount_all_drives()")
        takeover = kernel.index("REIST_HANDOVER TAKEOVER_OK")
        send = kernel.index("handover_serial_send_state(&state)", takeover)
        release = kernel.index("storage_handover_release(&state)", send)
        marker = kernel.index("REIST_HANDOVER STORAGE_OUTPUT_RELEASED", release)
        self.assertLess(hold, mount)
        self.assertLess(takeover, send)
        self.assertLess(send, release)
        self.assertLess(release, marker)
        self.assertIn("storage_handover_is_held()", kernel)

    def test_every_supervised_write_obeys_the_handover_gate(self):
        safety = (ROOT / "kernel/init/storage_safety.c").read_text(
            encoding="utf-8")
        begin = safety[safety.index("bool storage_write_begin"):
                       safety.index("bool storage_write_end")]
        self.assertIn("storage_handover_is_held()", begin)
        handover = (ROOT / "kernel/init/storage_handover.c").read_text(
            encoding="utf-8")
        self.assertIn("ata_read_sector_fresh", handover)
        ata = (ROOT / "drivers/block/ata.c").read_text(encoding="utf-8")
        fresh = ata[ata.index("bool ata_read_sector_fresh"):
                    ata.index("void ata_reset_error_counter")]
        self.assertLess(fresh.index("cached->valid = false"),
                        fresh.index("ata_read_sector_impl"))
        ata = (ROOT / "drivers/block/ata.c").read_text(encoding="utf-8")
        fdd = (ROOT / "drivers/block/fdd.c").read_text(encoding="utf-8")
        self.assertIn("storage_write_begin(pit_monotonic_ms())", ata)
        self.assertIn("storage_write_begin(pit_monotonic_ms())", fdd)


if __name__ == "__main__":
    unittest.main()
