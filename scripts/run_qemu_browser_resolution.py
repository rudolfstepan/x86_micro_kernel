"""R3.33 actual high-resolution configure, browser reflow, pixels and recovery."""
import argparse
import hashlib
import json
from pathlib import Path
import queue
import re
import subprocess
import threading
import time
from run_qemu_window_controls import WindowProof
from run_qemu_display_settings import validate_faults
from run_qemu_smoke import qemu_command,open_injection_listener,configure_qemu_host_timers,stop_process,SHELL_PROMPT
from run_qemu_runtime_desktop import BrowserInputMonitor,reader,drain,read_ppm
from measure_cpp_baseline import suppress_windows_test_dialogs
ROOT=Path(__file__).resolve().parents[1]

def artifacts(args):
    from verify_text_artifacts import read_fat_file,image_program_path
    from run_qemu_math import digest,kernel_digest
    pinned={
        'BENCHMARK.PRG':'b001fb18597e4122dc1dad928649c8c281c71bea0cee7b19887074e13facbfb3',
        'MATHTEST.PRG':'0bee6c4057aac105bb7eb87f63869902ccde11078fc40c69f258430b77c467c6',
        'TEXTTEST.PRG':'63776333af8e28e97e5a91196826194c471893d2fe3180f2d27f50ce202cf279',
        'CURL.PRG':'aa3e619cade08e7172ebee80f192c0db728a0a5b987b23319e11448c6fc1b7cc',
        'JSTEST.PRG':'723765e1e695274750366d36858b1c5812239033c6e9847616fca2357904d440',
        'JSWORK.PRG':'4bc4bd9d4f2913e00adbaa04e39b61f5f0888f93f0c5272889a8b6064b3516d0'}
    report={'baseline':'bd421a50','passed':False,'programs':pinned,'images':{}}
    for profile,path,kernel in (
        ('qemu',args.image,ROOT/'build/kernel.bin'),
        ('vmware',ROOT/'build/vmware/reist-os/reist-os-flat.vmdk',ROOT/'build/codex-agent/r333-resolution/kernel-vmware.bin')):
        actual=kernel_digest(path)
        if actual!=digest(kernel): raise RuntimeError('Image/reference kernel mismatch '+profile)
        for name,wanted in pinned.items():
            if hashlib.sha256(read_fat_file(path,image_program_path(name))).hexdigest()!=wanted:
                raise RuntimeError('Protected program changed '+profile+'/'+name)
        for name,guest in [('BROWSER.PRG','usr/gui/bin/browser.prg'),('HTMLWORK.PRG','usr/bin/htmlwork.prg'),('DESKTOP.PRG','usr/gui/bin/desktop.prg')]:
            if read_fat_file(path,guest)!=(ROOT/'build/programs'/name).read_bytes():
                raise RuntimeError('Stale repaired program '+profile+'/'+name)
        report['images'][profile]={'sha256':digest(path),'kernel_sha256':actual}
    report['passed']=True
    (args.evidence/'protected.json').write_text(json.dumps(report,indent=2)+'\n')
    print('RESOLUTION_ARTIFACTS PASS');return 0

