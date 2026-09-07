#!/usr/bin/env python3
"""Real desktop JS, replay/navigation/failure proof; preserved4b2b3302 payloads."""
import argparse,hashlib,inspect,json,re,time
from pathlib import Path
import run_qemu_runtime_desktop as desktop
from measure_cpp_baseline import suppress_windows_test_dialogs

def validate_transcript(text):
    markers=['BROWSER_SCRIPT_DOM_OK','BROWSER_SCRIPT_REFLOW_OK','BROWSER_SCRIPT_NAVIGATION_OK',
        'JS_SERVICE_HANG_ENTERED','BROWSER_SCRIPT_HANG_CONTAINED_OK','JS_SERVICE_FAULT_ENTERED',
        'BROWSER_SCRIPT_FAULT_CONTAINED_OK','BROWSER_SCRIPT_RECOVERY_OK','HOST_BROWSER_SCRIPT_TITLE_PIXELS_OK','BROWSER_CLOSE_OK',
        'HOST_BROWSER_SCRIPT_RESTART_OK']
    at=0
    for marker in markers:
        found=text.find(marker,at)
        if found<0: raise ValueError('missing ordered proof '+marker)
        at=found+len(marker)
    rows=re.findall(r'BROWSER_JS_WORKER pid=(\d+) generation=(\d+) fixture=(\d+)',text)
    if len(rows)!=5 or [r[2] for r in rows]!=['0','0','2','1','0'] or len({r[:2] for r in rows})!=5:
        raise ValueError('missing/stale JS worker ownership')
    if text.count('*** USER PROCESS PAGE FAULT ***')!=1 or 'Faulting address: 0x00000004' not in text:
        raise ValueError('actual fault not proved')
    if any(s in text for s in ('BROWSER_PROBE_FAIL','DESKTOP_BROWSER_FAIL','KERNEL PANIC','kernel panic')):
        raise ValueError('guest failure')

