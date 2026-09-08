#!/usr/bin/env python3
"""Actual TrueType guest scanout, retained atlas, fault/hang containment."""
import argparse
import hashlib
import inspect
import json
import re
import tarfile
import time
from pathlib import Path
import run_qemu_runtime_desktop as desktop
from measure_cpp_baseline import suppress_windows_test_dialogs

ROOT=Path(__file__).resolve().parents[1]


def artifacts(args):
    from run_qemu_browser_layout import artifacts as protected
    from verify_text_artifacts import read_fat_file
    if protected(args):return 1
    report=json.loads(args.log.read_text(encoding='utf-8'))
    report.update(baseline='f2dbc2d5',fonts={})
    try:
        with tarfile.open(ROOT/'assets/fonts/source/liberation-2.1.5.tar.gz') as archive:
            fonts={e.name.rsplit('/',1)[-1]:archive.extractfile(e).read() for e in archive if e.isfile() and ('LiberationSerif-' in e.name or 'LiberationSans-' in e.name)}
            license_fonts=archive.extractfile('liberation-fonts-ttf-2.1.5/LICENSE').read()
        with tarfile.open(ROOT/'third_party/freetype-2.14.3.tar.gz') as archive:
            license_ft=archive.extractfile('freetype-VER-2-14-3/docs/FTL.TXT').read()
        if len(fonts)!=8:raise ValueError('eight original faces required')
        for profile,path in (('qemu',args.image),('vmware',ROOT/'build/vmware/reist-os/reist-os-flat.vmdk')):
            expected={'htdocs/fonts.htm':(ROOT/'htdocs/fonts.htm').read_bytes(),
                      'htdocs/fonts.css':(ROOT/'htdocs/fonts.css').read_bytes(),
                      'usr/share/fonts/freetype.txt':license_ft,
                      'usr/share/fonts/liberation.txt':license_fonts}
            for name,data in expected.items():
                # Existing independent FAT verifier uses 8.3 aliases and skips
                # LFN entries, exactly as for BENCHM~1.PRG in the protected gate.
                short='usr/share/fonts/libera~1.txt' if name=='usr/share/fonts/liberation.txt' else name
                if read_fat_file(path,short)!=data:raise ValueError('fixture/license mismatch '+profile+'/'+name)
            worker=read_fat_file(path,'usr/bin/htmlwork.prg');ui=read_fat_file(path,'usr/gui/bin/browser.prg')
            for name,data in fonts.items():
                if data not in worker or data in ui:raise ValueError('outline ownership '+profile+'/'+name)
                report['fonts'][profile+'/'+name]=hashlib.sha256(data).hexdigest()
    except (OSError,ValueError,KeyError) as e:report['passed']=False;report['error']=str(e)
    args.log.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('BROWSER_FONT_ARTIFACTS '+('PASS' if report['passed'] else 'FAIL '+report['error']))
    return 0 if report['passed'] else 1


