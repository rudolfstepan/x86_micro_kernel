"""R3.32 real caption/taskbar input, retained clients and window-state proof."""
import argparse
import hashlib
import json
from pathlib import Path
import queue
import re
import subprocess
import threading
import time
from types import SimpleNamespace
from run_qemu_mouse_settings import MouseProof
from run_qemu_display_settings import validate_faults
from run_qemu_smoke import (qemu_command, open_injection_listener, configure_qemu_host_timers,
                           stop_process, SHELL_PROMPT)
from run_qemu_runtime_desktop import BrowserInputMonitor, reader, drain, read_ppm, SHELL_HELP_MARKER
from measure_cpp_baseline import suppress_windows_test_dialogs


def artifacts(args):
    from run_qemu_browser_layout import artifacts as protected
    from verify_text_artifacts import read_fat_file
    report_path=args.evidence/'protected.json'
    if protected(SimpleNamespace(image=args.image,log=report_path,resize_inset=12)):
        return 1
    report=json.loads(report_path.read_text())
    pinned={
        'usr/gui/bin/mouse.prg':'d9cd4514119a6a3497f2c6718de71ce3049bb1c7c208709ce55e48ee77fbfcb8',
        'usr/gui/bin/control.prg':'d594bfc7dd789c2f04c7dd0d381576bb18280bc33787440262a838baa7ab26dd',
        'sbin/config.prg':'8162522065a1d853f66028ae4080cc6eba19530f8deee5d03991f3498a84f7d6',
        'usr/gui/bin/display.prg':'95c6dc9971c46c9e46b60664f2afcdebc070bd1d626d4fafeb511119298b0463'}
    report.update(window_baseline='ef7f7d2f',window_protected={})
    root=Path(__file__).resolve().parents[1]
    for image in (args.image,root/'build/vmware/reist-os/reist-os-flat.vmdk'):
        for name,wanted in pinned.items():
            actual=hashlib.sha256(read_fat_file(image,name)).hexdigest()
            if actual!=wanted: raise RuntimeError('Protected window program differs: '+name)
            report['window_protected'][str(image)+'/'+name]=actual
    report_path.write_text(json.dumps(report,indent=2)+'\n')
    print('WINDOW_ARTIFACTS PASS kernels+13-programs'); return 0


