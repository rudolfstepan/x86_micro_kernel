"""One bounded snapshot boot: Display persistence, real input and Ring-3 fault containment."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import queue
import re
import subprocess
import threading
import time
from run_qemu_smoke import (qemu_command, open_injection_listener,
    configure_qemu_host_timers, stop_process, SHELL_PROMPT,
    REIST_PROBE_MARKERS, REIST_PROBE_COMPLETION_MARKER)
from run_qemu_runtime_desktop import (BrowserInputMonitor, reader, drain, read_ppm,
                                     SHELL_HELP_MARKER)


def validate_faults(text):
    text=text.replace('\r\n','\n')
    if text.count('\nBOOT_OK\n')!=1 or 'USER PROCESS PAGE FAULT' in text:
        raise RuntimeError('Unexpected extra userspace fault')
    boot,runtime=text.split('\nBOOT_OK\n')
    exception=('USER PROCESS EXCEPTION', 'Exception: Invalid Opcode (IRQ 6)',
               'Process terminated.')
    # The established boot self-test deliberately crashes reist.prg once.
    # Admit only its complete ordered recovery, not arbitrary pre-GUI faults.
    for phase,sequence in ((boot,(*exception,*REIST_PROBE_MARKERS,REIST_PROBE_COMPLETION_MARKER)),
                           (runtime,('DISPLAY_APPLET_FAULT',*exception))):
        previous=-1
        for marker in sequence:
            position=phase.find(marker)
            if phase.count(marker)!=1 or position<=previous:
                raise RuntimeError('Unexpected userspace fault sequence: '+marker)
            previous=position
    if 'DISPLAY_APPLET_FAULT' in boot:
        raise RuntimeError('Applet fault before boot completion')


class DisplayProof:
    def __init__(self, process, monitor, output, transcript, directory, deadline):
        self.process, self.monitor = process, monitor
        self.output, self.transcript = output, transcript
        self.directory, self.deadline = directory, deadline
        self.width, self.height, self.x, self.y = 1024, 768, 512, 384

    def text(self):
        drain(self.output, self.transcript)
        value = ''.join(self.transcript)
        if len(value) > 4*1024*1024:
            raise RuntimeError('Display serial quota')
        if any(marker in value for marker in ('KERNEL PANIC', '*** KERNEL EXCEPTION',
               'COMPOSITOR_DEGRADED', 'DISPLAY_APPLET_LAUNCH_FAILED')):
            raise RuntimeError('Display containment failure')
        return value

    def wait(self, pattern, offset=0):
        while time.monotonic() < self.deadline:
            found = re.search(pattern, self.text()[offset:])
            if found:
                print('display-proof: matched ' + pattern[:96], flush=True)
                return found
            if self.process.poll() is not None:
                raise RuntimeError('QEMU exited before '+pattern)
            time.sleep(0.02)
        raise TimeoutError('Display deadline waiting for '+pattern)

    def command(self, command, marker=None):
        offset = len(self.text())
        self.monitor.type_text(command)
        self.monitor.key('ret')
        if marker:
            self.wait(re.escape(marker)+r'[\s\S]*'+re.escape(SHELL_PROMPT), offset)
        else:
            self.wait(re.escape(SHELL_PROMPT), offset)
        return offset

    def move(self, x, y):
        # Same bounded relative HID contract as browser input acceptance.
        for _ in range(48):
            dx, dy = x-self.x, y-self.y
            if not dx and not dy:
                return
            dx, dy = max(-120,min(120,dx)), max(-120,min(120,dy))
            self.monitor.mouse(self.process, f'mouse_move {dx} {dy}')
            self.x += dx; self.y += dy
            time.sleep(0.04)
        raise RuntimeError('Display pointer trajectory quota')

    def click(self, x, y):
        self.move(x,y)
        for down in (1,0):
            self.monitor.mouse(self.process, f'mouse_button {down}')
            time.sleep(0.08)

    def screenshot(self, name, rect=None):
        path = self.directory / (name+'.ppm')
        time.sleep(0.20)
        self.monitor.execute('screendump', {'filename':str(path.resolve())})
        ppm = read_ppm(path)
        if ppm is None or ppm[:2] != (self.width,self.height):
            raise RuntimeError('Wrong scanout geometry: '+name)
        if rect is not None:
            x,y,w,h = rect
            colors = set()
            for yy in range(y+100,min(y+h-90,self.height),4):
                for xx in range(x+20,min(x+w-20,self.width),4):
                    start=(yy*self.width+xx)*3
                    colors.add(ppm[2][start:start+3])
            if b'\xff\xff\xff' not in colors or b'\x00\x00\x88' not in colors:
                raise RuntimeError('Native list pixels missing: '+name)
        return path

    def window(self, kind, offset):
        match=self.wait(r'DISPLAY_PROBE_'+kind+r' pid=(\d+) x=(\d+) y=(\d+) width=(\d+) height=(\d+)',offset)
        return int(match[1]),tuple(int(match[i]) for i in range(2,6))

    def desktop(self, expected=None):
        offset=len(self.text())
        self.monitor.type_text('desktop.prg --control-probe');self.monitor.key('ret')
        mode=self.wait(r'DESKTOP_MODE_ACTIVE width=(\d+) height=(\d+) bpp=32',offset)
        self.width,self.height=int(mode[1]),int(mode[2]);self.x,self.y=self.width//2,self.height//2
        if expected and (self.width,self.height)!=expected:
            raise RuntimeError(f'Persisted mode mismatch: {(self.width,self.height)} != {expected}')
        self.wait('DESKTOP_CONTROL_OK',offset)
        self.wait('CONTROL_PANEL_READY',offset)
        self.control_pid,control=self.window('CONTROL',offset)
        point=self.wait(r'DISPLAY_PROBE_START_POINT x=(\d+) y=(\d+)',offset)
        self.start_point=(int(point[1]),int(point[2]))
        return offset,control

    def open_applet(self, control, select=True):
        offset=len(self.text())
        if select:
            for _ in range(4):self.monitor.key('down')
            self.monitor.key('ret')
        else:
            x,y,_,_=control;self.click(x+260,y+145)
        self.wait('DISPLAY_APPLET_LAUNCHED',offset)
        self.wait('DISPLAY_APPLET_PAINTED',offset)
        self.applet_pid,rect=self.window('APPLET',offset)
        return self.applet_pid,rect

    def save(self, rect, downs, value, name):
        offset=len(self.text())
        for _ in range(downs):self.monitor.key('down')
        # Keys are asynchronous: do not move focus to Save before the applet
        # has applied AND painted the requested selection. No reinjection.
        self.wait('DISPLAY_SELECTION_READY value='+re.escape(value)+r'\r?\n',offset)
        offset=len(self.text())
        x,y,_,height=rect;self.click(x+50,y+height-57)
        self.wait('DISPLAY_SETTINGS_SAVED '+re.escape(value),offset)
        self.screenshot(name,rect)  # stored setting must not resize this live session
        if 'DESKTOP_MODE_ACTIVE' in self.text()[offset:]:
            raise RuntimeError('Unexpected live mode activation')

    def close_desktop(self, windows=True):
        if windows:
            offset=len(self.text());self.monitor.key('esc')
            self.wait('DISPLAY_APPLET_CLOSED',offset)
            self.wait(r'DISPLAY_PROBE_APPLET_RETIRED pid='+str(self.applet_pid)+r'\r?\n',offset)
            offset=len(self.text())
            self.monitor.key('esc')
            self.wait(r'DISPLAY_PROBE_CONTROL_RETIRED pid='+str(self.control_pid)+r'\r?\n',offset)
        offset=len(self.text())
        if windows:
            self.click(*self.start_point)
            point=self.wait(r'DISPLAY_PROBE_MENU_READY x=(\d+) y=(\d+)',offset)
            self.click(int(point[1]),int(point[2]))
        else:
            self.click(30,self.height-15)
            self.click(70,self.height-42)
        self.wait('DESKTOP_EXIT_OK',offset)
        # GUI programs are launched in the background. The prior shell prompt
        # is not new evidence; require a fresh foreground response after exit.
        self.command('help',SHELL_HELP_MARKER)

    def run(self):
        first=self.wait(r'DESKTOP_OK|'+re.escape(SHELL_PROMPT))
        if first[0]=='DESKTOP_OK':
            mode=re.findall(r'DESKTOP_MODE_ACTIVE width=(\d+) height=(\d+)',self.text())[-1]
            self.width,self.height=map(int,mode);self.x,self.y=self.width//2,self.height//2
            self.close_desktop(False)
        self.command('help',SHELL_HELP_MARKER)
        self.command("display --list",'DISPLAY_COMMAND_READY')
        _,control=self.desktop()
        initial=(self.width,self.height)
        _,rect=self.open_applet(control)
        self.screenshot('initial',rect)
        self.save(rect,1,'800x600','saved-800-not-live')
        assert (self.width,self.height)==initial
        self.close_desktop()
        _,control=self.desktop((800,600))
        pid,rect=self.open_applet(control)
        self.screenshot('active-800',rect)
        self.save(rect,3,'1280x720','saved-1280-not-live')
        fault_offset=len(self.text());self.monitor.key('ctrl-g')
        self.wait('DISPLAY_APPLET_FAULT',fault_offset)
        self.wait(r'USER PROCESS EXCEPTION[\s\S]*Process terminated\.',fault_offset)
        self.wait(r'DISPLAY_PROBE_APPLET_RETIRED pid='+str(pid)+r'\r?\n',fault_offset)
        replacement,replacement_rect=self.open_applet(control,False)
        if replacement==pid:
            raise RuntimeError('Faulted applet PID reused as live instance')
        self.screenshot('fresh-applet-after-exception',replacement_rect)
        self.close_desktop()
        _,control=self.desktop((1280,720))
        _,rect=self.open_applet(control)
        self.screenshot('active-1280',rect)
        self.close_desktop()
        self.command('config set desktop resolution 4096x4096','CONFIG_UPDATE_OK')
        offset,control=self.desktop(initial)
        self.wait('DESKTOP_MODE_FALLBACK status=-95',offset)
        _,rect=self.open_applet(control)
        self.screenshot('unsupported-safe-fallback',rect)
        self.close_desktop()
        self.command('help',SHELL_HELP_MARKER)
        validate_faults(self.text())
        return {'saved_modes':['800x600','1280x720'], 'fallback':initial,
                'exception_pid':pid,'replacement_pid':replacement,'shell_return':True}


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--qemu',type=Path,required=True)
    parser.add_argument('--image',type=Path,required=True)
    parser.add_argument('--backend',choices=('std','vmware'),required=True)
    parser.add_argument('--timeout',type=int,default=180)
    parser.add_argument('--evidence',type=Path,required=True)
    args=parser.parse_args()
    if not 1<=args.timeout<=180:
        parser.error('frozen maximum: 180 seconds')
    args.evidence.mkdir(parents=True,exist_ok=True)
    if (args.evidence/'status.json').exists():
        parser.error('evidence already exists; preserve previous acceptance')
    started=time.monotonic();deadline=started+args.timeout
    listener,port=open_injection_listener()
    command=qemu_command(args.qemu,args.image,memory='1024M',vmware_vga=args.backend=='vmware',smp=1)
    if args.backend=='std':command+=['-device','VGA']
    command+=['-device','qemu-xhci,id=reistxhci','-device','usb-mouse,bus=reistxhci.0',
              '-qmp',f'tcp:127.0.0.1:{port},server=off,nodelay=on']
    process=monitor=None;transcript=[];output=queue.Queue();result={'passed':False,'backend':args.backend}
    try:
        process=subprocess.Popen(command,stdin=subprocess.PIPE,stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace',bufsize=0,
            creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0)|getattr(subprocess,'BELOW_NORMAL_PRIORITY_CLASS',0))
        timers=configure_qemu_host_timers(process)
        transcript.append(f'HOST_QEMU_TIMER_POLICY verified={int(timers)} scope=child\n')
        threading.Thread(target=reader,args=(process.stdout,output,threading.Event()),daemon=True).start()
        monitor=BrowserInputMonitor.accept(listener,deadline)
        proof=DisplayProof(process,monitor,output,transcript,args.evidence,deadline)
        result.update(proof.run());result['passed']=True
    except (OSError,RuntimeError,TimeoutError,AssertionError,ValueError) as error:
        result['error']=str(error)
    finally:
        listener.close()
        if monitor:monitor.peer.close()
        if process:
            stop_process(process)
            for pipe in (process.stdin,process.stdout):
                if pipe:pipe.close()
        drain(output,transcript)
        result['elapsed_seconds']=round(time.monotonic()-started,3)
        result['command']=command
        (args.evidence/'serial.log').write_text(''.join(transcript),encoding='utf-8')
        (args.evidence/'status.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print('runtime-display-settings: '+('PASS' if result['passed'] else 'FAIL')+
          f" backend={args.backend} elapsed={result['elapsed_seconds']}s "+result.get('error',''))
    return 0 if result['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
