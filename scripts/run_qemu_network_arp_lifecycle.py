"""Real lost-ARP/retry/PING proof, both NICs, without private guest code."""
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

ROOT=Path(__file__).resolve().parents[1]
LIMIT=2*1024*1024


def service_status(text):
    caption='COMPONENT STATUS 6 name=network-service state=READY generation='
    pattern=re.escape(caption)+r'([1-9][0-9]{0,9})'
    lines=text.replace('\r\n','\n').splitlines();matches=[]
    for index,line in enumerate(lines):
        match=re.fullmatch(pattern,line)
        # The text-console wrap can split the fixed caption, observed as
        # "gene\nration". Join at most two complete adjacent caption pieces;
        # never join/synthesize the numeric generation or remove other noise.
        if (not match and 0<len(line)<len(caption) and caption.startswith(line)
                and index+1<len(lines)):
            match=re.fullmatch(pattern,line+lines[index+1])
        if match:
            following=index+1 if re.fullmatch(pattern,line) else index+2
            if following<len(lines) and re.fullmatch(r'[0-9]+',lines[following]):
                raise ValueError('ambiguous generation continuation')
            matches.append(match[1])
    if len(matches)!=1 or int(matches[0])>0xffffffff:raise ValueError('exact ready service generation missing')
    return int(matches[0])


def service_pid(text):
    rows=re.findall(r'(?mi)^ *([1-9][0-9]*) +[0-9]+ +(READY|RUNNING|SLEEPING|WAITING) +(?:/libexec/reist/)?reist(?:\.prg)?\r?$',text)
    if len(rows)!=1:raise ValueError('unique live network service PID missing')
    return int(rows[0][0])


def echo(frame):
    if len(frame)<42 or frame[:6]!=smoke.OUTBOUND_PING_MAC or frame[12:14]!=b'\x08\x00':return None
    if frame[14]>>4!=4 or frame[23]!=1 or frame[26:30]!=smoke.GUEST_IP or frame[30:34]!=smoke.OUTBOUND_PING_TARGET:return None
    ihl=(frame[14]&15)*4; total=struct.unpack_from('!H',frame,16)[0]
    if ihl<20 or total<ihl+8 or 14+total>len(frame):return None
    icmp=frame[14+ihl:14+total]
    if smoke.internet_checksum(frame[14:14+ihl]) or icmp[:2]!=b'\x08\x00' or smoke.internet_checksum(icmp):return None
    identifier,sequence=struct.unpack_from('!HH',icmp,4)
    return identifier,sequence,icmp[8:]


class Peer:
    def __init__(self,connection,deadline):
        self.connection=connection; self.deadline=deadline; self.stop=threading.Event()
        self.error=None; self.events=[]; self.arp=0; self.replies=0; self.frames=0
        self.thread=threading.Thread(target=self.run,daemon=True)
    def run(self):
        try:
            # Stop only between complete bounded frames. Socket closure by the
            # owner cancels reads; there is no unbounded peer/server lifetime.
            self.connection.settimeout(2)
            while not self.stop.is_set() and time.monotonic()<self.deadline:
                self.connection.settimeout(min(2,max(.01,self.deadline-time.monotonic())))
                try: header=self.connection.recv(4)
                except socket.timeout:continue
                if not header:return
                if len(header)<4:
                    tail=smoke.receive_exact(self.connection,4-len(header))
                    if tail is None:raise ValueError('partial peer frame length')
                    header+=tail
                length=struct.unpack('!I',header)[0]
                self.frames+=1
                if not 14<=length<=1518 or self.frames>8192:raise ValueError('peer frame quota')
                frame=smoke.receive_exact(self.connection,length)
                if frame is None:raise ValueError('partial peer frame')
                if (len(frame)>=42 and frame[6:12]==smoke.GUEST_MAC and
                    frame[12:22]==bytes.fromhex('08060001080006040001') and
                    frame[22:28]==smoke.GUEST_MAC and frame[28:32]==smoke.GUEST_IP and
                    frame[38:42]==smoke.OUTBOUND_PING_TARGET):
                    self.arp+=1
                    if self.arp>32:raise ValueError('ARP retry quota')
                    self.events.append({'kind':'arp','index':self.arp,'host_time':time.monotonic(),'frame':frame.hex()})
                    # Two actual requests get no response. A third can only
                    # be admitted after the kernel retired both predecessors.
                    if self.arp>=3:
                        if not smoke.inject_ethernet_frame(self.connection,smoke.outbound_ping_arp_reply_frame()):raise ValueError('ARP reply send')
                packet=echo(frame)
                if packet is not None:
                    if self.replies>=32:raise ValueError('echo quota')
                    if self.arp<3:raise ValueError('echo before deliberate loss')
                    if not smoke.inject_ethernet_frame(self.connection,smoke.outbound_ping_icmp_reply_frame(*packet)):raise ValueError('ICMP reply send')
                    self.replies+=1
                    self.events.append({'kind':'echo','sequence':packet[1],'host_time':time.monotonic()})
        except (OSError,ValueError) as error:
            if not self.stop.is_set():self.error=str(error)


