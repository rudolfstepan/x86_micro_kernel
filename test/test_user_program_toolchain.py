import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ZIG = shutil.which("zig") or Path(
    r"C:\tmp\zig-0.16.0-portable\zig-x86_64-windows-0.16.0\zig.exe"
)

sys.path.insert(0, str(ROOT))
from scripts import build_user_program as builder  # noqa: E402


def make_elf32(segments, entry, *, runtime_relocations=False):
    """Create the smallest ELF32 fixture accepted by the MYPR converter."""
    phoff = builder.ELF_HEADER.size
    cursor = phoff + len(segments) * builder.ELF_PROGRAM_HEADER.size
    elf = bytearray(cursor)
    program_headers = []

    for virtual_address, data, memory_size, flags in segments:
        file_offset = cursor
        elf.extend(data)
        cursor += len(data)
        program_headers.append((
            1, file_offset, virtual_address, virtual_address,
            len(data), memory_size, flags, 1,
        ))

    section_offset = 0
    section_count = 0
    if runtime_relocations:
        section_offset = cursor
        section_count = 1
        # SHT_REL with one non-empty entry is enough for the converter to
        # reject a runtime-relocatable executable.
        elf.extend(builder.ELF_SECTION_HEADER.pack(
            0, 9, 0, 0, 0, 8, 0, 0, 4, 8
        ))

    ident = b"\x7fELF\x01\x01\x01" + b"\0" * 9
    elf[:builder.ELF_HEADER.size] = builder.ELF_HEADER.pack(
        ident, 2, 3, 1, entry, phoff, section_offset, 0,
        builder.ELF_HEADER.size, builder.ELF_PROGRAM_HEADER.size,
        len(program_headers), builder.ELF_SECTION_HEADER.size,
        section_count, 0,
    )
    for index, program_header in enumerate(program_headers):
        builder.ELF_PROGRAM_HEADER.pack_into(
            elf,
            phoff + index * builder.ELF_PROGRAM_HEADER.size,
            *program_header,
        )
    return bytes(elf)


class MyprV1BuilderContractTests(unittest.TestCase):
    def test_converter_emits_only_the_fixed_address_v1_contract(self):
        payload_address = builder.PAYLOAD_BASE
        elf = make_elf32(
            [(payload_address, b"\x90\xc3", 8, 5)],
            payload_address,
        )

        program = builder.elf_to_mypr(elf)
        identifier, magic, entry, size, base, reloc_offset, reloc_size = \
            builder.PROGRAM_HEADER.unpack_from(program)

        self.assertEqual(identifier, b"MYPR")
        self.assertEqual(magic, 0xDEADBEEF)
        self.assertEqual(base, builder.PROGRAM_BASE)
        self.assertEqual(reloc_size, 0)
        self.assertEqual(reloc_offset, len(program))
        self.assertGreaterEqual(entry, builder.PROGRAM_HEADER.size)
        self.assertLess(entry, reloc_offset)
        self.assertLessEqual(len(program), builder.PROGRAM_HEADER.size + size)

    def test_converter_rejects_runtime_relocations(self):
        elf = make_elf32(
            [(builder.PAYLOAD_BASE, b"\x90", 4, 5)],
            builder.PAYLOAD_BASE,
            runtime_relocations=True,
        )
        with self.assertRaisesRegex(ValueError, "relocations"):
            builder.elf_to_mypr(elf)

    def test_converter_rejects_an_entry_point_in_zero_filled_bss(self):
        elf = make_elf32(
            [(builder.PAYLOAD_BASE, b"\x90", 8, 5)],
            builder.PAYLOAD_BASE + 4,
        )
        with self.assertRaisesRegex(ValueError, "entry point"):
            builder.elf_to_mypr(elf)

    def test_converter_rejects_overlapping_load_segments(self):
        elf = make_elf32(
            [
                (builder.PAYLOAD_BASE, b"AAAA", 16, 5),
                (builder.PAYLOAD_BASE + 8, b"BBBB", 8, 6),
            ],
            builder.PAYLOAD_BASE,
        )
        with self.assertRaisesRegex(ValueError, "overlapping"):
            builder.elf_to_mypr(elf)

    def test_converter_rejects_a_program_linked_outside_the_fixed_region(self):
        wrong_base = builder.PROGRAM_BASE + builder.PROGRAM_REGION_SIZE + 0x1000
        elf = make_elf32([(wrong_base, b"\x90", 4, 5)], wrong_base)
        with self.assertRaisesRegex(ValueError, "8 MiB"):
            builder.elf_to_mypr(elf)

    def test_converter_rejects_a_virtual_address_overflow(self):
        elf = make_elf32([(0xFFFFFFFC, b"\x90", 8, 5)], 0xFFFFFFFC)
        with self.assertRaisesRegex(ValueError, "8 MiB"):
            builder.elf_to_mypr(elf)