def probe(process,output,transcript,screenshot,deadline,monitor):
    captures={}
    def wait(pattern,offset=0):
        while time.monotonic()<deadline:
            desktop.drain(output,transcript);text=''.join(transcript);desktop.browser_model_guest_health(text)
            found=re.search(pattern,text[offset:])
            if found:return found
            time.sleep(.01)
        raise RuntimeError('font deadline: '+pattern)
    def capture(label,limit):
        path=screenshot.with_name(screenshot.stem+'-'+label+'.ppm')
        if path.exists():raise RuntimeError('existing scanout evidence')
        monitor.execute('screendump',{'filename':str(path.resolve())},limit)
        ppm=desktop.read_ppm(path)
        if ppm is None:raise RuntimeError('invalid scanout')
        captures[label]=hashlib.sha256(ppm[2]).hexdigest()
        return ppm,path
    def geometry(marker,offset=0):
        match=wait('BROWSER_FONT_'+marker+r' width=(\d+) height=(\d+) scene=(\d+) total=(\d+)\r?\n',offset)
        width,height,scene,total=map(int,match.groups())
        atlas=wait(r'BROWSER_FONT_ATLAS count=(\d+) bytes=(\d+)\r?\n',offset)
        count,used=map(int,atlas.groups())
        samples=[tuple(map(int,v)) for v in re.findall(r'BROWSER_FONT_SAMPLE kind=(\d+) size=(\d+) x=(\d+) y=(\d+) w=(\d+)\r?\n',''.join(transcript)[offset:])[:4]]
        if len(samples)!=4 or total<=height or not (0<count<=1024 and 0<used<=512*1024):raise RuntimeError('font scene missing/oversized')
        expected=[(0,24),(1,24),(0,40),(1,40)]
        if [s[:2] for s in samples]!=expected:raise RuntimeError('font samples')
        if not(samples[1][4]>samples[0][4]*2 and samples[3][4]>samples[2][4]*2 and samples[2][4]>samples[0][4] and samples[3][4]>samples[1][4]):
            raise RuntimeError('proportional or size metrics')
        return width,height,scene,samples
    def pixels(label,samples,origin):
        for attempt in range(32):
            ppm,_=capture(label+'-'+str(attempt),deadline);shapes=[]
            for kind,size,x,y,w in samples:
                region=desktop.browser_model_crop(ppm,origin[0]+x-2,origin[1]+76+y-2,w+4,size+4)
                colors=[tuple(region[i:i+3]) for i in range(0,len(region),3)]
                # Only black text on white exists in these four sample boxes.
                # Real grayscale coverage must be present in each size/shape.
                gray=sum(0<r<255 and r==g==b for r,g,b in colors)
                ink=[i for i,(r,g,b) in enumerate(colors) if r==g==b and r<200]
                if gray<12 or len(ink)<24 or any(r!=g or g!=b for r,g,b in colors):break
                columns=[i%(w+4) for i in ink]
                if max(columns)-min(columns)<w//2:break
                shapes.append(hashlib.sha256(region).hexdigest())
            if len(shapes)==4:return ppm,shapes
            time.sleep(.01)
        raise RuntimeError('missing antialiased glyph scanout '+label)
    wait(r'BROWSER_INPUT_READY\r?\n');monitor.key('t')
    width,height,scene,samples=geometry('INITIAL_OK')
    monitor.key('ret');_,_,origin=desktop.browser_model_initial(capture,width,height,deadline);monitor.key('esc')
    initial,original_shapes=pixels('wide',samples,origin)
    for _ in range((max(initial[0],initial[1])+119)//120):
        monitor.mouse(process,'mouse_move -120 -120');time.sleep(.01)
    desktop.browser_probe_wait_pointer(monitor,screenshot,(0,0),'font-home')
    pointer=[0,0];corner=(origin[0]+width-1,origin[1]+height-1)
    desktop.shortcut_probe_move_mouse(process,pointer,*corner,monitor=monitor.mouse)
    desktop.browser_probe_wait_pointer(monitor,screenshot,corner,'font-grip')
    offset=len(''.join(transcript));monitor.mouse(process,'mouse_button 1');time.sleep(.12)
    desktop.shortcut_probe_move_mouse(process,pointer,corner[0]-(width-480),corner[1],monitor=monitor.mouse)
    time.sleep(.12);monitor.mouse(process,'mouse_button 0')
    width,height,scene,samples=geometry('REFLOW_OK',offset)
    if width!=480:raise RuntimeError('actual resize width')
    desktop.shortcut_probe_move_mouse(process,pointer,origin[0]+scene-30,origin[1]+height-40,monitor=monitor.mouse)
    desktop.browser_probe_wait_pointer(monitor,screenshot,tuple(pointer),'font-body')
    stable,shapes=pixels('narrow',samples,origin)
    if shapes!=original_shapes:raise RuntimeError('glyphs changed on reflow')
    body=lambda ppm:desktop.browser_model_crop(ppm,origin[0],origin[1]+76,scene,height-98)
    previous=body(stable)
    for _ in range(4):monitor.mouse(process,'mouse_move 0 0 -1')
    wait(r'BROWSER_FONT_WHEEL_DOWN_OK scroll=(\d+)\r?\n')
    for attempt in range(32):
        down,_=capture('scroll-'+str(attempt),deadline)
        if body(down)!=previous:break
        time.sleep(.01)
    else:raise RuntimeError('wheel did not change scanout')
    for _ in range(4):monitor.mouse(process,'mouse_move 0 0 1')
    wait('BROWSER_FONT_WHEEL_UP_OK');stable,shapes=pixels('top',samples,origin)
    if shapes!=original_shapes or body(stable)!=previous:raise RuntimeError('scroll cache changed pixels')
    for key,marker in (('f','FAULT'),('h','HANG')):
        monitor.key(key);wait('BROWSER_FONT_'+marker+'_CONTAINED_OK')
        ppm,_=pixels(marker.lower(),samples,origin)
        if body(ppm)!=previous:raise RuntimeError('old page changed after '+marker)
    offset=len(''.join(transcript));monitor.key('r')
    width,height,scene,samples=geometry('RECOVERY_OK',offset)
    _,shapes=pixels('recovery',samples,origin)
    if shapes!=original_shapes:raise RuntimeError('recovery glyphs differ')
    monitor.key('esc');wait('BROWSER_CLOSE_OK');wait('TERMINAL_INPUT_IDLE')
    for _ in range((max(initial[0],initial[1])+119)//120):
        monitor.mouse(process,'mouse_move -120 -120');time.sleep(.01)
    desktop.browser_probe_wait_pointer(monitor,screenshot,(0,0),'font-exit-home')
    pointer=[0,0];centre=(initial[0]//2,initial[1]//2)
    desktop.shortcut_probe_move_mouse(process,pointer,*centre,monitor=monitor.mouse)
    desktop.browser_probe_wait_pointer(monitor,screenshot,centre,'font-exit-centre')
    desktop.send_desktop_exit_click(process);wait('DESKTOP_EXIT_OK')
    offset=len(''.join(transcript));desktop.send_command(process,'help');wait(desktop.SHELL_HELP_MARKER,offset)
    text=''.join(transcript)
    workers=re.findall(r'BROWSER_FONT_WORKER pid=(\d+) generation=(\d+) mode=(\d+)',text)
    reaps=re.findall(r'BROWSER_FONT_REAP pid=(\d+) generation=(\d+) status=(\d+)',text)
    if not workers or len({w[:2] for w in workers})!=len(workers) or sorted(w[:2] for w in workers)!=sorted(r[:2] for r in reaps):
        raise RuntimeError('unreaped/stale font worker')
    statuses={r[:2]:int(r[2]) for r in reaps}
    if sum(w[2]=='1' for w in workers)!=1 or sum(w[2]=='2' for w in workers)!=1 or any((statuses[w[:2]]==0)!=(w[2]=='0') for w in workers):
        raise RuntimeError('missing actual worker fault/hang')
    transcript.append('HOST_FONT_TWO_WIDTH_TWO_SIZE_GRAY_OK\nHOST_FONT_CACHE_OLD_PAGE_RECOVERY_SHELL_OK\n')
    screenshot.with_suffix('.scanout.json').write_text(json.dumps(captures,indent=2)+'\n',encoding='utf-8')
    print('BROWSER_FONT_RUNTIME PASS TrueType-gray-metrics-cache-fault-hang-recovery')
    return 0


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify-artifacts',action='store_true');parser.add_argument('--qemu',type=Path)
    parser.add_argument('--image',required=True,type=Path);parser.add_argument('--log',required=True,type=Path)
    args=parser.parse_args()
    if args.log.exists():parser.error('refusing to overwrite evidence')
    args.log.parent.mkdir(parents=True,exist_ok=True);suppress_windows_test_dialogs()
    if args.verify_artifacts:return artifacts(args)
    options={name:False for name,value in inspect.signature(desktop.run).parameters.items() if value.default is inspect.Parameter.empty}
    screenshot=args.log.with_suffix('.ppm')
    options.update(qemu=args.qemu,image=args.image,screenshot=screenshot,timeout=180.0,metrics_log=None,smp=1,browser_input_probe=True)
    original=desktop.run_browser_input_probe;desktop.run_browser_input_probe=probe;start=time.monotonic()
    try:return desktop.run(**options)
    except (OSError,RuntimeError,ValueError) as e:print('BROWSER_FONT_RUNTIME FAIL '+str(e));return 1
    finally:
        desktop.run_browser_input_probe=original;evidence=screenshot.with_suffix('.browser.log')
        if evidence.exists():args.log.write_bytes(evidence.read_bytes())
        print(f'BROWSER_FONT_RUNTIME elapsed={time.monotonic()-start:.3f}s log={args.log}')


if __name__=='__main__':raise SystemExit(main())
