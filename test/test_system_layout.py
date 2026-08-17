import io
import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from scripts.create_floppy_boot_image import create_floppy_image
from scripts.create_native_boot_image import (
    DATA_PARTITION_START,
    IMAGE_SIZE,
    write_fat32_volume,
)
from scripts.fat_image_tree import build_tree
from test_native_boot_image import minimal_kernel


def directory_entries(data: bytes):
    result = {}
    for offset in range(0, len(data), 32):
        entry = data[offset:offset + 32]
        if entry[0] == 0:
            break
        if entry[0] == 0xE5 or entry[11] == 0x0F or entry[11] & 0x08:
            continue
        result[entry[0:11]] = entry
    return result


class SystemLayoutContracts(unittest.TestCase):
    def read(self, relative: str) -> str:
        return (ROOT / relative).read_text(encoding="utf-8")

    def test_tree_rejects_noncanonical_and_unbounded_paths(self):
        with self.assertRaisesRegex(ValueError, "lowercase"):
            build_tree({"BIN/shell.prg": b"x"})
        with self.assertRaisesRegex(ValueError, "depth"):
            build_tree({"a/b/c/d/e.prg": b"x"})
        with self.assertRaisesRegex(ValueError, "invalid"):
            build_tree({"bin/../shell.prg": b"x"})
        with self.assertRaisesRegex(ValueError, "collides"):
            build_tree({"bin": b"x", "bin/tool.prg": b"y"})

    def test_floppy_contains_lowercase_nested_system_tree(self):
        stage1 = bytes(510) + b"\x55\xaa"
        image = create_floppy_image(
            stage1,
            bytes(2048),
            minimal_kernel(),
            {
                "bin/shell.prg": b"shell",
                "libexec/reist/storage.prg": b"storage",
            },
        )
        reserved = struct.unpack_from("<H", image, 14)[0]
        root_sectors = 14
        data_start = reserved + 6 + root_sectors
        root = image[(reserved + 6) * 512:data_start * 512]
        root_entries = directory_entries(root)
        self.assertEqual(root_entries[b"BIN        "][12], 0x08)
        self.assertEqual(root_entries[b"LIBEXEC    "][12], 0x08)

        def cluster_entries(entry):
            cluster = struct.unpack_from("<H", entry, 26)[0]
            offset = (data_start + cluster - 2) * 512
            return directory_entries(image[offset:offset + 512])

        bin_entries = cluster_entries(root_entries[b"BIN        "])
        self.assertEqual(bin_entries[b"SHELL   PRG"][12], 0x18)
        libexec_entries = cluster_entries(root_entries[b"LIBEXEC    "])
        reist_entries = cluster_entries(libexec_entries[b"REIST      "])
        self.assertEqual(reist_entries[b"STORAGE PRG"][12], 0x18)

    def test_fat32_contains_lowercase_nested_system_tree(self):
        image = io.BytesIO(bytes(IMAGE_SIZE))
        total = IMAGE_SIZE // 512 - DATA_PARTITION_START
        write_fat32_volume(
            image,
            DATA_PARTITION_START,
            total,
            0x12345678,
            {
                "sbin/svcctl.prg": b"svcctl",
                "usr/bin/hello.prg": b"hello",
            },
        )
        image.seek(DATA_PARTITION_START * 512)
        boot = image.read(512)
        reserved = struct.unpack_from("<H", boot, 14)[0]
        fat_count = boot[16]
        fat_sectors = struct.unpack_from("<I", boot, 36)[0]
        data_start = reserved + fat_count * fat_sectors

        def cluster_entries(cluster):
            sector = DATA_PARTITION_START + data_start + cluster - 2
            image.seek(sector * 512)
            return directory_entries(image.read(512))

        root = cluster_entries(struct.unpack_from("<I", boot, 44)[0])
        self.assertEqual(root[b"SBIN       "][12], 0x08)
        self.assertEqual(root[b"USR        "][12], 0x08)
        sbin_cluster = struct.unpack_from("<H", root[b"SBIN       "], 26)[0]
        sbin = cluster_entries(sbin_cluster)
        self.assertEqual(sbin[b"SVCCTL  PRG"][12], 0x18)
        usr_cluster = struct.unpack_from("<H", root[b"USR        "], 26)[0]
        usr = cluster_entries(usr_cluster)
        bin_cluster = struct.unpack_from("<H", usr[b"BIN        "], 26)[0]
        self.assertEqual(cluster_entries(bin_cluster)[b"HELLO   PRG"][12], 0x18)

    def test_builds_and_runtime_use_only_canonical_targets(self):
        makefile = self.read("Makefile")
        windows = self.read("scripts/build-windows.ps1")
        for target in (
            "bin/shell.prg", "sbin/svcctl.prg",
            "libexec/reist/storage.prg",
        ):
            self.assertIn(target, makefile)
            self.assertIn(target, windows)
        self.assertIn("usr/bin/hello.prg", makefile)
        self.assertIn("usr/bin/$($ProgramName.ToLowerInvariant())", windows)
        self.assertNotIn("--data-file SHELL.PRG=", makefile)
        self.assertNotIn('--data-file "SHELL.PRG=', windows)

        process = self.read("kernel/proc/process.c")
        shell = self.read("userspace/bin/shell.c")
        self.assertIn('{"/SVCCTL.PRG", "/sbin/svcctl.prg"}', process)
        self.assertIn('strcmp(resolved, "/sbin/svcctl.prg")', process)
        self.assertIn('"/bin", "/sbin", "/usr/bin"', shell)
        self.assertIn('"/sbin/svcctl.prg"', shell)
        self.assertIn("program[length++] = 'p';", shell)


if __name__ == "__main__":
    unittest.main()
