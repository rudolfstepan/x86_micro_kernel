"""Execute real bounded receive/cadence code at O0/O2; OS-only mocks."""
import pathlib
import os
import shutil
import subprocess
import sys
import tempfile
import struct
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from measure_cpp_baseline import suppress_windows_test_dialogs


class NetworkProgressTests(unittest.TestCase):
    def compile_run(self, sources, includes, defines=()):
        suppress_windows_test_dialogs()
        zig = pathlib.Path(r'C:\tools\zig-x86_64-windows-0.16.0\zig.exe')
        compiler = [str(zig), 'cc'] if zig.is_file() else [shutil.which('gcc')]
        self.assertTrue(compiler[0], 'native compiler required')
        with tempfile.TemporaryDirectory(prefix='reist-net-progress-') as folder:
            environment = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(ROOT / 'build/codex-agent/zig-cache'),
                               ZIG_LOCAL_CACHE_DIR=str(pathlib.Path(folder) / 'zig-local'))
            for opt in ('-O0', '-O2'):
                exe = pathlib.Path(folder) / (opt[1:] + '.exe')
                command = compiler + ['-std=c11', '-Wall', '-Wextra', '-Werror', opt]
                command += ['-D' + value for value in defines]
                for path in includes:
                    command += ['-I', str(ROOT / path)]
                command += [str(ROOT / path) for path in sources] + ['-o', str(exe)]
                result = subprocess.run(command, capture_output=True, text=True, timeout=60, env=environment)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_real_tcp_receive(self):
        self.compile_run(['drivers/net/tcp_socket.c', 'test/network_progress_host.c'],
                         ['.'], ['REIST_HOST_TEST'])

    def test_real_ring3_parser(self):
        self.compile_run(['userspace/sdk/reist_ipv4_parser.c',
                          'userspace/sdk/reist_tcp_parser.c',
                          'test/test_reist_tcp_parser_host.c'], ['userspace/sdk/include'])

    def test_real_rtl8139_receive_wrap(self):
        source = (ROOT / 'drivers/net/rtl8139.c').read_text(encoding='utf-8')
        # Compile unchanged production declarations and the entire RX drain.
        # Only PIO and the receiving netdev boundary are mocks.
        constants = '\n'.join(re.findall(r'^#define RTL\w+[^\n]*', source, re.M))
        declarations = source[source.index('typedef struct {'):
                              source.index('static volatile uint32_t rtl8139_tx_busy;')]
        declarations += '\nstatic volatile bool rtl8139_rx_pending;\n'
        declarations += source[source.index('static uint8_t rtl8139_rx_buffer['):
                               source.index('static bool rtl8139_wait_reset(')]
        start = source.index('static void rtl8139_drain_rx(void) {')
        opening = source.index('{', start)
        end, depth = opening + 1, 1
        while depth:
            depth += (source[end] == '{') - (source[end] == '}')
            end += 1
        fixture = (ROOT / 'test/rtl8139_receive_host.c').read_text(encoding='utf-8')
        fixture = fixture.replace('/* REAL_DECLARATIONS */', constants + '\n' + declarations)
        fixture = fixture.replace('/* REAL_FUNCTION */', source[start:end])
        with tempfile.TemporaryDirectory(prefix='reist-rtl-rx-') as folder:
            unit = pathlib.Path(folder) / 'rtl_receive.c'
            unit.write_text(fixture, encoding='utf-8')
            self.compile_run([unit], [])

    def test_packaging_and_bounded_integration(self):
        for path, marker in (
            ('Makefile', 'sbin/nettest.prg=$(SYSTEM_PROGRAM_DIR)/NETTEST.PRG'),
            ('scripts/build-windows.ps1', "'sbin/nettest.prg' = 'NETTEST.PRG'"),
            ('scripts/build_system_programs.py', '"NETTEST.PRG": ROOT / "userspace/programs/nettest.c"'),
            ('userspace/bin/shell.c', '"/bin", "/sbin", "/usr/bin", "/usr/gui/bin"'),
            ('scripts/build_system_programs.py', 'ROOT / "userspace/programs/network_cadence.h"'),
            ('userspace/programs/reist_probe.c', '&network_cadence, now_ms, received_frame'),
            ('userspace/programs/reist_probe.c', 'x86os_ipc_receive_timeout(endpoint, &request, control_wait)'),
            ('kernel/syscall/syscall_table.c', 'uint8_t data[TCP_SOCKET_RECEIVE_CAPACITY]'),
        ):
            self.assertIn(marker, (ROOT / path).read_text(encoding='utf-8'))

    def test_guest_evidence_rejects_incomplete_or_slow_results(self):
        from run_qemu_network_progress import validate_metrics
        rows = ['NETTEST_OK mode=' + mode + ' bytes=' + ('1048576' if mode in 'sw' else '0') + ' elapsed_ms=500'
                for mode in 'swtrs']
        good = '\n'.join(rows[:4]) + '\nNETTEST_CANCEL_READY\nNETTEST_CANCEL_REAP_OK status=143\n' + rows[4] + '\nNETTEST_FILE_OK bytes=1048576\n'
        self.assertEqual(len(validate_metrics(good)), 5)
        for bad in ('', good.replace('1048576', '325231'), good.replace('elapsed_ms=500', 'elapsed_ms=5001'),
                    good.replace('NETTEST_CANCEL_READY', ''), good.replace('status=143', 'status=0'),
                    good + '*** USER PROCESS PAGE FAULT ***', good + rows[0] + '\n', good + 'NETTEST_FAIL'):
            with self.assertRaises(ValueError): validate_metrics(bad)

    def test_packet_evidence_is_bounded_and_checks_backpressure(self):
        from run_qemu_network_progress import packet_windows
        class Trace:
            def __init__(self, data): self.data = data
            def read_bytes(self): return self.data
        def packet(window, source=40000, destination=18766, flags=16):
            frame = bytearray(54)
            frame[12:14] = b'\x08\x00'; frame[14] = 0x45; frame[23] = 6
            struct.pack_into('>HH', frame, 34, source, destination)
            frame[47] = flags; struct.pack_into('>H', frame, 48, window)
            return struct.pack('<IIII', 0, 0, 54, 54) + frame
        header = b'\xd4\xc3\xb2\xa1' + bytes(20)
        good = header + packet(32768) + packet(0) + packet(0, 18766, 40000, 4)
        self.assertEqual(packet_windows(Trace(good))['peer_resets'], 1)
        self.assertEqual(packet_windows(Trace(header + packet(32768) + packet(6848) + packet(0, 18766, 40000, 4)))['min'], 6848)
        for bad in (b'', good[:-1], header + packet(2048),
                    header + packet(32768), header + packet(32768) + packet(0),
                    header + packet(32768) + packet(16384) + packet(0, 18766, 40000, 4)):
            with self.assertRaises(ValueError): packet_windows(Trace(bad))


if __name__ == '__main__':
    unittest.main()