def run_case(qemu,image,evidence,nic,overall):
    started=time.monotonic(); deadline=min(overall,started+90)
    listener,port=smoke.open_injection_listener()
    process=None; connection=None; peer=None; reader=None
    chunks=queue.Queue(LIMIT+1); transcript=[]; finished=threading.Event(); overflow=threading.Event()
    error=None; commands=[]; identities=[]; boot_end=0; prompt=-1
    def serial():
        try:
            with (evidence/(nic+'-live.log')).open('w',encoding='utf-8') as log:
                for _ in range(LIMIT):
                    c=process.stdout.read(1)
                    if not c:return
                    log.write(c)
                    if c=='\n':log.flush()
                    chunks.put_nowait(c)
            overflow.set()
        finally:finished.set()
    def wait(marker,after):
        while time.monotonic()<deadline:
            smoke.drain(chunks,transcript); text=''.join(transcript)
            tail=text[boot_end:] if boot_end else ''
            if overflow.is_set() or smoke.failure_marker(text) or 'REIST_NETWORK SERVICE_CRASH_RECOVERED' in tail or '*** USER PROCESS' in tail:
                raise ValueError('guest fault/restart/quota')
            if peer and peer.error:raise ValueError(peer.error)
            position=smoke.exact_line_position(text,marker,after)
            if position>=0:return position
            if process.poll() is not None:raise ValueError('guest exited')
            try:transcript.append(chunks.get(timeout=.05))
            except queue.Empty:pass
        raise ValueError('guest deadline before '+marker)
    def command(text,required=None):
        nonlocal prompt
        before=prompt
        smoke.inject_ps2_command(process,text)
        prompt=wait(smoke.SHELL_PROMPT,before)
        result=''.join(transcript)[before+len(smoke.SHELL_PROMPT):prompt]
        if required and required not in result.replace('\r','').splitlines():raise ValueError('command result missing '+text)
        commands.append(text);return result
    try:
        process=subprocess.Popen(smoke.qemu_command(qemu,image,nic=nic,injection_port=port),
            stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,
            encoding='utf-8',errors='replace',bufsize=0,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        reader=threading.Thread(target=serial,daemon=True);reader.start()
        connection,_=listener.accept();peer=Peer(connection,deadline);peer.thread.start()
        smoke.configure_qemu_host_timers(process)
        prompt=wait(smoke.SHELL_PROMPT,-1);boot_end=prompt
        for cycle in range(2):
            generation=service_status(command('svcctl status 6'))
            pid=service_pid(command('ps'));identities.append((pid,generation))
            before=prompt
            smoke.inject_ps2_command(process,smoke.OUTBOUND_PING_COMMAND)
            reply=wait(smoke.OUTBOUND_PING_REPLY_MARKER,before)
            smoke.inject_ps2_key(process,'ctrl-c');prompt=wait(smoke.SHELL_PROMPT,reply)
            commands.append(smoke.OUTBOUND_PING_COMMAND)
            command('cat /htdocs/hello.js',"print('Hello from REIST JavaScript');")
        identities.append((service_pid(command('ps')),service_status(command('svcctl status 6'))))
        command('help','Built-ins: cd path pwd history help exit')
        if len(set(identities))!=1 or peer.arp<3 or peer.replies<2:raise ValueError('lifecycle/retry/progress proof incomplete')
    except (OSError,ValueError,RuntimeError) as caught:error=str(caught)
    finally:
        if peer:peer.stop.set()
        if connection:
            try:connection.shutdown(socket.SHUT_RDWR)
            except OSError:pass
            connection.close()
        listener.close()
        if peer:peer.thread.join(2)
        if process:smoke.stop_process(process)
        if reader:reader.join(2)
        smoke.drain(chunks,transcript)
        raw=''.join(transcript);(evidence/(nic+'.log')).write_text(raw,encoding='utf-8')
    if (overflow.is_set() or (peer and (peer.error or peer.thread.is_alive())) or
        (reader and reader.is_alive()) or time.monotonic()>deadline):
        error=error or 'peer/serial cleanup, quota or deadline failure'
    if ('REIST_NETWORK SERVICE_CRASH_RECOVERED' in raw[boot_end:] or
        (boot_end and '*** USER PROCESS' in raw[boot_end:])):
        error=error or 'network service restarted during proof'
    admitted=len(re.findall(r'(?m)^REIST_NETWORK ARP_RESOLUTION_MEDIATED\r?$',raw[boot_end:]))
    if admitted<3:error=error or 'three distinct kernel-mediated transmissions missing'
    report={'nic':nic,'passed':error is None,'error':error,'commands':commands,'identities':identities,
        'events':peer.events if peer else [],'peer_error':peer.error if peer else None,
        'mediated_requests':admitted,
        'seconds':round(time.monotonic()-started,3)}
    print('NETWORK_ARP_GUEST',nic,'PASS' if error is None else 'FAIL: '+error,flush=True)
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qemu',type=Path,required=True);parser.add_argument('--image',type=Path,required=True)
    parser.add_argument('--evidence',type=Path,required=True);args=parser.parse_args()
    evidence=args.evidence.resolve();allowed=(ROOT/'build/codex-agent/r341a-network-arp').resolve()
    if evidence.exists() or evidence==allowed or not evidence.is_relative_to(allowed) or not args.qemu.is_file() or not args.image.is_file():parser.error('existing qemu/image and new r341a evidence subdirectory required')
    suppress_windows_test_dialogs();evidence.mkdir(parents=True)
    from run_qemu_math import digest
    baseline=digest(args.image);start=time.monotonic();report={'passed':False,'reference_sha256':baseline,'guests':[]}
    for nic in ('e1000','rtl8139'):
        result=run_case(args.qemu,args.image,evidence,nic,start+180);report['guests'].append(result)
        if not result['passed']:break
    report['passed']=len(report['guests'])==2 and all(x['passed'] for x in report['guests']) and digest(args.image)==baseline
    report['elapsed_seconds']=round(time.monotonic()-start,3)
    (evidence/'result.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
