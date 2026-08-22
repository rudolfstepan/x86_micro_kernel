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

    def test_reist_configuration_defaults_are_versioned_and_bounded(self):
        required = {
            "system.conf": "schema=reist.system/1",
            "input.conf": "schema=reist.input/1",
            "desktop.conf": "schema=reist.desktop/1",
            "filetypes.conf": "schema=reist.filetypes/1",
        }
        for name, schema in required.items():
            text = self.read(f"config/etc/reist/{name}")
            effective = [
                line for line in text.splitlines()
                if line and not line.startswith("#")
            ]
            self.assertEqual(effective[0], schema)
            self.assertLessEqual(len(text.encode("ascii")), 4096)
            keys = [line.split("=", 1)[0] for line in effective]
            self.assertEqual(len(keys), len(set(keys)))
            self.assertTrue(all("=" in line for line in effective))

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
        fat_count = image[16]
        fat_sectors = struct.unpack_from("<H", image, 22)[0]
        root_start = reserved + fat_count * fat_sectors
        root_sectors = 14
        data_start = root_start + root_sectors
        root = image[root_start * 512:data_start * 512]
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
                "sbin/config.prg": b"config",
                "usr/bin/hello.prg": b"hello",
                "usr/gui/bin/desktop.prg": b"desktop",
                "usr/gui/bin/guidemo.prg": b"guidemo",
                "usr/gui/bin/notepad.prg": b"notepad",
                "usr/gui/bin/soundplayer.prg": b"soundplayer",
                "usr/gui/bin/imageviewer.prg": b"imageviewer",
                "usr/gui/bin/control.prg": b"control",
                "usr/share/sounds/440hz.wav": b"wave",
                "usr/share/images/demo-desktop.bmp": b"bmp",
                "usr/share/images/demo-colors.gif": b"gif",
                "usr/share/icons/program.ico": b"ico",
                "etc/reist/input.conf": b"schema=reist.input/1\n",
                "etc/reist/filetypes.conf": b"schema=reist.filetypes/1\n",
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
        self.assertEqual(root[b"ETC        "][12], 0x08)
        sbin_cluster = struct.unpack_from("<H", root[b"SBIN       "], 26)[0]
        sbin = cluster_entries(sbin_cluster)
        self.assertEqual(sbin[b"SVCCTL  PRG"][12], 0x18)
        self.assertEqual(sbin[b"CONFIG  PRG"][12], 0x18)
        usr_cluster = struct.unpack_from("<H", root[b"USR        "], 26)[0]
        usr = cluster_entries(usr_cluster)
        bin_cluster = struct.unpack_from("<H", usr[b"BIN        "], 26)[0]
        self.assertEqual(cluster_entries(bin_cluster)[b"HELLO   PRG"][12], 0x18)
        share_cluster = struct.unpack_from("<H", usr[b"SHARE      "], 26)[0]
        share = cluster_entries(share_cluster)
        sounds_cluster = struct.unpack_from("<H", share[b"SOUNDS     "], 26)[0]
        self.assertEqual(
            cluster_entries(sounds_cluster)[b"440HZ   WAV"][12], 0x18
        )
        gui_cluster = struct.unpack_from("<H", usr[b"GUI        "], 26)[0]
        gui = cluster_entries(gui_cluster)
        gui_bin_cluster = struct.unpack_from("<H", gui[b"BIN        "], 26)[0]
        self.assertEqual(
            cluster_entries(gui_bin_cluster)[b"DESKTOP PRG"][12], 0x18
        )
        self.assertEqual(
            cluster_entries(gui_bin_cluster)[b"GUIDEMO PRG"][12], 0x18
        )
        self.assertEqual(
            cluster_entries(gui_bin_cluster)[b"NOTEPAD PRG"][12], 0x18
        )
        self.assertEqual(
            cluster_entries(gui_bin_cluster)[b"IMAGEV~1PRG"][12], 0x18
        )
        self.assertEqual(
            cluster_entries(gui_bin_cluster)[b"CONTROL PRG"][12], 0x18
        )
        etc_cluster = struct.unpack_from("<H", root[b"ETC        "], 26)[0]
        etc = cluster_entries(etc_cluster)
        reist_cluster = struct.unpack_from("<H", etc[b"REIST      "], 26)[0]
        reist = cluster_entries(reist_cluster)
        input_aliases = [
            entry for name, entry in reist.items()
            if name.startswith(b"INPUT") and name[8:11] == b"CON"
        ]
        self.assertEqual(len(input_aliases), 1)
        self.assertEqual(input_aliases[0][12], 0x18)

    def test_builds_and_runtime_use_only_canonical_targets(self):
        makefile = self.read("Makefile")
        windows = self.read("scripts/build-windows.ps1")
        for target in (
            "bin/shell.prg", "sbin/svcctl.prg",
            "libexec/reist/storage.prg",
            "sbin/audioinfo.prg", "usr/bin/audiotest.prg",
            "usr/bin/wavplay.prg", "usr/share/sounds/440hz.wav",
            "usr/gui/bin/soundplayer.prg",
            "usr/gui/bin/imageviewer.prg",
            "usr/gui/bin/control.prg", "sbin/config.prg",
            "usr/share/images/demo-desktop.bmp",
            "usr/share/images/demo-colors.gif",
            "libexec/reist/hda.prg", "libexec/reist/audio.prg",
        ):
            self.assertIn(target, makefile)
            self.assertIn(target, windows)
        self.assertIn("usr/bin/hello.prg", makefile)
        self.assertIn("usr/bin/$($ProgramName.ToLowerInvariant())", windows)
        self.assertNotIn("--data-file SHELL.PRG=", makefile)
        self.assertNotIn('--data-file "SHELL.PRG=', windows)
        for icon in (
            "folder-empty", "folder-full", "program", "text",
            "audio", "image", "settings", "unknown",
        ):
            self.assertIn(
                f"usr/share/icons/{icon}.ico=assets/icons/{icon}.ico",
                makefile,
            )
            self.assertIn(f"'{icon}'", windows)
        self.assertIn(
            '"usr/share/icons/$iconName.ico=$iconPath"', windows
        )

        process = self.read("kernel/proc/process.c")
        shell = self.read("userspace/bin/shell.c")
        self.assertIn('{"/SVCCTL.PRG", "/sbin/svcctl.prg"}', process)
        self.assertIn('strcmp(resolved, "/sbin/svcctl.prg")', process)
        self.assertIn('"/bin", "/sbin", "/usr/bin", "/usr/gui/bin"', shell)
        self.assertIn("usr/gui/bin/guidemo.prg", makefile)
        self.assertIn("'usr/gui/bin/guidemo.prg'", windows)
        for config in (
            "system.conf", "input.conf", "desktop.conf", "filetypes.conf"
        ):
            self.assertIn(
                f"etc/reist/{config}=config/etc/reist/{config}", makefile
            )
        self.assertIn('"etc/reist/$configFile=$configPath"', windows)
        self.assertIn("usr/gui/bin/notepad.prg", makefile)
        self.assertIn("'usr/gui/bin/notepad.prg'", windows)
        self.assertIn("usr/gui/bin/soundplayer.prg", makefile)
        self.assertIn("'usr/gui/bin/soundplayer.prg'", windows)
        self.assertIn('{"/DESKTOP.PRG", "/usr/gui/bin/desktop.prg"}', process)
        self.assertIn(
            '{"/usr/bin/desktop.prg", "/usr/gui/bin/desktop.prg"}', process
        )
        self.assertIn('"/sbin/svcctl.prg"', shell)
        self.assertIn("program[length++] = 'p';", shell)


if __name__ == "__main__":
    unittest.main()
