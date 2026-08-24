import importlib.util
import pathlib
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts/run_qemu_ext2_stat.py"
SPEC = importlib.util.spec_from_file_location("run_qemu_ext2_stat", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class QemuExt2StatTests(unittest.TestCase):
    def test_fixture_has_ext2_superblock_inode_and_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            image = pathlib.Path(directory) / "ext2.img"
            MODULE.create_ext2_image(image)
            data = image.read_bytes()
        self.assertEqual(len(data), MODULE.BLOCKS * MODULE.BLOCK_SIZE)
        self.assertEqual(int.from_bytes(data[1080:1082], "little"), 0xEF53)
        self.assertEqual(int.from_bytes(data[2048 + 8:2048 + 12], "little"), 5)
        self.assertIn(b"readme.txt", data[21 * 1024:22 * 1024])
        self.assertEqual(data[22 * 1024:22 * 1024 + 15], b"EXT2 AUTHORITY\n")

    def test_qemu_uses_second_read_only_snapshot_disk(self):
        command = MODULE.qemu_command(
            pathlib.Path("qemu-system-i386"), pathlib.Path("system.img"),
            pathlib.Path("ext2.img"))
        joined = " ".join(str(item) for item in command)
        self.assertIn("if=ide,index=0", joined)
        self.assertIn("if=ide,index=1", joined)
        self.assertIn("-snapshot", command)
        self.assertEqual(MODULE.STAT_COMMAND, "stat /mnt/hdd1/readme.txt")
        self.assertEqual(MODULE.CAT_COMMAND, "cat /mnt/hdd1/readme.txt")
        self.assertEqual(MODULE.LS_COMMAND, "ls /mnt/hdd1")

    def test_runner_requires_metadata_and_shell_return(self):
        source = SCRIPT.read_text(encoding="utf-8")
        for marker in ("Name: readme.txt", "Size: 15 bytes",
                       "CAT_TEXT", "LS_NAME", "smoke.SHELL_PROMPT",
                       "EXT2 STAT PASS"):
            self.assertIn(marker, source)
        self.assertIn("smoke.BOOT_MARKER", source)
        self.assertNotIn("smoke.TEST_MARKER", source)


if __name__ == "__main__":
    unittest.main()