class ResolutionProof(WindowProof):
    def state(self,offset=0,**wanted):
        limit=min(self.deadline,time.monotonic()+8)
        while time.monotonic()<limit:
            latest={}
            for line in re.findall(r'WINDOW_STATE ([^\r\n]+)\r?\n',self.text()[offset:]):
                row={key:int(value) for key,value in re.findall(r'(\w+)=(\d+)',line)}
                # An interrupted diagnostic must not replace a complete record.
                if len(row)==24:latest[row['slot']]=row
            for row in latest.values():
                if all(row.get(k)==v for k,v in wanted.items()):return row
            time.sleep(.02)
        raise RuntimeError('Window state deadline: '+str(wanted))

    def text(self):
        value=super().text()
        if any(x in value for x in ('BROWSER_PROBE_FAIL','BROWSER_RUNTIME_FAILURE','DESKTOP_BROWSER_FAIL')):
            raise RuntimeError('Browser/desktop failure')
        return value

    def viewport(self,offset,state,scroll=None):
        limit=min(self.deadline,time.monotonic()+12)
        next_query=time.monotonic()+.75
        while time.monotonic()<limit:
            lines=re.findall(r'BROWSER_VIEWPORT ([^\r\n]+)\r?\n',self.text()[offset:])
            if lines:
                row={k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',lines[-1])}
                w,h=state['cw'],state['ch']
                if len(row)==8 and (row['width'],row['height'],row['scene'],row['view'],row['bufferw'],row['bufferh'])==(w,h,w-18,h-98,w,h):
                    if scroll is None or row['scroll']==scroll: return row
            if time.monotonic()>=next_query:
                self.monitor.key('ctrl-p');next_query=time.monotonic()+.75
            time.sleep(.02)
        raise RuntimeError('Browser viewport/buffer did not follow acknowledged frame '+str((state['cw'],state['ch'])))

    def open_browser(self):
        offset=len(self.text())
        self.monitor.type_text('desktop.prg --browser-window-probe');self.monitor.key('ret')
        match=self.wait(r'DESKTOP_MODE_ACTIVE width=(\d+) height=(\d+) bpp=32',offset)
        self.width,self.height=map(int,match.groups());self.x,self.y=self.width//2,self.height//2
        if (self.width,self.height)!=tuple(map(int,self.mode.split('x'))): raise RuntimeError('Requested mode not active')
        match=self.wait(r'WINDOW_WORK x=(\d+) y=(\d+) w=(\d+) h=(\d+)',offset)
        self.work=tuple(map(int,match.groups()))
        self.wait('BROWSER_INPUT_READY',offset)
        state=self.settled(offset,visible=1,focus=1,min=0,max=0)
        if not state['pid']: raise RuntimeError('Browser process missing')
        offset=len(self.text());self.monitor.key('v')
        self.wait('BROWSER_VIEWPORT',offset)
        self.viewport(offset,state,0)
        return state

    def pixels(self,name,state):
        self.move(state['x']+50,state['y']+105)
        limit=min(self.deadline,time.monotonic()+5)
        for attempt in range(20):
            if time.monotonic()>limit:break
            path=self.screenshot(name+'-'+str(attempt))
            sw,sh,data=read_ppm(path);x,y=state['x']+3,state['y']+27;w,h=state['cw'],state['ch']
            def stripe(xx,yy,color):return data[(yy*sw+xx)*3:(yy*sw+xx+8)*3]==bytes.fromhex(color)*8
            # Far edge address field, page background and bottom status must
            # move with the actual client, not remain at800x600.
            if ((sw,sh)==(self.width,self.height) and stripe(x+w-48,y+24,'ffffff') and
                stripe(x+w-48,y+100,'f9f8f5') and stripe(x+w-48,y+h-10,'d4d0c8')):
                if state['max']:
                    first=[];color=bytes.fromhex('dcebd6')*4
                    for yy in range(y+76,y+h-22):
                        row=data[(yy*sw+x)*3:(yy*sw+x+w-18)*3];at=row.find(color)
                        if at>=0:first.append(at//3)
                    expected=(w-18-720)//2+16
                    if not first or min(first)!=expected:continue
                return
        raise RuntimeError('High-resolution browser scanout mismatch '+name)

    def run(self):
        first=self.wait(r'DESKTOP_OK|'+re.escape(SHELL_PROMPT))
        if first[0]=='DESKTOP_OK':self.close_desktop(False)
        self.command('config set desktop resolution '+self.mode,'CONFIG_UPDATE_OK')
        normal=self.open_browser();pid=normal['pid'];bounds=self.geometry(normal)
        self.pixels('normal',normal)
        state=normal
        for cycle in range(3):
            offset=len(self.text());state=self.caption(state,'max',pid=pid,max=1)
            if self.geometry(state)!=self.work:raise RuntimeError('Wrong maximized frame')
            self.viewport(offset,state);self.pixels('max'+str(cycle),state)
            offset=len(self.text());state=self.caption(state,'max',pid=pid,max=0)
            if self.geometry(state)!=bounds:raise RuntimeError('Restore lost normal bounds')
            self.viewport(offset,state);self.pixels('restore'+str(cycle),state)
        # Normal window to upper-left, then real bottom-right resize to wide
        # but scrollable geometry even on a1440px desktop.
        offset=len(self.text());self.move(state['x']+100,state['y']+15)
        self.monitor.mouse(self.process,'mouse_button 1');self.move(104,19)
        self.monitor.mouse(self.process,'mouse_button 0');time.sleep(.1)
        state=self.settled(offset,pid=pid,x=4,y=4)
        offset=len(self.text());self.move(state['x']+state['w']-2,state['y']+state['h']-2)
        self.monitor.mouse(self.process,'mouse_button 1');self.move(self.width-6,min(self.height-38,780))
        self.monitor.mouse(self.process,'mouse_button 0')
        time.sleep(.1);state=self.settled(offset,pid=pid,capture=0)
        if state['cw']<=1024:raise RuntimeError('Resize did not cross old width limit')
        self.viewport(offset,state);self.pixels('wide-resized',state)
        self.move(state['x']+100,state['y']+150)
        for cycle in range(2):
            for delta,scroll in ((-1,192),(1,0)):
                offset=len(self.text())
                for _ in range(4):self.monitor.mouse(self.process,'mouse_move 0 0 '+str(delta))
                self.viewport(offset,state,scroll)
        self.pixels('wide-scrolled-back',state)
        offset=len(self.text());self.monitor.key('ctrl-g')
        self.wait('BROWSER_RESOLUTION_FAULT',offset)
        self.wait(r'USER PROCESS EXCEPTION[\s\S]*Process terminated\.',offset)
        self.wait('TERMINAL_INPUT_IDLE',offset)
        self.close_desktop(False)
        replacement=self.open_browser()
        if replacement['pid']==pid:raise RuntimeError('Browser identity reused')
        offset=len(self.text());replacement=self.caption(replacement,'max',max=1)
        self.viewport(offset,replacement);self.pixels('replacement-max',replacement)
        self.close_desktop(False)
        validate_faults(self.text().replace('BROWSER_RESOLUTION_FAULT','DISPLAY_APPLET_FAULT'))
        return {'mode':self.mode,'normal':bounds,'work_area':self.work,'pid':pid,
                'replacement_pid':replacement['pid'],'max_restore_cycles':3,'wheel_cycles':2,'shell_return':True}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify-artifacts',action='store_true');parser.add_argument('--qemu',type=Path)
    parser.add_argument('--image',type=Path,required=True);parser.add_argument('--evidence',type=Path,required=True)
    parser.add_argument('--mode',choices=('1600x900','2560x1440'),default='1600x900')
    args=parser.parse_args()
    if args.evidence.exists():parser.error('refusing to overwrite evidence')
    args.evidence.mkdir(parents=True);suppress_windows_test_dialogs()
    if args.verify_artifacts:return artifacts(args)
    if args.qemu is None:parser.error('--qemu required')
    started=time.monotonic();deadline=started+180;listener,port=open_injection_listener()
    command=qemu_command(args.qemu,args.image,memory='1024M',smp=1)
    command+=['-device','VGA','-device','qemu-xhci,id=reistxhci','-device','usb-mouse,bus=reistxhci.0',
              '-qmp',f'tcp:127.0.0.1:{port},server=off,nodelay=on']
    process=monitor=None;transcript=[];output=queue.Queue();result={'passed':False}
    try:
        process=subprocess.Popen(command,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
            text=True,encoding='utf-8',errors='replace',bufsize=0,
            creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0)|getattr(subprocess,'BELOW_NORMAL_PRIORITY_CLASS',0))
        configure_qemu_host_timers(process)
        threading.Thread(target=reader,args=(process.stdout,output,threading.Event()),daemon=True).start()
        monitor=BrowserInputMonitor.accept(listener,deadline)
        proof=ResolutionProof(process,monitor,output,transcript,args.evidence,deadline);proof.mode=args.mode
        result.update(proof.run());result['passed']=True
    except (OSError,RuntimeError,TimeoutError,AssertionError,ValueError) as error:result['error']=str(error)
    finally:
        listener.close()
        if monitor:monitor.peer.close()
        if process:
            stop_process(process)
            for pipe in (process.stdin,process.stdout):
                if pipe:pipe.close()
        drain(output,transcript);result['elapsed_seconds']=round(time.monotonic()-started,3);result['command']=command
        (args.evidence/'serial.log').write_text(''.join(transcript),encoding='utf-8')
        (args.evidence/'status.json').write_text(json.dumps(result,indent=2)+'\n')
    print('RESOLUTION_RUNTIME '+('PASS' if result['passed'] else 'FAIL')+
          f" elapsed={result['elapsed_seconds']}s "+result.get('error',''))
    return 0 if result['passed'] else 1

if __name__=='__main__':raise SystemExit(main())
