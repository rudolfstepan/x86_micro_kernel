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
    NATIVE_SYSTEM_DIRECTORIES,
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

    def test_windows_build_bounds_default_program_parallelism(self):
        windows = self.read("scripts/build-windows.ps1")
        self.assertIn("[int]$SystemBuildJobs = 2", windows)
        self.assertIn("'--jobs', [string]$SystemBuildJobs", windows)
        self.assertIn("ProcessPriorityClass]::BelowNormal", windows)

    def test_real_hardware_build_publishes_installer_only_artifact(self):
        windows = self.read("scripts/build-windows.ps1")
        samsung = self.read("scripts/install-reist-samsung-120gb.cmd")
        fujitsu = self.read("scripts/install-reist-fujitsu-80gb.cmd")
        canonical = "build\\reist-os-real-hw.img"
        self.assertIn("$HardwareInstallImage", windows)
        self.assertIn("if ($Target -eq 'real_hw')", windows)
        self.assertIn("$HardwareInstallPending", windows)
        self.assertIn("validate_boot_manifest.py", windows)
        self.assertIn("Move-Item -LiteralPath $HardwareInstallPending", windows)
        self.assertIn(canonical, samsung)
        self.assertIn(canonical, fujitsu)
        self.assertNotIn("build\\reist-os.img", samsung)
        self.assertNotIn("build\\reist-os.img", fujitsu)

    def test_tree_rejects_noncanonical_and_unbounded_paths(self):
        with self.assertRaisesRegex(ValueError, "lowercase"):
            build_tree({"BIN/shell.prg": b"x"})
        with self.assertRaisesRegex(ValueError, "depth"):
            build_tree({"a/b/c/d/e.prg": b"x"})
        with self.assertRaisesRegex(ValueError, "invalid"):
            build_tree({"bin/../shell.prg": b"x"})
        with self.assertRaisesRegex(ValueError, "collides"):
            build_tree({"bin": b"x", "bin/tool.prg": b"y"})

    def test_tree_supports_bounded_empty_directories(self):
        tree = build_tree({}, NATIVE_SYSTEM_DIRECTORIES)
        trash = next(item for item in tree.directories
                     if item.name == "trash")
        self.assertEqual(
            [item.name for item in trash.directories], ["files", "info"]
        )
        self.assertTrue(all(not item.files for item in trash.directories))
        with self.assertRaisesRegex(ValueError, "lowercase"):
            build_tree({}, ["TRASH/files"])
        with self.assertRaisesRegex(ValueError, "depth"):
            build_tree({}, ["a/b/c/d/e"])
        with self.assertRaisesRegex(ValueError, "collides"):
            build_tree({"trash": b"file"}, ["trash/files"])

    def test_reist_configuration_defaults_are_versioned_and_bounded(self):
        required = {
            "system.conf": "schema=reist.system/1",
            "input.conf": "schema=reist.input/1",
            "desktop.conf": "schema=reist.desktop/1",
            "desktop-layout.conf": "schema=reist.desktop-layout/1",
            "filetypes.conf": "schema=reist.filetypes/1",
            "sounds.conf": "schema=reist.sounds/1",
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
            bytes((index * 31 + 1) & 0xFF for index in range(256)),
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
                "usr/share/sounds/startup.wav": b"startup",
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
        self.assertEqual(root[b"TRASH      "][11], 0x10)
        trash_cluster = struct.unpack_from("<H", root[b"TRASH      "], 26)[0]
        trash = cluster_entries(trash_cluster)
        self.assertEqual(trash[b"FILES      "][11], 0x10)
        self.assertEqual(trash[b"INFO       "][11], 0x10)
        for name in (b"FILES      ", b"INFO       "):
            cluster = struct.unpack_from("<H", trash[name], 26)[0]
            self.assertEqual(
                set(cluster_entries(cluster)), {b".          ", b"..         "}
            )
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
            cluster_entries(sounds_cluster)[b"STARTUP WAV"][12], 0x18
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
        native_builder = "scripts/create_native_boot_image.py"
        self.assertIn(native_builder, makefile)
        self.assertIn(native_builder, windows)
        self.assertEqual(
            NATIVE_SYSTEM_DIRECTORIES, ("trash/files", "trash/info")
        )
        for target in (
            "bin/shell.prg", "bin/ln.prg", "bin/readlink.prg",
            "sbin/svcctl.prg",
            "libexec/reist/storage.prg",
            "sbin/audioinfo.prg", "usr/bin/audiotest.prg",
            "usr/bin/wavplay.prg",
            "usr/gui/bin/soundplayer.prg",
            "usr/gui/bin/imageviewer.prg",
            "usr/gui/bin/control.prg", "usr/gui/bin/browser.prg",
            "sbin/config.prg",
            "usr/share/images/demo-desktop.bmp",
            "usr/share/images/demo-colors.gif",
            "libexec/reist/hda.prg", "libexec/reist/audio.prg",
        ):
            self.assertIn(target, makefile)
            self.assertIn(target, windows)
        for sound in ("startup", "shutdown", "error", "notify",
                      "trash-drop", "trash-empty"):
            self.assertIn(f"usr/share/sounds/{sound}.wav", makefile)
            self.assertIn(f"'{sound}'", windows)
        self.assertIn(
            '"usr/share/sounds/$soundName.wav=$soundPath"', windows
        )
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
            "system.conf", "input.conf", "desktop.conf",
            "desktop-layout.conf", "filetypes.conf", "sounds.conf"
        ):
            self.assertIn(
                f"etc/reist/{config}=config/etc/reist/{config}", makefile
            )
        self.assertIn('"etc/reist/$configFile=$configPath"', windows)
        self.assertIn("usr/gui/bin/notepad.prg", makefile)
        self.assertIn("'usr/gui/bin/notepad.prg'", windows)
        self.assertIn("usr/gui/bin/browser.prg", makefile)
        self.assertIn("'usr/gui/bin/browser.prg'", windows)
        self.assertIn("htdocs/index.html", makefile)
        self.assertIn("'index.html'", windows)
        for font in ("reist-unicode.psf", "reist-jetbrains-mono.psf",
                     "reist-source-code-pro.psf", "reist-iosevka.psf",
                     "reist-fira-code.psf", "catalog.toml"):
            self.assertIn(f"usr/share/fonts/{font}", makefile)
            self.assertIn(f"usr/share/fonts/{font}", windows)
        self.assertIn("usr/gui/bin/soundplayer.prg", makefile)
        self.assertIn("'usr/gui/bin/soundplayer.prg'", windows)
        self.assertIn('{"/DESKTOP.PRG", "/usr/gui/bin/desktop.prg"}', process)
        self.assertIn(
            '{"/usr/bin/desktop.prg", "/usr/gui/bin/desktop.prg"}', process
        )
        self.assertIn('"/sbin/svcctl.prg"', shell)
        self.assertIn("program[length++] = 'p';", shell)

    def test_copy_source_uses_ring3_object_and_runtime_readback(self):
        source = self.read("userspace/programs/copy.c")
        build = self.read("scripts/build_system_programs.py")
        runner = self.read("scripts/run_qemu_system_layout.py")
        self.assertIn('#include "reist/vfs_file_client.h"', source)
        self.assertIn("reist_vfs_file_open_rights(", source)
        self.assertIn(
            "REIST_VFS_FILE_RIGHT_READ | REIST_VFS_FILE_RIGHT_STAT", source)
        self.assertIn("reist_vfs_file_read_bulk(", source)
        self.assertIn("reist_vfs_file_close(source)", source)
        self.assertNotIn("x86os_read(source", source)
        self.assertIn('"COPY.PRG": (', build)
        self.assertIn('("copy /readme.txt /copy-vfs.txt",', runner)
        self.assertIn('("cat /copy-vfs.txt", "REIST OS")', runner)
        self.assertIn('("del /copy-vfs.txt", None)', runner)
        self.assertIn(
            '("stat /copy-vfs.txt", "stat: path not found")', runner)

    def test_runtime_exercises_interactive_basic_object_load(self):
        source = self.read("userspace/bin/basic.c")
        runner = self.read("scripts/run_qemu_system_layout.py")
        self.assertIn('#include "reist/vfs_file_client.h"', source)
        self.assertIn("reist_vfs_file_open_rights(", source)
        self.assertIn("reist_vfs_file_read_bulk(", source)
        load = source[source.index("void cmd_load("):
                      source.index("void cmd_save(")]
        for legacy in ("x86os_stat(", "x86os_open(", "x86os_read(",
                       "x86os_close("):
            self.assertNotIn(legacy, load)

    def test_runtime_exercises_interactive_editor_object_load(self):
        source = self.read("userspace/bin/edit.c")
        build = self.read("scripts/build_system_programs.py")
        runner = self.read("scripts/run_qemu_system_layout.py")
        load = source[source.index("static int load_file("):
                      source.index("static int write_all(")]
        self.assertIn('#include "reist/vfs_file_client.h"', source)
        self.assertIn("reist_vfs_file_open_rights(", load)
        self.assertIn("REIST_VFS_FILE_RIGHT_READ | REIST_VFS_FILE_RIGHT_STAT", load)
        self.assertIn("reist_vfs_file_fstat(", load)
        self.assertIn("reist_vfs_file_read_bulk(", load)
        self.assertIn("EDIT_VFS_LOAD_OK", load)
        for legacy in ("x86os_stat(", "x86os_open(", "x86os_read(",
                       "x86os_close("):
            self.assertNotIn(legacy, load)
        self.assertIn('inject(process, "basic")', runner)
        self.assertIn(
            '("save /vfsload.bas 10 print 1", None)', runner)
        self.assertIn('inject(process, "load /vfsload.bas")', runner)
        self.assertIn('"Loaded 11 bytes successfully."', runner)
        self.assertIn('inject(process, "exit")', runner)
        self.assertIn('"stat /vfsload.bas", "stat: path not found"', runner)
        self.assertIn('"EDIT.PRG": (', build)
        self.assertIn('inject(process, "edit /edit-vfs.txt")', runner)
        self.assertIn('"EDIT_VFS_LOAD_OK"', runner)
        self.assertIn("inject_ctrl_x(process)", runner)
        self.assertIn('"(qemu) " + smoke.SHELL_PROMPT', runner)
        self.assertIn('("cat /edit-vfs.txt", "editor object load")', runner)
        self.assertIn('parser.add_argument("--editor-only"', runner)
        self.assertIn("elif editor_only:", runner)
        self.assertIn("'editor-load'", self.read("scripts/test-reist-runtime.ps1"))

    def test_chkdsk_path_scan_uses_ring3_clients_and_runtime(self):
        source = self.read("userspace/programs/chkdsk.c")
        build = self.read("scripts/build_system_programs.py")
        runner = self.read("scripts/run_qemu_system_layout.py")
        runtime = self.read("scripts/test-reist-runtime.ps1")
        for token in ('#include "reist/vfs_file_client.h"',
                      '#include "reist/vfs_stat_client.h"',
                      '#include "reist/vfs_read_client.h"',
                      "scan_remaining_ms(", "reist_vfs_file_open_rights(",
                      "scan_failure_stage", "CHKDSK: failure stage=",
                      "reist_vfs_file_set_timeout(",
                      "reist_vfs_file_fstat(", "reist_vfs_file_read(",
                      "reist_vfs_readdir_at(", "reist_vfs_stat("):
            self.assertIn(token, source)
        for legacy in ("x86os_stat(", "x86os_open(", "x86os_read(",
                       "x86os_close(", "x86os_readdir_batch("):
            self.assertNotIn(legacy, source)
        self.assertIn('"CHKDSK.PRG": (', build)
        for client in ("vfs_file_client.c", "vfs_stat_client.c",
                       "vfs_read_client.c", "vfs_path.c"):
            self.assertIn(client, build[build.index('"CHKDSK.PRG": ('):])
        self.assertIn(
            '("chkdsk /htdocs", "CHKDSK: read-only check passed")', runner)
        self.assertIn('default=600.0', runner)
        self.assertIn('parser.add_argument("--chkdsk-only"', runner)
        self.assertIn("if chkdsk_only:", runner)
        self.assertIn("chkdsk_failure not in smoke.FAIL_MARKERS", runner)
        self.assertGreaterEqual(
            runner.count("if error is None and not chkdsk_only:"), 3)
        layout = runtime[runtime.index("function Invoke-SystemLayout"):
                         runtime.index("function Invoke-StorageReconnect")]
        self.assertIn("'SYSTEM LAYOUT FAIL'", layout)
        self.assertIn("'SYSTEM LAYOUT PASS'", layout)
        self.assertIn("if ($ChkdskOnly)", layout)
        self.assertIn("'chkdsk-readonly'", runtime)


if __name__ == "__main__":
    unittest.main()
