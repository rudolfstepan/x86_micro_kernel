"""Bounded O0/O2 execution of the actual Ring-3 ARP lifecycle branches."""
from pathlib import Path
import subprocess
import sys
import unittest
import uuid

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from measure_cpp_baseline import suppress_windows_test_dialogs
from test_reist_probe_domain import function


class NetworkArpLifecycleTests(unittest.TestCase):
    def test_guest_identity_is_exact_and_live(self):
        from run_qemu_network_arp_lifecycle import service_status,service_pid
        status='COMPONENT STATUS 6 name=network-service state=READY generation=3'
        self.assertEqual(service_status(status+'\n'),3)
        self.assertEqual(service_status('\n'+status+'\n'),3)
        self.assertEqual(service_status(status.replace('generation','gene\nration')+'\n'),3)
        for bad in ('',status.replace('=3','=0'),status.replace('=3','=4294967296'),
                    status+' FENCED',status.replace('READY','FAILED'),'noise '+status,status+'\n'+status,
                    status.replace('=3','=3\n2'),status.replace('generation','gene\nnoise\nration')):
            with self.assertRaises(ValueError):service_status(bad)
        pid='    5     0  WAITING   /libexec/reist/reist.prg'
        self.assertEqual(service_pid(pid+'\n'),5)
        for bad in ('',pid.replace('WAITING','ZOMBIE'),pid+' extra','noise '+pid,pid+'\n'+pid):
            with self.assertRaises(ValueError):service_pid(bad)

    def test_real_peer_withholds_two_requests_then_replies(self):
        import socket,struct,time
        import run_qemu_network_arp_lifecycle as guest
        smoke=guest.smoke
        left,right=socket.socketpair();peer=guest.Peer(left,time.monotonic()+3)
        peer.thread.start();right.settimeout(.05)
        frame=bytearray(smoke.arp_request_frame(smoke.GUEST_MAC,smoke.GUEST_IP))
        frame[38:42]=smoke.OUTBOUND_PING_TARGET
        try:
            for _ in range(2):
                self.assertTrue(smoke.inject_ethernet_frame(right,frame))
                with self.assertRaises(socket.timeout):right.recv(1)
            self.assertTrue(smoke.inject_ethernet_frame(right,frame));right.settimeout(1)
            length=struct.unpack('!I',smoke.receive_exact(right,4))[0]
            self.assertEqual(smoke.receive_exact(right,length),smoke.outbound_ping_arp_reply_frame())
            self.assertEqual(peer.arp,3);self.assertIsNone(peer.error)
        finally:
            peer.stop.set();right.close();left.close();peer.thread.join(2)
        self.assertFalse(peer.thread.is_alive())

    def test_echo_rejects_invalid_ranges_and_checksums(self):
        import struct
        import run_qemu_network_arp_lifecycle as guest
        smoke=guest.smoke
        frame=bytearray(smoke.outbound_ping_icmp_reply_frame(0x1234,2,b'ping'))
        frame[:6]=smoke.OUTBOUND_PING_MAC;frame[6:12]=smoke.GUEST_MAC
        frame[26:30]=smoke.GUEST_IP;frame[30:34]=smoke.OUTBOUND_PING_TARGET
        frame[34]=8;frame[36:38]=b'\0\0'
        frame[36:38]=struct.pack('!H',smoke.internet_checksum(frame[34:46]))
        self.assertEqual(guest.echo(frame),(0x1234,2,b'ping'))
        for at in (0,12,14,16,23,26,30,34,36,45):
            bad=bytearray(frame);bad[at]^=1
            self.assertIsNone(guest.echo(bad))
        self.assertIsNone(guest.echo(frame[:40]))

    def test_actual_ring3_dispatch(self):
        suppress_windows_test_dialogs()
        evidence=ROOT/'build/codex-agent/r341a-network-arp'/('native-'+uuid.uuid4().hex)
        evidence.mkdir(parents=True)
        source=(ROOT/'userspace/programs/reist_probe.c').read_text(encoding='utf-8')
        helper='static bool network_arp_refused('
        helpers=function(source,helper) if helper in source else ''
        branches='\n'.join(function(source,signature) for signature in (
            "if (network != NULL && request.payload[3] == 'R' &&",
            "if (network != NULL && request.payload[3] == 'R')",
            "if (network != NULL && request.payload[3] == 'A')"))
        unit=(ROOT/'test/network_arp_lifecycle_host.c').read_text().replace(
            '/* PRODUCTION_HELPERS */',helpers).replace('/* PRODUCTION_DISPATCH */',branches)
        path=evidence/'actual.c'; path.write_text(unit,encoding='utf-8')
        failures=[]
        for opt in ('-O0','-O2'):
            exe=evidence/(opt+'.exe')
            commands=[['gcc','-std=c11','-Wall','-Wextra','-Werror',opt,str(path),'-o',str(exe)],[str(exe)]]
            for index,cmd in enumerate(commands):
                result=subprocess.run(cmd,capture_output=True,text=True,timeout=90 if index==0 else 10,
                    creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
                (evidence/(opt+('-compile.log' if index==0 else '-run.log'))).write_text(
                    result.stdout+result.stderr,encoding='utf-8')
                if index==0:self.assertEqual(result.returncode,0,result.stderr)
                elif result.returncode:failures.append(opt+': '+result.stdout+result.stderr)
                else:print(opt,result.stdout.strip())
        self.assertFalse(failures,'\n'.join(failures))


if __name__=='__main__':unittest.main()
