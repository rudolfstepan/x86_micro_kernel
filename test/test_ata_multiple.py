"""Compile production ATA PIO transaction functions against a port/time model."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from build_user_program import find_zig


class AtaMultipleTests(unittest.TestCase):
    def test_production_transactions(self):
        source = (ROOT / "drivers/block/ata.c").read_text(encoding="utf-8")
        begin = source.index("static bool ata_pio_wait_status(")
        end = source.index("bool ata_read_sectors(", begin)
        implementation = source[begin:end]
        harness = (ROOT / "test/test_ata_multiple_host.c").read_text(encoding="utf-8")
        self.assertEqual(harness.count("/* PRODUCTION */"), 1)
        with tempfile.TemporaryDirectory(prefix="reist-ata-multiple-") as directory:
            tmp = Path(directory)
            c = tmp / "host.c"
            c.write_text(harness.replace("/* PRODUCTION */", implementation), encoding="utf-8")
            executable = tmp / "host.exe"
            env = os.environ.copy()
            env["ZIG_GLOBAL_CACHE_DIR"] = str(ROOT / "build/codex-agent/ata-host-cache")
            env["ZIG_LOCAL_CACHE_DIR"] = str(tmp / "cache")
            build = subprocess.run([str(find_zig()), "cc", "-std=c11", "-O1",
                                    "-I" + str(ROOT), str(c), "-o", str(executable)],
                                   capture_output=True, text=True, env=env, timeout=60)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True, timeout=5)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("ATA_MULTIPLE_HOST_OK", run.stdout)


if __name__ == "__main__":
    unittest.main()
