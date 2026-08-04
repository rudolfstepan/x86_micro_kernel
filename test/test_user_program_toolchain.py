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


@unittest.skipUnless(Path(ZIG).is_file(), "Zig is required for user programs")
class UserProgramToolchainTests(unittest.TestCase):
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
            self.assertEqual(size, len(program) - 28)
            self.assertGreaterEqual(entry, 28)
            self.assertLess(entry, 28 + size)
            self.assertEqual(reloc_offset, len(program))
            self.assertEqual(reloc_offset % 4, 0)
            self.assertEqual(reloc_size, 0)
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
            }
            self.assertEqual({path.name for path in output.iterdir()}, expected)
            for name in expected:
                program = (output / name).read_bytes()
                self.assertEqual(program[:4], b"MYPR")
                self.assertGreater(len(program), 28)


if __name__ == "__main__":
    unittest.main()