@unittest.skipUnless(Path(ZIG).is_file(), "Zig is required for user programs")
class UserProgramToolchainTests(unittest.TestCase):
    def test_userspace_toolchain_uses_release_flags(self):
        source = (ROOT / "scripts/build_user_program.py").read_text(
            encoding="utf-8"
        )
        self.assertIn('"-O2", "-DNDEBUG"', source)
        self.assertIn('"--gc-sections", "--strip-all"', source)

    def test_sdk_uses_standard_compiler_linker_and_archive_interfaces(self):
        program_builder = (
            ROOT / "scripts" / "build_user_program.py"
        ).read_text(encoding="utf-8")
        sdk_builder = (ROOT / "scripts" / "build_user_sdk.py").read_text(
            encoding="utf-8"
        )
        system_builder = (
            ROOT / "scripts" / "build_system_programs.py"
        ).read_text(encoding="utf-8")
        self.assertIn('str(zig), "cc"', program_builder)
        self.assertIn('str(zig), "ld.lld"', program_builder)
        self.assertIn('parser.add_argument("-I"', program_builder)
        self.assertIn('parser.add_argument("-L"', program_builder)
        self.assertIn('parser.add_argument("-l"', program_builder)
        self.assertIn('parser.add_argument("--sysroot"', program_builder)
        self.assertIn('[str(zig), "ar", "rcs"', sdk_builder)
        self.assertIn('"usr" / "include"', sdk_builder)
        self.assertIn('library_dir / "libreistos.a"', sdk_builder)
        self.assertIn('library_dir / "libreistgui.a"', sdk_builder)
        self.assertIn('library_dir / "libreistaudio.a"', sdk_builder)
        self.assertIn("MAX_SYSTEM_BUILD_WORKERS = 8", system_builder)
        self.assertIn("DEFAULT_SYSTEM_BUILD_WORKERS = min(", system_builder)
        self.assertIn('"-j", "--jobs"', system_builder)
        self.assertIn("ThreadPoolExecutor", system_builder)
        self.assertIn("zig-global-shared", system_builder)
        self.assertIn('temporary_path / "zig-local"', program_builder)

    def test_sdk_documentation_matches_the_installed_modular_layout(self):
        documentation = (
            ROOT / "docs/development/USER_PROGRAM_TOOLCHAIN.md"
        ).read_text(encoding="utf-8")
        architecture = (
            ROOT / "docs/architecture/USERSPACE_SDK_AND_PORTABILITY.md"
        ).read_text(encoding="utf-8")
        for artifact in (
            "crt0.o", "libreistos.a", "libreistnetparse.a",
            "libreistgui.a", "libreistaudio.a",
        ):
            self.assertIn(artifact, documentation)
            self.assertIn(artifact, architecture)
        self.assertIn("--sysroot build/sdk -l reistgui", documentation)
        self.assertIn("C11-Quellportabilität", architecture)
        self.assertIn("keine Shared-Library-ABI", architecture)

    def test_installed_gui_sdk_builds_the_documented_external_example(self):
        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory)
            sdk = temporary / "sdk"
            output = temporary / "MENUDEMO.PRG"
            dialog_output = temporary / "DIALOGDEMO.PRG"
            control_output = temporary / "CONTROLDEMO.PRG"
            container_output = temporary / "CONTAINERDEMO.PRG"
            tabs_output = temporary / "TABSDEMO.PRG"
            values_output = temporary / "VALUESDEMO.PRG"
            audio_output = temporary / "AUDIOINFO.PRG"
            subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "scripts" / "build_user_sdk.py"),
                    "--output-dir", str(sdk),
                    "--zig", str(ZIG),
                ],
                cwd=ROOT, check=True, capture_output=True, timeout=60,
            )
            include = sdk / "usr" / "include"
            library = sdk / "usr" / "lib"
            self.assertTrue((include / "x86os.h").is_file())
            self.assertTrue((include / "reist/gui/types.h").is_file())
            self.assertTrue((include / "reist/gui/menu.h").is_file())
            self.assertTrue((include / "reist/gui/dialog.h").is_file())
            self.assertTrue((include / "reist/gui/control.h").is_file())
            self.assertTrue((include / "reist/gui/container.h").is_file())
            self.assertTrue((include / "reist/gui/tabs.h").is_file())
            self.assertTrue(
                (include / "reist/gui/value_controls.h").is_file())
            self.assertTrue((include / "reist/audio.h").is_file())
            self.assertTrue((library / "crt0.o").is_file())
            self.assertEqual(
                (library / "libreistos.a").read_bytes()[:8], b"!<arch>\n"
            )
            self.assertEqual(
                (library / "libreistgui.a").read_bytes()[:8], b"!<arch>\n"
            )
            self.assertEqual(
                (library / "libreistaudio.a").read_bytes()[:8], b"!<arch>\n"
            )
            package = (
                library / "pkgconfig" / "reist-gui.pc"
            ).read_text(encoding="ascii")
            self.assertIn("Cflags: -I${includedir}", package)
            self.assertIn("Libs: -L${libdir} -lreistgui", package)
            audio_package = (
                library / "pkgconfig" / "reist-audio.pc"
            ).read_text(encoding="ascii")
            self.assertIn("Cflags: -I${includedir}", audio_package)
            self.assertIn(
                "Libs: -L${libdir} -lreistaudio -lreistos",
                audio_package,
            )

            stable_artifacts = [
                library / "crt0.o",
                library / "libreistos.a",
                library / "libreistnetparse.a",
                library / "libreistaudio.a",
            ]
            stable_times = {
                artifact: artifact.stat().st_mtime_ns
                for artifact in stable_artifacts
            }
            gui_library = library / "libreistgui.a"
            os.utime(gui_library, ns=(1, 1))
            subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "scripts" / "build_user_sdk.py"),
                    "--output-dir", str(sdk),
                    "--zig", str(ZIG),
                    "--incremental",
                ],
                cwd=ROOT, check=True, capture_output=True, timeout=60,
            )
            self.assertGreater(gui_library.stat().st_mtime_ns, 1)
            self.assertEqual(
                stable_times,
                {artifact: artifact.stat().st_mtime_ns
                 for artifact in stable_artifacts},
            )

            subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "scripts" / "build_user_program.py"),
                    str(ROOT / "userspace/gui/examples/menu_controller.c"),
                    "--output", str(output),
                    "--zig", str(ZIG),
                    "--sysroot", str(sdk),
                    "-l", "reistgui",
                ],
                cwd=ROOT, check=True, capture_output=True, timeout=60,
            )
            self.assertEqual(output.read_bytes()[:4], b"MYPR")
            subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "scripts" / "build_user_program.py"),
                    str(ROOT / "userspace/gui/examples/dialog_controller.c"),
                    "--output", str(dialog_output),
                    "--zig", str(ZIG),
                    "--sysroot", str(sdk),
                    "-l", "reistgui",
                ],
                cwd=ROOT, check=True, capture_output=True, timeout=60,
            )
            self.assertEqual(dialog_output.read_bytes()[:4], b"MYPR")
            subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "scripts" / "build_user_program.py"),
                    str(ROOT / "userspace/gui/examples/basic_controls.c"),
                    "--output", str(control_output),
                    "--zig", str(ZIG),
                    "--sysroot", str(sdk),
                    "-l", "reistgui",
                ],
                cwd=ROOT, check=True, capture_output=True, timeout=60,
            )
            self.assertEqual(control_output.read_bytes()[:4], b"MYPR")
            subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "scripts" / "build_user_program.py"),
                    str(ROOT / "userspace/gui/examples/nested_containers.c"),
                    "--output", str(container_output),
                    "--zig", str(ZIG),
                    "--sysroot", str(sdk),
                    "-l", "reistgui",
                ],
                cwd=ROOT, check=True, capture_output=True, timeout=60,
            )
            self.assertEqual(container_output.read_bytes()[:4], b"MYPR")
            subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "scripts" / "build_user_program.py"),
                    str(ROOT / "userspace/gui/examples/tab_sheet.c"),
                    "--output", str(tabs_output),
                    "--zig", str(ZIG),
                    "--sysroot", str(sdk),
                    "-l", "reistgui",
                ],
                cwd=ROOT, check=True, capture_output=True, timeout=60,
            )
            self.assertEqual(tabs_output.read_bytes()[:4], b"MYPR")
            subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "scripts" / "build_user_program.py"),
                    str(ROOT / "userspace/gui/examples/value_controls.c"),
                    "--output", str(values_output),
                    "--zig", str(ZIG),
                    "--sysroot", str(sdk),
                    "-l", "reistgui",
                ],
                cwd=ROOT, check=True, capture_output=True, timeout=60,
            )
            self.assertEqual(values_output.read_bytes()[:4], b"MYPR")
            subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "scripts" / "build_user_program.py"),
                    str(ROOT / "userspace/programs/audioinfo.c"),
                    "--output", str(audio_output),
                    "--zig", str(ZIG),
                    "--sysroot", str(sdk),
                    "-l", "reistaudio",
                ],
                cwd=ROOT, check=True, capture_output=True, timeout=60,
            )
            self.assertEqual(audio_output.read_bytes()[:4], b"MYPR")

    def test_external_c_source_builds_a_valid_mypr_image(self):
        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory)
            source = temporary / "external.c"
            assembly = temporary / "helper.S"
            output = temporary / "EXTERN.PRG"
            elf = temporary / "external.elf"
            source.write_text(
                '#include "x86os.h"\n'
                'extern unsigned assembly_value(void);\n'
                'static volatile unsigned value = 41;\n'
                'static volatile unsigned zero_value;\n'
                'int main(void) { value++; if (value == 42 && zero_value == 0 '
                '&& assembly_value() == 7) '
                'x86os_puts("USERSPACE-E2E-OK\\n"); return 0; }\n',
                encoding="ascii",
            )
            assembly.write_text(
                '.section .text\n.globl assembly_value\n'
                'assembly_value:\n  movl $7, %eax\n  ret\n',
                encoding="ascii",
            )
            subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "scripts" / "build_user_program.py"),
                    str(source), str(assembly),
                    "--output", str(output),
                    "--elf-output", str(elf),
                    "--zig", str(ZIG),
                ],
                cwd=ROOT,
                check=True,
                capture_output=True,
                timeout=60,
            )

            program = output.read_bytes()
            identifier, magic, entry, size, base, reloc_offset, reloc_size = \
                struct.unpack_from("<4s6I", program)
            self.assertEqual(identifier, b"MYPR")
            self.assertEqual(magic, 0xDEADBEEF)
            self.assertEqual(base, 0x40000000)
            self.assertGreater(size, len(program) - 28)
            self.assertGreaterEqual(entry, 28)
            self.assertLess(entry, 28 + size)
            self.assertEqual(reloc_offset, len(program))
            self.assertLessEqual(reloc_offset, 28 + size)
            self.assertEqual(reloc_offset % 4, 0)
            self.assertEqual(reloc_size, 0)
            self.assertLess(len(program), 28 + size)
            self.assertIn(b"USERSPACE-E2E-OK\n", program)
            self.assertEqual(elf.read_bytes()[:4], b"\x7fELF")

    def test_standard_system_programs_build_as_mypr_images(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "programs"
            subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "scripts" / "build_system_programs.py"),
                    "--output-dir", str(output),
                    "--zig", str(ZIG),
                ],
                cwd=ROOT,
                check=True,
                capture_output=True,
                # This is a correctness build, not a wall-clock benchmark.
                # Windows antivirus and cold temporary Zig caches can create
                # rare >120 s outliers; rebuild selectivity is asserted by
                # the dedicated incremental tests below.
                timeout=240,
            )
            expected = {
                "HELLO.PRG", "SYSINFO.PRG", "USBINFO.PRG", "REPEAT.PRG",
                "CALC.PRG",
                "DATE.PRG", "UPTIME.PRG", "MEMINFO.PRG", "ASCII.PRG",
                "CAT.PRG",
                "CHKDSK.PRG",
                "FDISK.PRG",
                "FORMAT.PRG",
                "LS.PRG",
                "SAVE.PRG",
                "BASIC.PRG",
                "SPAWN.PRG",
                "PS.PRG",
                "KILL.PRG",
                "PWD.PRG",
                "SHELL.PRG",
                "DESKTOP.PRG",
                "GUIDEMO.PRG",
                "NOTEPAD.PRG",
                "SOUNDPLAYER.PRG",
                "MKDIR.PRG",
                "RMDIR.PRG",
                "DEL.PRG",
                "COPY.PRG",
                "RENAME.PRG",
                "STAT.PRG",
                "DF.PRG",
                "TOUCH.PRG",
                "TREE.PRG",
                "FIND.PRG",
                "RM.PRG",
                "ECHO.PRG",
                "CLS.PRG",
                "DRIVES.PRG",
                "DEVCTL.PRG",
                "MOUNT.PRG",
                "UMOUNT.PRG",
                "SVCCTL.PRG",
                "IFCONFIG.PRG",
                "PING.PRG",
                "NETSTAT.PRG",
                "UDP.PRG",
                "NSLOOKUP.PRG",
                "NC.PRG",
                "HTTPD.PRG",
                "EDIT.PRG",
                "CHILDEX.PRG",
                "FAULTDE.PRG",
                "FAULTUD.PRG",
                "FAULTPF.PRG",
                "FAULTSTK.PRG",
                "GTEST.PRG",
                "REIST.PRG",
                "STORAGE.PRG",
                "HDA.PRG",
                "AUDIO.PRG",
                "AUDIOINFO.PRG",
                "AUDIOTEST.PRG",
                "WAVPLAY.PRG",
                "SLEEPER.PRG",
                "SATAWR.PRG",
            }
            self.assertEqual({path.name for path in output.iterdir()}, expected)
            for name in expected:
                program = (output / name).read_bytes()
                self.assertEqual(program[:4], b"MYPR")
                self.assertGreater(len(program), 28)

            original_times = {
                name: (output / name).stat().st_mtime_ns
                for name in expected
            }
            gui_library = output.parent / "sdk/usr/lib/libreistgui.a"
            future = max(original_times.values()) + 2_000_000_000
            os.utime(gui_library, ns=(future, future))
            subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "scripts" / "build_system_programs.py"),
                    "--output-dir", str(output),
                    "--zig", str(ZIG),
                    "--incremental",
                ],
                cwd=ROOT,
                check=True,
                capture_output=True,
                timeout=30,
            )
            rebuilt = {
                name for name in expected
                if (output / name).stat().st_mtime_ns != original_times[name]
            }
            self.assertEqual(
                rebuilt, {"DESKTOP.PRG", "GUIDEMO.PRG", "NOTEPAD.PRG",
                          "SOUNDPLAYER.PRG"}
            )

    def test_drives_reports_versioned_storage_health(self):
        header = (ROOT / "userspace" / "sdk" / "include" / "x86os.h").read_text(
            encoding="utf-8"
        )
        sdk = (ROOT / "userspace" / "sdk" / "x86os.c").read_text(
            encoding="utf-8"
        )
        kernel = (ROOT / "kernel" / "syscall" / "syscall_table.c").read_text(
            encoding="utf-8"
        )
        drives = (ROOT / "userspace" / "programs" / "drives.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("X86OS_SYS_DRIVE_STATUS = 89", header)
        self.assertIn("X86OS_DRIVE_STATUS_VERSION", header)
        self.assertIn("x86os_drive_status", sdk)
        self.assertIn("request.struct_size != sizeof(request)", kernel)
        self.assertIn("storage_service_resource_available(index)", kernel)
        self.assertIn("storage_service_resource_read_only(index)", kernel)
        self.assertIn("storage_service_resource_recovering(index)", kernel)
        self.assertIn("RECOVERING", drives)
        self.assertIn("QUARANTINED", drives)
        self.assertIn("DEGRADED", drives)
        self.assertIn("READONLY", drives)
        self.assertIn("READY", drives)

    def test_editor_is_packaged_in_both_native_images(self):
        build_script = (ROOT / "scripts" / "build-windows.ps1").read_text(
            encoding="utf-8"
        )
        self.assertEqual(build_script.count("'bin/edit.prg'"), 1)

    def test_desktop_is_packaged_in_both_native_images(self):
        build_script = (ROOT / "scripts" / "build-windows.ps1").read_text(
            encoding="utf-8"
        )
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        self.assertEqual(build_script.count("'usr/gui/bin/desktop.prg'"), 1)
        self.assertEqual(makefile.count("usr/gui/bin/desktop.prg="), 1)
        self.assertEqual(build_script.count("'usr/gui/bin/guidemo.prg'"), 1)
        self.assertEqual(makefile.count("usr/gui/bin/guidemo.prg="), 1)
        self.assertNotIn("'usr/bin/desktop.prg' = 'DESKTOP.PRG'", build_script)
        self.assertNotIn("usr/bin/desktop.prg=", makefile)

        shell = (ROOT / "userspace" / "bin" / "shell.c").read_text(
            encoding="utf-8"
        )
        process = (ROOT / "kernel" / "proc" / "process.c").read_text(
            encoding="utf-8"
        )
        self.assertIn('"/usr/gui/bin"', shell)
        self.assertIn("search_path_count = 4U", shell)
        self.assertIn(
            '{"/DESKTOP.PRG", "/usr/gui/bin/desktop.prg"}', process
        )
        self.assertIn(
            '{"/usr/bin/desktop.prg", "/usr/gui/bin/desktop.prg"}', process
        )

    def test_usbinfo_is_packaged_in_both_native_images(self):
        build_script = (ROOT / "scripts" / "build-windows.ps1").read_text(
            encoding="utf-8"
        )
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        self.assertEqual(build_script.count("'sbin/usbinfo.prg'"), 1)
        self.assertEqual(makefile.count("sbin/usbinfo.prg="), 1)

    def test_sata_write_probe_is_bounded_and_packaged(self):
        source = (ROOT / "userspace" / "programs" /
                  "sata_write_test.c").read_text(encoding="utf-8")
        build_script = (ROOT / "scripts" / "build-windows.ps1").read_text(
            encoding="utf-8"
        )
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        self.assertIn("SATA_WRITE_DURATION_MS 10000U", source)
        self.assertIn("SATA_RECONNECT_TIMEOUT_MS 65000U", source)
        self.assertIn("SATA_WRITE_MAX_RECORDS 2048U", source)
        self.assertIn("x86os_monotonic_ms", source)
        self.assertIn("x86os_fsync", source)
        self.assertIn("record_crc32", source)
        self.assertIn("SATA_WRITE RECOVERY_RW_OK", source)
        self.assertIn('x86os_open("/bin/shell.prg")', source)
        self.assertIn("SATA_WRITE TEST_OK", source)
        self.assertEqual(build_script.count("'libexec/reist/satawr.prg'"), 1)
        self.assertEqual(makefile.count("libexec/reist/satawr.prg="), 1)

    def test_fat12_tools_are_packaged_on_floppy_and_native_images(self):
        build_script = (ROOT / "scripts" / "build-windows.ps1").read_text(
            encoding="utf-8"
        )
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        for program in ("chkdsk", "fdisk", "format"):
            self.assertEqual(build_script.count(f"'sbin/{program}.prg'"), 1)
            self.assertEqual(makefile.count(f"sbin/{program}.prg="), 1)
        self.assertEqual(makefile.count("usr/gui/bin/desktop.prg="), 1)

    def test_drives_program_displays_resource_id(self):
        source = (ROOT / "userspace" / "programs" / "drives.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("Resource  Drive  Device  Type", source)
        self.assertIn("x86os_print_number((int)index)", source)


if __name__ == "__main__":
    unittest.main()
