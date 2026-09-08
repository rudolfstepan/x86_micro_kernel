#!/usr/bin/env python3
"""R3.27a: real TCP byte integrity/progress, cancellation and protected images."""
import argparse
import hashlib
import json
from pathlib import Path
import queue
import re
import socket
import struct
import subprocess
import threading
import time

import run_qemu_smoke as smoke
from measure_cpp_baseline import suppress_windows_test_dialogs

ROOT = Path(__file__).resolve().parents[1]
BODY = bytes((i * 29 + i // 251) & 255 for i in range(1048576))


def validate_metrics(text):
    rows = re.findall(r'(?m)^NETTEST_OK mode=([swtr]) bytes=(\d+) elapsed_ms=(\d+)\r?$', text)
    if [row[0] for row in rows] != ['s', 'w', 't', 'r', 's']:
        raise ValueError('missing/duplicate ordered transfers')
    for mode, count, elapsed in rows:
        if int(count) != (1048576 if mode in 'sw' else 0) or not 0 < int(elapsed) <= 5000:
            raise ValueError('byte count or bounded progress failed')
        if mode == 't' and int(elapsed) < 250:
            raise ValueError('timeout did not wait')
    if text.count('NETTEST_CANCEL_READY') != 1 or text.count('NETTEST_CANCEL_REAP_OK status=143') != 1 or text.count('NETTEST_FILE_OK bytes=1048576') != 1:
        raise ValueError('missing cancellation/actual CURL file proof')
    if not text.index('NETTEST_OK mode=r') < text.index('NETTEST_CANCEL_READY') < text.index('NETTEST_CANCEL_REAP_OK') < text.rindex('NETTEST_OK mode=s') < text.index('NETTEST_FILE_OK'):
        raise ValueError('cancellation/recovery ordering')
    if any(marker in text for marker in ('NETTEST_FAIL', 'KERNEL PANIC', 'kernel panic', 'BROWSER_PROBE_FAIL')):
        raise ValueError('guest reported failure')
    workload = text.split(smoke.SHELL_PROMPT, 1)[-1]
    if '*** USER PROCESS' in workload:
        raise ValueError('unexpected user process fault during network workload')
    return rows


def packet_windows(path):
    data = path.read_bytes()
    if len(data) > 16 * 1024 * 1024 or len(data) < 24 or data[:4] != b'\xd4\xc3\xb2\xa1':
        raise ValueError('invalid/capacity packet trace')
    windows = []; resets = 0; at = 24
    while at < len(data):
        if at + 16 > len(data): raise ValueError('truncated trace record')
        _, _, size, _ = struct.unpack_from('<IIII', data, at); at += 16
        if size > 96 or at + size > len(data): raise ValueError('trace prefix bounds')
        frame = data[at:at+size]; at += size
        if len(frame) < 54 or frame[12:14] != b'\x08\x00' or frame[23] != 6: continue
        tcp = 14 + (frame[14] & 15) * 4
        if tcp + 20 <= len(frame) and struct.unpack_from('>H', frame, tcp+2)[0] == 18766:
            windows.append(struct.unpack_from('>H', frame, tcp+14)[0])
        if tcp + 20 <= len(frame) and struct.unpack_from('>H', frame, tcp)[0] == 18766 and frame[tcp+13] & 4:
            resets += 1
    # A standards-conforming peer need not fill the final partial window.
    # Actual backpressure + recovery are required; exact zero/overflow/reopen
    # are exercised against the real TCB in the mandatory host test.
    if not windows or max(windows) != 32768 or min(windows) >= 16384 or not resets:
        raise ValueError('actual receive-window pressure/reset not observed')
    return {'max': max(windows), 'min': min(windows), 'zero_count': windows.count(0), 'packets': len(windows), 'peer_resets': resets}


class Peer:
    def __init__(self):
        self.stop = threading.Event(); self.events = []; self.errors = []
        self.socket = socket.socket()
        self.socket.bind(('127.0.0.1', 18766)); self.socket.listen(4); self.socket.settimeout(.2)
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        deadline = time.monotonic() + 180
        while not self.stop.is_set() and time.monotonic() < deadline and len(self.events) < 12:
            try: client, _ = self.socket.accept()
            except socket.timeout: continue
            except OSError: break
            with client:
                client.settimeout(8)
                mode = b''
                try:
                    mode = client.recv(1)
                    self.events.append({'mode': mode.decode('ascii'), 'start': time.monotonic()})
                    if mode in (b's', b'w'):
                        client.sendall(BODY); client.shutdown(socket.SHUT_WR)
                    elif mode in (b't', b'c'):
                        # A stalled source: wait for actual guest close/reset.
                        if client.recv(1): raise ValueError('unexpected stalled-source request')
                    elif mode == b'r':
                        # Inject reset during receive, not while the one-byte
                        # trigger is still awaiting its TCP ACK. This finite
                        # fault delay never paces the successful data streams.
                        self.stop.wait(.25)
                        client.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER,
                                          struct.pack('HH' if __import__('os').name == 'nt' else 'ii', 1, 0))
                    elif mode == b'G':
                        request = b'G'
                        while b'\r\n\r\n' not in request and len(request) < 4096:
                            block = client.recv(512)
                            if not block: break
                            request += block
                        if not request.startswith(b'GET /data.bin HTTP/') or b'\r\n\r\n' not in request:
                            raise ValueError('invalid HTTP request')
                        client.sendall(b'HTTP/1.0 200 OK\r\nContent-Length: 1048576\r\nConnection: close\r\n\r\n' + BODY)
                        client.shutdown(socket.SHUT_WR)
                    else: raise ValueError('unexpected local-peer mode')
                    self.events[-1]['sent'] = time.monotonic()
                except ConnectionResetError:
                    if mode not in (b't', b'c'): self.errors.append('unexpected reset')
                except (OSError, ValueError) as exc: self.errors.append(str(exc))

    def close(self):
        self.stop.set(); self.socket.close(); self.thread.join(9)
        if self.thread.is_alive(): raise RuntimeError('bounded peer teardown failed')


def guest(args, nic):
    log = args.log.with_name(args.log.stem + '-' + nic + '.log')
    trace = log.with_suffix('.pcap')
    if log.exists() or trace.exists() or log.with_suffix('.json').exists(): raise ValueError('existing evidence')
    peer = Peer(); start = time.monotonic(); deadline = start + 180
    command = smoke.qemu_command(args.qemu, args.image, nic=nic, hardware_entropy=True)
    command += ['-object', 'filter-dump,id=rxtrace,netdev=reistnet0,file=' + trace.resolve().as_posix() + ',maxlen=96']
    process = None; reader = None; transcript = []; chunks = queue.Queue(); finished = threading.Event()
    report = {'nic': nic, 'passed': False}
    try:
        process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace',
                                   creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        report['host_timer_policy'] = smoke.configure_qemu_host_timers(process)
        reader = threading.Thread(target=smoke.reader, args=(process.stdout, chunks, finished), daemon=True)
        reader.start()
        position = -1
        def wait(marker):
            nonlocal position
            error, position = smoke.wait_for_line(process, chunks, transcript, finished, marker, deadline, after=position)
            if error: raise RuntimeError(error + ' waiting for ' + marker)
            if 'NETTEST_FAIL' in ''.join(transcript): raise RuntimeError('guest receive failure')
        wait(smoke.SHELL_PROMPT)
        for mode, letter in (('stream', 's'), ('slow', 'w'), ('timeout', 't'), ('reset', 'r')):
            smoke.inject_ps2_command(process, 'nettest ' + mode)
            # Metric fields vary; wait for subsequent prompt, then validate all rows.
            wait(smoke.SHELL_PROMPT)
        smoke.inject_ps2_command(process, 'nettest cancel'); wait('NETTEST_CANCEL_READY')
        wait('NETTEST_CANCEL_REAP_OK status=143'); wait(smoke.SHELL_PROMPT)
        smoke.inject_ps2_command(process, 'nettest stream'); wait(smoke.SHELL_PROMPT)
        # Reference images have no /tmp directory. Create test output only in
        # this disposable QEMU disk overlay, using the normal userspace shell.
        smoke.inject_ps2_command(process, 'mkdir /tmp'); wait(smoke.SHELL_PROMPT)
        smoke.inject_ps2_command(process, 'curl --max-bytes 1048576 -o /tmp/nettest.bin http://10.0.2.2:18766/data.bin')
        wait(smoke.SHELL_PROMPT)
        smoke.inject_ps2_command(process, 'nettest file'); wait('NETTEST_FILE_OK bytes=1048576'); wait(smoke.SHELL_PROMPT)
        report['metrics'] = validate_metrics(''.join(transcript))
    except (OSError, RuntimeError, ValueError) as exc:
        report['error'] = str(exc)
    finally:
        if process is not None: smoke.stop_process(process)
        if reader is not None: finished.wait(1); reader.join(1)
        smoke.drain(chunks, transcript); log.write_text(''.join(transcript), encoding='utf-8')
        peer.close(); report['peer'] = peer.events; report['peer_errors'] = peer.errors
    if 'error' not in report:
        try:
            if [e['mode'] for e in peer.events] != list('swtrcsG') or peer.errors:
                raise ValueError('missing/excess/failed actual peer sessions')
            report['windows'] = packet_windows(trace)
            report['passed'] = True
        except ValueError as exc: report['error'] = str(exc)
    report['elapsed_seconds'] = round(time.monotonic() - start, 3)
    log.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('NETWORK_PROGRESS ' + nic + ' ' + ('PASS' if report['passed'] else 'FAIL ' + report['error']) +
          f" elapsed={report['elapsed_seconds']}s")
    if not report['passed']:
        print('\n'.join(line for line in ''.join(transcript).splitlines()
                        if line.startswith(('NETTEST', 'curl:', 'PANIC:', 'TEST_FAIL')))[-2000:])
    return report


def artifacts(args):
    from verify_text_artifacts import read_fat_file, image_program_path, UNCHANGED
    from run_qemu_math import kernel_digest, digest
    expected = {**UNCHANGED,
        'BROWSER.PRG': 'bcf3c72462b9965605fe252a5f0c78eac42c806f310d71a554b07489d999b3db',
        'HTMLWORK.PRG': 'ceefae4746a906e48aae4c3ababd7d39f47a22525b385f6014a87ea851956658',
        'CURL.PRG': 'aa3e619cade08e7172ebee80f192c0db728a0a5b987b23319e11448c6fc1b7cc',
        'JSTEST.PRG': '723765e1e695274750366d36858b1c5812239033c6e9847616fca2357904d440',
        'JSWORK.PRG': '4bc4bd9d4f2913e00adbaa04e39b61f5f0888f93f0c5272889a8b6064b3516d0'}
    report = {'baseline': '3eab01ab', 'passed': False, 'programs': expected, 'images': {}}
    for name, wanted in expected.items():
        if digest(ROOT / 'build/programs' / name) != wanted: raise ValueError('protected build changed ' + name)
    for profile, image in [('qemu', args.image), ('vmware', ROOT / 'build/vmware/reist-os/reist-os-flat.vmdk')]:
        for name, wanted in expected.items():
            if hashlib.sha256(read_fat_file(image, image_program_path(name))).hexdigest() != wanted:
                raise ValueError('protected payload changed ' + profile + ' ' + name)
        for name, path in [('NETTEST.PRG', 'sbin/nettest.prg'), ('REIST.PRG', 'libexec/reist/reist.prg')]:
            if hashlib.sha256(read_fat_file(image, path)).hexdigest() != digest(ROOT / 'build/programs' / name):
                raise ValueError('new payload mismatch ' + name)
        report['images'][profile] = {'sha256': digest(image), 'kernel_sha256': kernel_digest(image)}
    if report['images']['qemu']['kernel_sha256'] != digest(ROOT / 'build/kernel.bin'):
        raise ValueError('QEMU kernel payload mismatch')
    report['passed'] = True
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qemu', type=Path)
    parser.add_argument('--image', type=Path, default=ROOT / 'build/reist-os.img')
    parser.add_argument('--log', type=Path, required=True)
    parser.add_argument('--verify-artifacts', action='store_true')
    args = parser.parse_args()
    if not args.verify_artifacts and (args.qemu is None or not args.qemu.is_file()):
        parser.error('existing QEMU executable required')
    if args.log.exists(): parser.error('refusing to overwrite evidence')
    args.log.parent.mkdir(parents=True, exist_ok=True); suppress_windows_test_dialogs()
    started = time.monotonic(); report = {'passed': False}
    try:
        if args.verify_artifacts: report = artifacts(args)
        else:
            report['guests'] = []
            for nic in ('e1000', 'rtl8139'):
                result = guest(args, nic); report['guests'].append(result)
                if not result['passed']: break
            report['passed'] = len(report['guests']) == 2 and all(r['passed'] for r in report['guests'])
    except (OSError, ValueError, RuntimeError) as exc: report['error'] = str(exc)
    report['elapsed_seconds'] = round(time.monotonic() - started, 3)
    args.log.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('NETWORK_GATE ' + ('PASS' if report['passed'] else 'FAIL ' + report.get('error', 'guest evidence')) +
          f" elapsed={report['elapsed_seconds']}s")
    return 0 if report['passed'] else 1


if __name__ == '__main__': raise SystemExit(main())
