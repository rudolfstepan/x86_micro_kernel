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
                timeout=120,
            )
            expected = {
                "HELLO.PRG", "SYSINFO.PRG", "REPEAT.PRG", "CALC.PRG",
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
                "MKDIR.PRG",
                "RMDIR.PRG",
                "DEL.PRG",
                "COPY.PRG",
                "ECHO.PRG",
                "CLS.PRG",
                "DRIVES.PRG",
                "EDIT.PRG",
                "CHILDEX.PRG",
                "FAULTDE.PRG",
                "FAULTUD.PRG",
                "FAULTPF.PRG",
                "FAULTSTK.PRG",
                "GTEST.PRG",
                "REIST.PRG",
                "STORAGE.PRG",
                "SLEEPER.PRG",
                "SATAWR.PRG",
            }
            self.assertEqual({path.name for path in output.iterdir()}, expected)
            for name in expected:
                program = (output / name).read_bytes()
                self.assertEqual(program[:4], b"MYPR")
                self.assertGreater(len(program), 28)

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
        drives = (ROOT / "examples" / "userspace" / "drives.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("X86OS_SYS_DRIVE_STATUS = 89", header)
        self.assertIn("X86OS_DRIVE_STATUS_VERSION", header)
        self.assertIn("x86os_drive_status", sdk)
        self.assertIn("request.struct_size != sizeof(request)", kernel)
        self.assertIn("storage_service_resource_available(health_resource)", kernel)
        self.assertIn("storage_service_resource_read_only(health_resource)", kernel)
        self.assertIn("storage_service_resource_recovering(health_resource)", kernel)
        self.assertIn("health_resource = detected_drives[index].parent_resource", kernel)
        self.assertIn("RECOVERING", drives)
        self.assertIn("QUARANTINED", drives)
        self.assertIn("DEGRADED", drives)
        self.assertIn("READONLY", drives)
        self.assertIn("READY", drives)

    def test_editor_is_packaged_in_both_native_images(self):
        build_script = (ROOT / "scripts" / "build-windows.ps1").read_text(
            encoding="utf-8"
        )
        self.assertEqual(build_script.count('--data-file "EDIT.PRG='), 2)

    def test_desktop_is_packaged_in_both_native_images(self):
        build_script = (ROOT / "scripts" / "build-windows.ps1").read_text(
            encoding="utf-8"
        )
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        self.assertEqual(build_script.count('--data-file "DESKTOP.PRG='), 2)

    def test_sata_write_probe_is_bounded_and_packaged(self):
        source = (ROOT / "examples" / "userspace" /
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
        self.assertIn('x86os_open("/SHELL.PRG")', source)
        self.assertIn("SATA_WRITE TEST_OK", source)
        self.assertEqual(build_script.count('--data-file "SATAWR.PRG='), 2)
        self.assertEqual(makefile.count("--data-file SATAWR.PRG="), 2)

    def test_fat12_tools_are_packaged_on_floppy_and_native_images(self):
        build_script = (ROOT / "scripts" / "build-windows.ps1").read_text(
            encoding="utf-8"
        )
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        for program in ("CHKDSK.PRG", "FDISK.PRG", "FORMAT.PRG"):
            self.assertEqual(
                build_script.count(f'--data-file "{program}='), 2
            )
            self.assertEqual(makefile.count(f"--data-file {program}="), 2)
        self.assertEqual(makefile.count("--data-file DESKTOP.PRG="), 2)

    def test_drives_program_displays_resource_id(self):
        source = (ROOT / "examples" / "userspace" / "drives.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("Resource  Drive  Device  Type", source)
        self.assertIn("x86os_print_number((int)index)", source)


if __name__ == "__main__":
    unittest.main()
