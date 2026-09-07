"""Link actual i386 layout; exercise production PMM with relocated boot symbols."""
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from build_user_program import find_zig
from create_native_boot_image import validate_elf32, MAX_LOAD_ADDRESS
from measure_cpp_baseline import suppress_windows_test_dialogs


def elf_sections(data):
    header = struct.unpack_from('<16sHHIIIIIHHHHHH', data)
    return [struct.unpack_from('<IIIIIIIIII', data, header[6] + i * header[11])
            for i in range(header[12])]


def elf_symbols(data):
    sections = elf_sections(data)
    symbols = {}
    for section in sections:
        if section[1] != 2:
            continue
        strings = sections[section[6]]
        names = data[strings[4]:strings[4] + strings[5]]
        for offset in range(section[4], section[4] + section[5], section[9]):
            name, value, size, _, _, _ = struct.unpack_from('<IIIBBH', data, offset)
            name = names[name:names.index(b'\0', name)].decode()
            symbols[name] = (value, size)
    return symbols


class KernelMemoryLayoutTests(unittest.TestCase):
    def setUp(self):
        suppress_windows_test_dialogs()
        self.env = os.environ.copy()
        self.env['ZIG_GLOBAL_CACHE_DIR'] = str(ROOT / 'build/zig-global-cache')
        self.env['ZIG_LOCAL_CACHE_DIR'] = str(ROOT / 'build/zig-cache')
        self.zig = str(find_zig())

    def invoke(self, args, timeout=30):
        return subprocess.run(args, cwd=ROOT, env=self.env, capture_output=True,
            text=True, timeout=timeout,
            creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))

    def link(self, directory, bss_bytes, legacy=None):
        # Actual graphics arrays plus a bounded model of the other kernel BSS.
        framebuffer = (ROOT / 'drivers/video/framebuffer.c').read_text()
        storage = framebuffer[framebuffer.index('static uint8_t framebuffer_shadow'):
                              framebuffer.index('static uint8_t* fb_address')]
        source = directory / 'fixture.c'
        source.write_text('#include <stdint.h>\n#include "drivers/video/framebuffer.h"\n' + storage +
            f'unsigned char other_bss[{bss_bytes}];\n' +
            'void start(void) { framebuffer_shadow[0] = framebuffer_staged_blit[0]; }\n' +
            (f'__attribute__((section("{legacy}"))) char legacy_byte = 1;\n' if legacy else ''),
            encoding='utf-8')
        obj = directory / 'fixture.o'
        result = self.invoke([self.zig, 'cc', '-target', 'x86-freestanding', '-O0',
            '-ffreestanding', '-fno-sanitize=all', '-fno-unwind-tables', '-fno-asynchronous-unwind-tables',
            '-fno-stack-protector', '-I.', '-Iinclude', '-c', str(source), '-o', str(obj)])
        self.assertEqual(result.returncode, 0, result.stderr)
        output = directory / 'fixture.elf'
        result = self.invoke([self.zig, 'ld.lld', '-m', 'elf_i386', '--build-id=sha1',
            '-T', 'config/klink.ld', str(obj), '-o', str(output)], timeout=10)
        return result, output

    def test_real_graphics_buffers_fit_loader_and_reservation_envelope(self):
        with tempfile.TemporaryDirectory(prefix='reist-layout-') as tmp:
            result, output = self.link(Path(tmp), 18 * 1024 * 1024)
            self.assertEqual(result.returncode, 0, result.stderr)
            data = output.read_bytes()
            entry, _ = validate_elf32(data)
            symbols = elf_symbols(data)
            self.assertEqual(symbols['start'][0], entry)
            ranges = []
            for name in ('framebuffer_shadow', 'framebuffer_staged_blit'):
                address, size = symbols[name]
                self.assertEqual(size, 16 * 1024 * 1024)
                ranges.append((address, address + size))
            ranges.sort()
            self.assertLessEqual(ranges[0][1], ranges[1][0])
            guard = symbols['_stack_guard_start'][0]
            self.assertEqual(guard % 4096, 0)
            self.assertGreaterEqual(guard, max(end for _, end in ranges))
            self.assertEqual(symbols['_stack_guard_end'][0] - guard, 4096)
            self.assertEqual(symbols['_stack_start'][0], guard + 4096)
            self.assertEqual(symbols['_stack_end'][0] - symbols['_stack_start'][0], 8192)
            self.assertEqual(symbols['_kernel_end'][0], symbols['_stack_end'][0])
            self.assertLessEqual(symbols['_kernel_end'][0], MAX_LOAD_ADDRESS)
            self.assertLess(len(data), 1024 * 1024)  # NOBITS must not enlarge boot disk payload.
            self.assertTrue(any(s[1] == 8 and s[5] >= 50 * 1024 * 1024
                                for s in elf_sections(data)))
            loader = (ROOT / 'arch/x86/boot/bios/stage2_bios.asm').read_text()
            self.assertEqual(int(re.search(r'MAX_LOAD_ADDRESS\s+equ\s+(0x[0-9A-Fa-f]+)',
                                           loader)[1], 16), MAX_LOAD_ADDRESS)

    def test_overflow_fails_link_instead_of_spilling_into_other_memory(self):
        with tempfile.TemporaryDirectory(prefix='reist-layout-') as tmp:
            result, _ = self.link(Path(tmp), 32 * 1024 * 1024)
            self.assertNotEqual(result.returncode, 0)
            self.assertRegex(result.stderr, r'kernel_ram|Kernel exceeds')

    def test_embedded_legacy_userspace_sections_are_rejected(self):
        for section in ('.user_text', '.user_rodata', '.user_data', '.user_bss', '.user_text.extra'):
            with self.subTest(section=section), tempfile.TemporaryDirectory(prefix='reist-layout-') as tmp:
                result, _ = self.link(Path(tmp), 4096, section)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('Embedded userspace sections are not supported', result.stderr)

    def test_real_pmm_reserves_large_kernel_and_preserves_firmware_and_recovery(self):
        memory = (ROOT / 'mm/kmalloc.c').read_text()
        # Keep real PMM initialization, map handling, allocation and free. Only
        # replace host-inapplicable linker identity/IRQ glue and i386 heap ABI
        # assertion; the separately linked fixture verifies the actual i386 ABI.
        production = memory[memory.index('#define ALIGN_UP'):memory.index('static bool reserve_frame_run_locked')]
        production = production.replace('_Static_assert(sizeof(memory_block) == 16,\n'
            '               "heap metadata must preserve 16-byte payload alignment");', '')
        fixture = (ROOT / 'test/test_kernel_memory_layout_host.c').read_text()
        with tempfile.TemporaryDirectory(prefix='reist-layout-host-') as tmp:
            source = Path(tmp) / 'host.c'
            source.write_text(fixture.replace('/* PRODUCTION */', production), encoding='utf-8')
            for optimize in ('-O0', '-O2'):
                exe = Path(tmp) / ('host' + optimize + '.exe')
                result = self.invoke([self.zig, 'cc', '-std=c11', optimize, '-UNDEBUG',
                    '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-variable',
                    '-I.', str(source), '-o', str(exe)], timeout=60)
                self.assertEqual(result.returncode, 0, result.stderr)
                result = self.invoke([str(exe)], timeout=10)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn('KERNEL_LAYOUT_HOST_OK', result.stdout)


if __name__ == '__main__':
    unittest.main()