class WindowProof(MouseProof):
    def desktop(self):
        offset=len(self.text())
        self.monitor.type_text('desktop.prg --window-probe'); self.monitor.key('ret')
        mode=self.wait(r'DESKTOP_MODE_ACTIVE width=(\d+) height=(\d+) bpp=32',offset)
        self.width,self.height=map(int,mode.groups()); self.x,self.y=self.width//2,self.height//2
        self.wait('DESKTOP_CONTROL_OK',offset); self.wait('CONTROL_PANEL_READY',offset)
        self.control_pid,control=self.window('CONTROL',offset)
        work=self.wait(r'WINDOW_WORK x=(\d+) y=(\d+) w=(\d+) h=(\d+)',offset)
        self.work=tuple(map(int,work.groups()))
        point=self.wait(r'DISPLAY_PROBE_START_POINT x=(\d+) y=(\d+)',offset)
        self.start_point=int(point[1]),int(point[2])
        return control

    def state(self,offset=0,**wanted):
        limit=min(self.deadline,time.monotonic()+8)
        while time.monotonic()<limit:
            latest={}
            for line in re.findall(r'WINDOW_STATE ([^\r\n]+)\r?\n',self.text()[offset:]):
                row={key:int(value) for key,value in re.findall(r'(\w+)=(\d+)',line)}
                latest[row['slot']]=row
            for row in latest.values():
                if all(row.get(key)==value for key,value in wanted.items()):
                    return row
            time.sleep(.02)
        raise RuntimeError('Window state deadline: '+str(wanted))

    @staticmethod
    def geometry(state):
        return tuple(state[key] for key in ('x','y','w','h'))

    def settled(self,offset,**wanted):
        limit=min(self.deadline,time.monotonic()+8)
        while time.monotonic()<limit:
            state=self.state(offset,**wanted)
            if state['configured']==state['acked'] and (
                state['cw']==state['w']-6 and state['ch']==state['h']-30):
                return state
            time.sleep(.02)
        raise RuntimeError('Configure/ACK geometry did not settle')

    def caption(self,state,button,**wanted):
        offset=len(self.text()); self.click(state[button+'x'],state[button+'y'])
        return self.settled(offset,slot=state['slot'],**wanted)

    def task(self,state,**wanted):
        offset=len(self.text()); self.click(state['taskx'],state['tasky'])
        return self.settled(offset,slot=state['slot'],**wanted)

    def scan(self,name,state,applet=True):
        # Keep the software pointer out of the caption glyph comparison.
        self.move(state['x']+100,state['y']+80)
        # Reference VGA font: 24px title plus 3px frame on each side.
        rect=(state['x']+3,state['y']+27,state['cw'],state['ch'])
        path=self.screenshot(name,rect if applet else None)
        width,height,pixels=read_ppm(path)
        if (width,height)!=(self.width,self.height): raise RuntimeError('Wrong scanout')
        x,y=state['maxx']-8,state['maxy']-8
        tile=b''.join(pixels[((y+dy)*width+x)*3:((y+dy)*width+x+16)*3] for dy in range(16))
        if tile.count(bytes.fromhex('181818'))<8:
            raise RuntimeError('Caption glyph missing')
        return tile

    def run(self):
        first=self.wait(r'DESKTOP_OK|'+re.escape(SHELL_PROMPT))
        if first[0]=='DESKTOP_OK':
            self.close_desktop(False)
        control=self.desktop()
        self.open_applet(control)
        pid=self.applet_pid
        normal=self.settled(0,pid=pid,visible=1,min=0,max=0)
        bounds=self.geometry(normal); generation=normal['gen']
        normal_pixels=self.scan('normal',normal)
        offset=len(self.text()); self.monitor.key('right')
        self.profile((125,0,0,0,500),offset)
        # Down in one button, outside release: no geometry or client action.
        offset=len(self.text()); self.move(normal['maxx'],normal['maxy'])
        self.monitor.mouse(self.process,'mouse_button 1')
        self.state(offset,pid=pid,capture=6,armed=1)
        self.move(normal['maxx']-45,normal['maxy'])
        self.monitor.mouse(self.process,'mouse_button 0')
        cancelled=self.settled(offset,pid=pid,capture=0,min=0,max=0)
        if self.geometry(cancelled)!=bounds: raise RuntimeError('Cancelled caption changed geometry')
        maximum=self.caption(cancelled,'max',visible=1,min=0,max=1,capture=0)
        if self.geometry(maximum)!=self.work: raise RuntimeError('Maximize ignored work area')
        maximum_pixels=self.scan('maximized',maximum)
        if maximum_pixels==normal_pixels: raise RuntimeError('Restore glyph did not change')
        # Maximized title drags must neither move nor resize the window.
        offset=len(self.text()); self.move(maximum['x']+100,maximum['y']+14)
        self.monitor.mouse(self.process,'mouse_button 1'); self.raw_move(50,30)
        self.monitor.mouse(self.process,'mouse_button 0'); time.sleep(.15)
        if self.geometry(self.state(pid=pid))!=self.work: raise RuntimeError('Maximized drag moved geometry')
        offset=len(self.text())
        hidden=self.caption(maximum,'min',visible=0,min=1,max=1,capture=0)
        self.screenshot('minimized')
        if hidden['focus'] or hidden['pid']!=pid or hidden['gen']!=generation:
            raise RuntimeError('Minimize lost retained identity/focus')
        if 'MOUSE_APPLET_CLOSED' in self.text()[offset:]: raise RuntimeError('Minimize sent close')
        maximum=self.task(hidden,pid=pid,visible=1,min=0,max=1,focus=1)
        if self.geometry(maximum)!=self.work: raise RuntimeError('Taskbar lost maximized state')
        self.scan('maximized-restored',maximum)
        restored=self.caption(maximum,'max',pid=pid,visible=1,min=0,max=0)
        if self.geometry(restored)!=bounds: raise RuntimeError('Normal bounds lost')
        if self.scan('normal-restored',restored)!=normal_pixels: raise RuntimeError('Normal glyph not restored')
        hidden=self.task(restored,pid=pid,visible=0,min=1,max=0)
        restored=self.task(hidden,pid=pid,visible=1,min=0,max=0)
        if self.geometry(restored)!=bounds or restored['gen']!=generation: raise RuntimeError('Taskbar recreated window')
        offset=len(self.text()); self.monitor.key('right')
        self.profile((150,0,0,0,500),offset)  # Private draft survived both states.
        self.caption(restored,'max',pid=pid,visible=1,max=1)
        offset=len(self.text()); self.monitor.key('ctrl-g')
        self.wait('MOUSE_APPLET_FAULT',offset)
        self.wait(r'USER PROCESS EXCEPTION[\s\S]*Process terminated\.',offset)
        self.wait(r'DISPLAY_PROBE_APPLET_RETIRED pid='+str(pid)+r'\r?\n',offset)
        self.open_applet(control,selected=True,physical=True)
        replacement=self.settled(offset,pid=self.applet_pid,visible=1,min=0,max=0,capture=0)
        if replacement['gen']==generation or self.applet_pid==pid: raise RuntimeError('Stale window incarnation')
        self.scan('replacement',replacement); self.close_applet()
        offset=len(self.text()); self.monitor.key('esc')
        self.wait(r'DISPLAY_PROBE_CONTROL_RETIRED pid='+str(self.control_pid)+r'\r?\n',offset)
        explorer=self.settled(0,slot=0,pid=0,visible=1,min=0,max=0)
        explorer_bounds=self.geometry(explorer)
        explorer=self.caption(explorer,'max',visible=1,max=1)
        if self.geometry(explorer)!=self.work: raise RuntimeError('Explorer maximize mismatch')
        self.scan('explorer-maximized',explorer,False)
        explorer=self.caption(explorer,'min',visible=0,min=1)
        explorer=self.task(explorer,visible=1,min=0,max=1)
        explorer=self.caption(explorer,'max',visible=1,max=0)
        if self.geometry(explorer)!=explorer_bounds: raise RuntimeError('Explorer normal bounds lost')
        self.scan('explorer-restored',explorer,False)
        offset=len(self.text()); self.click(*self.start_point)
        point=self.wait(r'DISPLAY_PROBE_MENU_READY x=(\d+) y=(\d+)',offset)
        self.click(int(point[1]),int(point[2])); self.wait('DESKTOP_EXIT_OK',offset)
        self.command('help',SHELL_HELP_MARKER)
        validate_faults(self.text().replace('MOUSE_APPLET_FAULT','DISPLAY_APPLET_FAULT'))
        return {'normal_bounds':bounds,'work_area':self.work,'retained_pid':pid,
                'replacement_pid':self.applet_pid,'generation':generation,
                'replacement_generation':replacement['gen'],'explorer':True,'shell_return':True}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qemu',type=Path)
    parser.add_argument('--image',type=Path,required=True)
    parser.add_argument('--evidence',type=Path,required=True)
    parser.add_argument('--verify-artifacts',action='store_true')
    args=parser.parse_args()
    if args.evidence.exists(): parser.error('refusing to overwrite evidence')
    args.evidence.mkdir(parents=True); suppress_windows_test_dialogs()
    if args.verify_artifacts: return artifacts(args)
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
        proof=WindowProof(process,monitor,output,transcript,args.evidence,deadline)
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
    print('WINDOW_RUNTIME '+('PASS' if result['passed'] else 'FAIL')+
          f" elapsed={result['elapsed_seconds']}s "+result.get('error',''))
    return 0 if result['passed'] else 1


if __name__=='__main__': raise SystemExit(main())
