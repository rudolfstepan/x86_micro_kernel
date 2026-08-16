"""Deterministic FAT12 persistence-barrier fault matrix."""

from hashlib import sha256
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
GCC = shutil.which("gcc")
REFERENCE_IMAGE = ROOT / "build" / "reist-os-floppy.img"


def image_digest() -> str | None:
    if not REFERENCE_IMAGE.is_file():
        return None
    return sha256(REFERENCE_IMAGE.read_bytes()).hexdigest()


@unittest.skipUnless(GCC, "gcc is required for the FAT12 fault matrix")
class Fat12FaultMatrixTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.reference_digest = image_digest()

    @classmethod
    def tearDownClass(cls) -> None:
        if (cls.reference_digest is not None and
                image_digest() != cls.reference_digest):
            raise AssertionError(
                "the FAT12 fault matrix modified the reference image"
            )

    def build_and_run(self, name: str, module: str) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / (
                name + (".exe" if os.name == "nt" else "")
            )
            subprocess.run(
                [
                    GCC,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-fno-builtin",
                    "-DKERNEL_HOST_TEST",
                    f"-I{ROOT}",
                    str(ROOT / "test" / f"{name}.c"),
                    str(ROOT / "fs" / "fat12" / module),
                    str(ROOT / "lib" / "libc" / "string.c"),
                    "-o",
                    str(executable),
                ],
                check=True,
                cwd=ROOT,
                capture_output=True,
            )
            subprocess.run(
                [str(executable)], check=True, cwd=ROOT,
                capture_output=True, timeout=10,
            )

    def test_journal_barriers(self) -> None:
        self.build_and_run("test_fat12_journal_host", "fat12_journal.c")

    def test_remap_barriers(self) -> None:
        self.build_and_run("test_fat12_remap_host", "fat12_remap.c")

    def test_replica_barriers(self) -> None:
        self.build_and_run("test_fat12_replica_host", "fat12_replica.c")


if __name__ == "__main__":
    unittest.main()