def probe(process,output,transcript,screenshot,deadline,monitor):
    def wait(pattern,offset=0):
        while time.monotonic()<deadline:
            desktop.drain(output,transcript); text=''.join(transcript)
            if any(s in text for s in ('BROWSER_PROBE_FAIL','DESKTOP_BROWSER_FAIL','KERNEL PANIC','kernel panic')):
                raise RuntimeError('script guest failure: '+text[-3000:])
            match=re.search(pattern,text[offset:])
            if match: return match
            time.sleep(.01)
        raise RuntimeError('script deadline: '+pattern)
    def capture(label,limit):
        path=screenshot.with_name(screenshot.stem+'-'+label+'.ppm')
        if path.exists(): raise RuntimeError('existing screenshot evidence')
        monitor.execute('screendump',{'filename':str(path.resolve())},limit)
        ppm=desktop.read_ppm(path)
        if ppm is None: raise RuntimeError('invalid framebuffer')
        return ppm,path
    def exit_desktop(label):
        # The accepted exit helper starts at screen centre. The resize probe
        # moved the pointer; establish and observe that precondition explicitly.
        ppm,_=capture(label,deadline)
        for _ in range((max(ppm[0],ppm[1])+119)//120):
            monitor.mouse(process,'mouse_move -120 -120'); time.sleep(.01)
        desktop.browser_probe_wait_pointer(monitor,screenshot,(0,0),label+'-home')
        pointer=[0,0]; centre=(ppm[0]//2,ppm[1]//2)
        desktop.shortcut_probe_move_mouse(process,pointer,*centre,monitor=monitor.mouse)
        desktop.browser_probe_wait_pointer(monitor,screenshot,centre,label+'-centre')
        desktop.send_desktop_exit_click(process)
    wait(r'BROWSER_INPUT_READY\r?\n'); monitor.key('j')
    ready=wait(r'BROWSER_SCRIPT_DOM_OK executions=2 width=(\d+) height=(\d+)\r?\n')
    width,height=map(int,ready.groups())
    capture('dom',deadline)
    # Focus pixels identify the actual client origin; real PS/2 drag then
    # crosses compositor configure -> isolated parser -> accepted replay.
    monitor.key('ret')
    initial,_,origin=desktop.browser_model_initial(capture,width,height,deadline)
    def title_pixels(ppm):
        return desktop.browser_model_crop(ppm,origin[0]+388,origin[1]+51,200,14)
    initial_title=title_pixels(initial)
    if initial_title.count(b'\x20\x20\x20')<32: raise RuntimeError('script title not visible in browser chrome')
    monitor.key('esc')
    corner=(origin[0]+width-1,origin[1]+height-1)
    ppm,_=capture('before-resize',deadline)
    for _ in range((max(ppm[0],ppm[1])+119)//120):
        monitor.mouse(process,'mouse_move -120 -120'); time.sleep(.01)
    desktop.browser_probe_wait_pointer(monitor,screenshot,(0,0),'home')
    pointer=[0,0]
    desktop.shortcut_probe_move_mouse(process,pointer,*corner,monitor=monitor.mouse)
    desktop.browser_probe_wait_pointer(monitor,screenshot,corner,'grip')
    monitor.mouse(process,'mouse_button 1'); time.sleep(.12)
    desktop.shortcut_probe_move_mouse(process,pointer,corner[0]-64,corner[1]-32,monitor=monitor.mouse)
    time.sleep(.12); monitor.mouse(process,'mouse_button 0')
    wait(r'BROWSER_SCRIPT_REFLOW_OK executions=2'); capture('reflow',deadline)
    fresh_title=None
    for key,marker in [('n','NAVIGATION_OK executions=3'),('h','HANG_CONTAINED_OK'),
                       ('f','FAULT_CONTAINED_OK'),('r','RECOVERY_OK executions=5')]:
        monitor.key(key); wait('BROWSER_SCRIPT_'+marker)
        for attempt in range(32):
            ppm,_=capture(key+'-'+str(attempt),deadline); current_title=title_pixels(ppm)
            if key=='n': accepted=current_title!=initial_title and current_title.count(b'\x20\x20\x20')>=32
            else: accepted=current_title==(initial_title if key=='r' else fresh_title)
            if accepted: break
            time.sleep(.01)
        else: raise RuntimeError('script title scanout mismatch '+key)
        if key=='n': fresh_title=current_title
    transcript.append('HOST_BROWSER_SCRIPT_TITLE_PIXELS_OK\n')
    monitor.key('esc'); wait('BROWSER_CLOSE_OK'); wait('TERMINAL_INPUT_IDLE')
    exit_desktop('exit-first'); wait('DESKTOP_EXIT_OK')
    offset=len(''.join(transcript)); desktop.send_command(process,'help')
    wait(desktop.SHELL_HELP_MARKER,offset)
    offset=len(''.join(transcript)); desktop.send_command(process,'desktop.prg --browser-input-probe')
    wait('BROWSER_INPUT_READY',offset); monitor.key('esc'); wait('BROWSER_CLOSE_OK',offset)
    exit_desktop('exit-second'); wait('DESKTOP_EXIT_OK',offset)
    transcript.append('HOST_BROWSER_SCRIPT_RESTART_OK\n'); validate_transcript(''.join(transcript))
    print('BROWSER_SCRIPT_RUNTIME PASS DOM-reflow-navigation-hang-fault-recovery-restart')
    return 0

def artifacts(args):
    from verify_text_artifacts import UNCHANGED,read_fat_file,image_program_path
    from run_qemu_math import ROOT,KERNELS,digest,kernel_digest
    expected={k:v for k,v in UNCHANGED.items() if k not in ('BROWSER.PRG','HTMLWORK.PRG')}
    expected.update({'JSTEST.PRG':'723765e1e695274750366d36858b1c5812239033c6e9847616fca2357904d440',
        'JSWORK.PRG':'4bc4bd9d4f2913e00adbaa04e39b61f5f0888f93f0c5272889a8b6064b3516d0'})
    report={'baseline':'4b2b3302','passed':False,'programs':{},'images':{}}; start=time.monotonic()
    try:
        for name,wanted in expected.items():
            if digest(ROOT/'build/programs'/name)!=wanted: raise ValueError('changed protected program '+name)
            report['programs'][name]=wanted
        for name in ('BROWSER.PRG','HTMLWORK.PRG'): report['programs'][name]=digest(ROOT/'build/programs'/name)
        if digest(ROOT/'build/kernel.bin')!=KERNELS['qemu']: raise ValueError('common kernel changed')
        for profile,path in [('qemu',args.image),('vmware',ROOT/'build/vmware/reist-os/reist-os-flat.vmdk')]:
            if kernel_digest(path)!=KERNELS[profile]: raise ValueError('kernel changed '+profile)
            for name,wanted in report['programs'].items():
                if hashlib.sha256(read_fat_file(path,image_program_path(name))).hexdigest()!=wanted:
                    raise ValueError('packaged program differs '+profile+' '+name)
            for name in ('javascript.htm','jsnext.htm'):
                # The artifact reader intentionally uses native FAT short names;
                # guest VFS resolves the packaged long name independently.
                alias='javasc~1.htm' if name=='javascript.htm' else name
                if read_fat_file(path,'htdocs/'+alias)!=(ROOT/'htdocs'/name).read_bytes(): raise ValueError('missing fixture '+name)
            report['images'][profile]={'path':str(path),'sha256':digest(path),'kernel_sha256':KERNELS[profile]}
        report['passed']=True
    except (OSError,ValueError) as e: report['error']=str(e)
    report['elapsed_seconds']=round(time.monotonic()-start,3)
    args.log.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('BROWSER_SCRIPT_ARTIFACTS '+('PASS' if report['passed'] else 'FAIL '+report['error']))
    return 0 if report['passed'] else 1

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qemu',type=Path); parser.add_argument('--verify-artifacts',action='store_true')
    parser.add_argument('--image',type=Path,required=True); parser.add_argument('--log',type=Path,required=True)
    args=parser.parse_args()
    if args.log.exists(): parser.error('refusing to overwrite evidence')
    args.log.parent.mkdir(parents=True,exist_ok=True); suppress_windows_test_dialogs()
    if args.verify_artifacts: return artifacts(args)
    # Reuse the accepted headless launch/reap/input harness, replacing only
    # the explicitly selected feature assertion callback, not its lifecycle.
    original=desktop.run_browser_input_probe; desktop.run_browser_input_probe=probe
    options={name:False for name,p in inspect.signature(desktop.run).parameters.items() if p.default is inspect.Parameter.empty}
    screenshot=args.log.with_suffix('.ppm')
    options.update(qemu=args.qemu,image=args.image,screenshot=screenshot,timeout=180.0,metrics_log=None,smp=1,browser_input_probe=True)
    start=time.monotonic()
    try: return desktop.run(**options)
    except (OSError,RuntimeError,ValueError) as e: print('BROWSER_SCRIPT_RUNTIME FAIL '+str(e)); return 1
    finally:
        desktop.run_browser_input_probe=original
        evidence=screenshot.with_suffix('.browser.log')
        if evidence.exists(): args.log.write_bytes(evidence.read_bytes())
        print(f'BROWSER_SCRIPT_RUNTIME elapsed={time.monotonic()-start:.3f}s log={args.log}')
if __name__=='__main__': raise SystemExit(main())
