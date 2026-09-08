"""R3.31 headless real Mouse applet, saved profile and session-input proof."""
import argparse
import json
from pathlib import Path
import queue
import re
import subprocess
import threading
import time
from types import SimpleNamespace
from run_qemu_display_settings import DisplayProof, validate_faults
from run_qemu_smoke import (qemu_command, open_injection_listener, configure_qemu_host_timers,
                           stop_process, SHELL_PROMPT)
from run_qemu_runtime_desktop import (BrowserInputMonitor, reader, drain, read_ppm,
                                    browser_probe_wait_pointer, SHELL_HELP_MARKER)
from measure_cpp_baseline import suppress_windows_test_dialogs


class MouseProof(DisplayProof):
    def __init__(self, *args):
        super().__init__(*args)
        self.speed, self.primary, self.rx, self.ry = 100, 1, 0, 0

    def text(self):
        text = super().text()
        if 'MOUSE_APPLET_LAUNCH_FAILED' in text:
            raise RuntimeError('Mouse applet launch failed')
        return text

    def raw_move(self, dx, dy):
        self.monitor.mouse(self.process, f'mouse_move {dx} {dy}')
        # Independent rational expected output; no guest geometry telemetry
        # can substitute for the actual software-pointer pixels below.
        vx, vy = dx*self.speed+self.rx, dy*self.speed+self.ry
        sx, sy = int(vx/100), int(vy/100)
        self.rx, self.ry = vx-sx*100, vy-sy*100
        self.x = max(0, min(self.width-1, self.x+sx))
        self.y = max(0, min(self.height-1, self.y+sy))
        time.sleep(.04)

    def move(self, x, y):
        for _ in range(48):
            dx, dy = x-self.x, y-self.y
            if abs(dx)<=2 and abs(dy)<=2:
                return
            rawx, rawy = int(dx*100/self.speed), int(dy*100/self.speed)
            self.raw_move(max(-80,min(80,rawx)), max(-80,min(80,rawy)))
        raise RuntimeError('Mouse trajectory quota')

    def click(self, x, y):
        self.move(x,y)
        for button in (self.primary, 0):
            if self.primary==2:
                # The unchanged browser helper deliberately admits left only.
                # Exercise the real secondary HID button through native QMP.
                self.monitor.execute('input-send-event', {'events': [
                    {'type':'btn','data':{'button':'right','down':button!=0}}]})
            else:
                self.monitor.mouse(self.process, f'mouse_button {button}')
            time.sleep(.08)

    def desktop(self, expected=(100,0,0,0,500)):
        offset=len(self.text())
        self.monitor.type_text('desktop.prg --mouse-probe'); self.monitor.key('ret')
        mode=self.wait(r'DESKTOP_MODE_ACTIVE width=(\d+) height=(\d+) bpp=32',offset)
        self.width,self.height=int(mode[1]),int(mode[2]); self.x,self.y=self.width//2,self.height//2
        match=self.wait(r'MOUSE_SETTINGS_ACTIVE speed=(\d+) profile=(\d+) right=(\d+) natural=(\d+) double=(\d+)',offset)
        if tuple(map(int,match.groups()))!=expected:
            raise RuntimeError('Wrong effective startup mouse settings '+str(match.groups()))
        self.speed=expected[0]; self.primary=2 if expected[2] else 1; self.rx=self.ry=0
        self.wait('DESKTOP_CONTROL_OK',offset); self.wait('CONTROL_PANEL_READY',offset)
        self.control_pid,control=self.window('CONTROL',offset)
        point=self.wait(r'DISPLAY_PROBE_START_POINT x=(\d+) y=(\d+)',offset)
        self.start_point=int(point[1]),int(point[2])
        return control

    def profile(self, expected, offset=0):
        speed,profile,right,natural,double=expected
        self.wait(fr'MOUSE_DRAFT_READY speed={speed} profile={profile} right={right} natural={natural} double={double}\r?\n',offset)

    def open_applet(self, control, selected=False, physical=False, expected=(100,0,0,0,500)):
        offset=len(self.text())
        x,y,_,_=control
        if physical:
            if not selected: self.click(x+90,y+137)
            self.click(x+260,y+145)
        else:
            self.monitor.key('down'); self.monitor.key('ret')
        self.wait('MOUSE_APPLET_LAUNCHED',offset); self.wait('MOUSE_APPLET_PAINTED',offset)
        self.applet_pid,rect=self.window('APPLET',offset)
        self.profile(expected,offset)
        return rect

    def screenshot(self, name, rect=None):
        path=super().screenshot(name)
        if rect:
            ppm=read_ppm(path); x,y,w,h=rect
            pixels=b''.join(ppm[2][((y+row)*ppm[0]+x+16)*3:((y+row)*ppm[0]+x+w-16)*3]
                            for row in range(42,min(h-70,340)))
            for color in ('ffffff','202020','c8c8c8'):
                if pixels.count(bytes.fromhex(color))<48:
                    raise RuntimeError('Native mouse controls missing pixels: '+color)
        return path

    def close_applet(self):
        offset=len(self.text()); self.monitor.key('esc')
        self.wait('MOUSE_APPLET_CLOSED',offset)
        self.wait(r'DISPLAY_PROBE_APPLET_RETIRED pid='+str(self.applet_pid)+r'\r?\n',offset)

    def close_session(self, applet=True):
        if applet: self.close_applet()
        offset=len(self.text()); self.monitor.key('esc')
        self.wait(r'DISPLAY_PROBE_CONTROL_RETIRED pid='+str(self.control_pid)+r'\r?\n',offset)
        offset=len(self.text()); self.click(*self.start_point)
        point=self.wait(r'DISPLAY_PROBE_MENU_READY x=(\d+) y=(\d+)',offset)
        self.click(int(point[1]),int(point[2])); self.wait('DESKTOP_EXIT_OK',offset)
        self.command('help',SHELL_HELP_MARKER)

    def save(self, rect):
        offset=len(self.text()); x,y,_,h=rect; self.click(x+60,y+h-53)
        self.wait('MOUSE_SETTINGS_SAVED',offset)
        if 'MOUSE_SETTINGS_ACTIVE' in self.text()[offset:]:
            raise RuntimeError('Unexpected live policy replacement')

    def run(self):
        first=self.wait(r'DESKTOP_OK|'+re.escape(SHELL_PROMPT))
        if first[0]=='DESKTOP_OK':
            mode=re.findall(r'DESKTOP_MODE_ACTIVE width=(\d+) height=(\d+)',self.text())[-1]
            self.width,self.height=map(int,mode); self.x,self.y=self.width//2,self.height//2
            self.close_desktop(False)
        self.command('mouse --list','MOUSE_COMMAND_READY')
        control=self.desktop(); rect=self.open_applet(control)
        self.screenshot('initial',rect); x,y,w,h=rect
        offset=len(self.text())
        self.click(x+230+257,y+60)  # floor(175*257/359)+25 =150
        self.monitor.key('tab'); self.monitor.key('spc')
        self.click(x+30,y+205)
        self.click(x+230,y+255)  # Minimum200, then eleven50ms keyboard steps.
        for _ in range(11): self.monitor.key('right')
        wanted=(150,0,1,1,750); self.profile(wanted,offset)
        self.click(x+100,y+314); self.click(x+100,y+314)
        self.wait('MOUSE_TEST_DOUBLE_OK',offset)
        self.save(rect); self.screenshot('saved-not-live',rect)
        self.close_applet()
        rect=self.open_applet(control,selected=True,physical=True,expected=wanted)
        fault_pid=self.applet_pid; offset=len(self.text()); self.monitor.key('ctrl-g')
        self.wait('MOUSE_APPLET_FAULT',offset)
        self.wait(r'USER PROCESS EXCEPTION[\s\S]*Process terminated\.',offset)
        self.wait(r'DISPLAY_PROBE_APPLET_RETIRED pid='+str(fault_pid)+r'\r?\n',offset)
        rect=self.open_applet(control,selected=True,physical=True,expected=wanted)
        replacement=self.applet_pid
        if replacement==fault_pid: raise RuntimeError('Faulted mouse generation reused')
        self.screenshot('fault-replacement',rect); self.close_session()

        control=self.desktop(wanted)
        self.raw_move(20,12)
        browser_probe_wait_pointer(self.monitor,self.directory/'scaled.ppm',(self.width//2+30,self.height//2+18),'150-percent')
        rect=self.open_applet(control,physical=True,expected=wanted)  # Real right-button primary.
        self.screenshot('effective-right-primary',rect)
        x,y,w,h=rect; self.move(x+100,y+310)
        offset=len(self.text()); self.monitor.mouse(self.process,'mouse_move 0 0 -1')
        self.wait(r'MOUSE_WHEEL delta=-120\r?\n',offset)
        offset=len(self.text()); self.click(x+220,y+h-53)  # Standard button.
        self.profile((100,0,0,0,500),offset); self.save(rect); self.close_session()
        self.command('mouse --list','MOUSE_SETTINGS_SAVED_PROFILE speed=100 profile=0 right=0 natural=0 double=500')
        self.desktop(); self.close_session(False)
        validate_faults(self.text().replace('MOUSE_APPLET_FAULT','DISPLAY_APPLET_FAULT'))
        return {'profile':wanted,'pointer_gain':1.5,'physical_primary':'right','natural_wheel':-120,
                'fault_pid':fault_pid,'replacement_pid':replacement,'defaults_restored':True,'shell_return':True}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qemu',type=Path)
    parser.add_argument('--image',type=Path,required=True)
    parser.add_argument('--evidence',type=Path,required=True)
    parser.add_argument('--verify-artifacts',action='store_true')
    args=parser.parse_args()
    if args.evidence.exists(): parser.error('refusing to overwrite evidence')
    args.evidence.mkdir(parents=True); suppress_windows_test_dialogs()
    if args.verify_artifacts:
        from run_qemu_browser_layout import artifacts
        from verify_text_artifacts import read_fat_file
        status=artifacts(SimpleNamespace(image=args.image,log=args.evidence/'protected.json',resize_inset=12))
        if status: return status
        root=Path(__file__).resolve().parents[1]
        for path in (args.image,root/'build/vmware/reist-os/reist-os-flat.vmdk'):
            for name,program in (('mouse','MOUSE'),('control','CONTROL')):
                if read_fat_file(path,'usr/gui/bin/'+name+'.prg')!=(root/'build/programs'/f'{program}.PRG').read_bytes():
                    raise RuntimeError('Applet package differs')
            # This independent reader walks 8.3 entries, not VFAT long names.
            # input.conf has the image's canonical INPUT~1.CON alias; the
            # guest proof separately opens the full /etc/reist/input.conf.
            if read_fat_file(path,'etc/reist/input~1.con')!=(root/'config/etc/reist/input.conf').read_bytes():
                raise RuntimeError('Input defaults differ')
        print('MOUSE_ARTIFACTS PASS'); return 0
    if args.qemu is None: parser.error('--qemu required')
    started=time.monotonic(); deadline=started+180
    listener,port=open_injection_listener()
    command=qemu_command(args.qemu,args.image,memory='1024M',smp=1)
    command+=['-device','VGA','-device','qemu-xhci,id=reistxhci','-device','usb-mouse,bus=reistxhci.0',
              '-qmp',f'tcp:127.0.0.1:{port},server=off,nodelay=on']
    process=monitor=None; transcript=[]; output=queue.Queue(); result={'passed':False}
    try:
        process=subprocess.Popen(command,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
            text=True,encoding='utf-8',errors='replace',bufsize=0,
            creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0)|getattr(subprocess,'BELOW_NORMAL_PRIORITY_CLASS',0))
        configure_qemu_host_timers(process)
        threading.Thread(target=reader,args=(process.stdout,output,threading.Event()),daemon=True).start()
        monitor=BrowserInputMonitor.accept(listener,deadline)
        proof=MouseProof(process,monitor,output,transcript,args.evidence,deadline)
        result.update(proof.run()); result['passed']=True
    except (OSError,RuntimeError,TimeoutError,AssertionError,ValueError) as error: result['error']=str(error)
    finally:
        listener.close()
        if monitor: monitor.peer.close()
        if process:
            stop_process(process)
            for pipe in (process.stdin,process.stdout):
                if pipe: pipe.close()
        drain(output,transcript); result['elapsed_seconds']=round(time.monotonic()-started,3)
        result['command']=command
        (args.evidence/'serial.log').write_text(''.join(transcript),encoding='utf-8')
        (args.evidence/'status.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print('MOUSE_RUNTIME '+('PASS' if result['passed'] else 'FAIL')+
          f" elapsed={result['elapsed_seconds']}s "+result.get('error',''))
    return 0 if result['passed'] else 1


if __name__=='__main__': raise SystemExit(main())
