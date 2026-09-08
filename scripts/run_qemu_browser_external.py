#!/usr/bin/env python3
"""R3.27: actual external classic scripts, owned HTTP cancellation and replay."""
import argparse,hashlib,inspect,json,re,socket,threading,time
from collections import Counter
from http.server import BaseHTTPRequestHandler,HTTPServer
from pathlib import Path
import run_qemu_runtime_desktop as desktop
from measure_cpp_baseline import suppress_windows_test_dialogs

CURL_SHA='aa3e619cade08e7172ebee80f192c0db728a0a5b987b23319e11448c6fc1b7cc'
ROOT=Path(__file__).resolve().parents[1]
# R3.27a accepted361beac4, explicitly frozen before R3.27 resumes.
# Other consumers keep their own older kernel baselines unchanged.
KERNELS={
    'qemu':'b8add76174cb003e06383079285af61c4b707e892cef9b65f1c5aaf13332b49d',
    'vmware':'2f561825a91362f357f019d1e1a770e53b9fabfb8e3306ed923d8a679393b810'}

class Resources:
    def __init__(self):
        self.requests=[]; self.hold=threading.Event(); self.entered=threading.Event()
        self.release=threading.Event(); self.stop=threading.Event(); self.errors=[]
        self.events=[]
        self.source=(b'/*'+b' bounded external source '*13000+b'*/\n'+
            b"if(typeof remoteCount==='undefined')globalThis.remoteCount=0;"
            b"if(order!==(remoteCount===0?'inline|local':'inline|local|http')||"
            b"document.getElementById('future')!==null)throw new Error('HTTP order');"
            b"remoteCount++;order+='|http';")
        owner=self
        class Handler(BaseHTTPRequestHandler):
            protocol_version='HTTP/1.1'
            def setup(self):
                super().setup();self.connection.settimeout(1)
            def log_message(self,*args): pass
            def do_GET(self):
                self.connection.settimeout(1)
                owner.requests.append(self.path)
                owner.events.append((self.path,'request',time.monotonic()))
                if len(owner.requests)>32:
                    owner.errors.append('HTTP request quota');self.close_connection=True;return
                body=b'';status=200;mime='text/javascript';location=None
                if self.path=='/redirect.js': status=302;location='/big.js'
                elif self.path=='/big.js': body=owner.source
                elif self.path=='/missing.js': status=404;body=b'globalThis.badMime=true'
                elif self.path=='/html.js': mime='text/html';body=b'globalThis.badMime=true'
                else: owner.errors.append('inert/unexpected script fetched '+self.path);status=404
                try:
                    self.send_response(status);self.send_header('Content-Type',mime)
                    self.send_header('Content-Length',str(len(body)));self.send_header('Connection','close')
                    self.send_header('Cache-Control','max-age=60')
                    if location:self.send_header('Location',location)
                    self.end_headers()
                    if self.path=='/big.js' and owner.hold.is_set():
                        owner.entered.set()
                        if not owner.release.wait(5): owner.errors.append('cancellation not completed within fetch budget')
                        return
                    self.wfile.write(body)
                    owner.events.append((self.path,'sent',len(body),time.monotonic()))
                except (BrokenPipeError,ConnectionResetError,socket.timeout):
                    if not owner.hold.is_set():owner.errors.append('unexpected HTTP disconnect')
                finally:self.close_connection=True
        self.server=HTTPServer(('127.0.0.1',18765),Handler)
        self.server.timeout=.1
        self.deadline=time.monotonic()+180
        def serve():
            while not self.stop.is_set() and time.monotonic()<self.deadline:
                self.server.handle_request()
        self.thread=threading.Thread(target=serve,name='r327-http-fixture',daemon=True)
    def __enter__(self):self.thread.start();return self
    def __exit__(self,*args):
        self.stop.set();self.release.set();self.thread.join(2);self.server.server_close()
        if self.thread.is_alive():raise RuntimeError('fixture cleanup deadline')
    def check(self,complete,cancelled=0):
        expected=Counter({'/redirect.js':complete+cancelled,'/big.js':complete+cancelled,
                          '/missing.js':complete,'/html.js':complete})
        if self.errors or Counter(self.requests)!=expected:
            raise RuntimeError('HTTP cache/admission proof: '+str((self.requests,self.errors)))

def validate_transcript(text):
    markers=['BROWSER_EXTERNAL_INITIAL_OK executions=5','BROWSER_EXTERNAL_REFLOW_OK executions=5',
        'BROWSER_EXTERNAL_RELOAD_OK executions=10','BROWSER_EXTERNAL_CANCEL_SENT',
        'BROWSER_EXTERNAL_CANCEL_OK executions=12','BROWSER_EXTERNAL_RECOVERY_OK executions=17',
        'HOST_EXTERNAL_SOURCE_CACHE_REPLAY_OK','HOST_EXTERNAL_PIXELS_OK','BROWSER_CLOSE_OK',
        'DESKTOP_EXIT_OK','HOST_EXTERNAL_SHELL_OK']
    at=0
    for marker in markers:
        found=text.find(marker,at)
        if found<0:raise ValueError('missing ordered proof '+marker)
        at=found+len(marker)
    workers=re.findall(r'BROWSER_SCRIPT_FETCH_WORKER pid=(\d+) generation=(\d+)',text)
    reaps=re.findall(r'BROWSER_SCRIPT_FETCH_REAP pid=(\d+) generation=(\d+) status=(\d+)',text)
    if len(workers)!=14 or len(set(workers))!=14 or Counter(workers)!=Counter(r[:2] for r in reaps):
        raise ValueError('missing/stale external worker reap')
    cancel=re.search(r'BROWSER_EXTERNAL_CANCEL_SENT pid=(\d+)',text)
    if not cancel or sum(r[2]=='143' and r[0]==cancel[1] for r in reaps)!=1 or sum(r[2]!='0' for r in reaps)!=1:
        raise ValueError('missing actual CURL cancellation')
    if any(s in text for s in ('BROWSER_PROBE_FAIL','DESKTOP_BROWSER_FAIL','KERNEL PANIC','kernel panic','*** USER PROCESS PAGE FAULT ***')):
        raise ValueError('guest failure')

def probe(resources,process,output,transcript,screenshot,deadline,monitor):
    def wait(pattern,offset=0):
        while time.monotonic()<deadline:
            desktop.drain(output,transcript);text=''.join(transcript)
            if any(s in text for s in ('BROWSER_PROBE_FAIL','DESKTOP_BROWSER_FAIL','KERNEL PANIC','*** USER PROCESS PAGE FAULT ***')):
                raise RuntimeError('external guest failure: '+text[-3000:])
            match=re.search(pattern,text[offset:])
            if match:return match
            time.sleep(.01)
        raise RuntimeError('external deadline: '+pattern)
    def capture(label,limit):
        path=screenshot.with_name(screenshot.stem+'-'+label+'.ppm')
        if path.exists():raise RuntimeError('existing screenshot evidence')
        monitor.execute('screendump',{'filename':str(path.resolve())},limit)
        ppm=desktop.read_ppm(path)
        if ppm is None:raise RuntimeError('invalid framebuffer')
        return ppm,path
    def pixels(label):
        ppm,_=capture(label,deadline)
        if ppm[2].count(bytes.fromhex('127a31'))<64 or ppm[2].count(bytes.fromhex('b21b25')):
            raise RuntimeError('external script visible result mismatch')
        return ppm
    wait(r'BROWSER_INPUT_READY\r?\n');monitor.key('e')
    ready=wait(r'BROWSER_EXTERNAL_INITIAL_OK executions=5 width=(\d+) height=(\d+)\r?\n')
    width,height=map(int,ready.groups());resources.check(1);pixels('initial')
    monitor.key('ret');_,_,origin=desktop.browser_model_initial(capture,width,height,deadline);monitor.key('esc')
    corner=(origin[0]+width-1,origin[1]+height-1)
    ppm,_=capture('before-resize',deadline)
    for _ in range((max(ppm[0],ppm[1])+119)//120):
        monitor.mouse(process,'mouse_move -120 -120');time.sleep(.01)
    desktop.browser_probe_wait_pointer(monitor,screenshot,(0,0),'home')
    pointer=[0,0];desktop.shortcut_probe_move_mouse(process,pointer,*corner,monitor=monitor.mouse)
    desktop.browser_probe_wait_pointer(monitor,screenshot,corner,'grip')
    monitor.mouse(process,'mouse_button 1');time.sleep(.12)
    desktop.shortcut_probe_move_mouse(process,pointer,corner[0]-64,corner[1]-32,monitor=monitor.mouse)
    time.sleep(.12);monitor.mouse(process,'mouse_button 0')
    wait('BROWSER_EXTERNAL_REFLOW_OK executions=5');resources.check(1);pixels('reflow')
    monitor.key('r');wait('BROWSER_EXTERNAL_RELOAD_OK executions=10');resources.check(2);pixels('reload')
    resources.hold.set();monitor.key('r')
    limit=min(deadline,time.monotonic()+10)
    while time.monotonic()<limit and not resources.entered.is_set():
        desktop.drain(output,transcript);time.sleep(.01)
    if not resources.entered.is_set():raise RuntimeError('no actual in-flight HTTP script')
    monitor.key('esc');wait('BROWSER_EXTERNAL_CANCEL_OK executions=12');resources.release.set()
    resources.check(2,1);pixels('cancel-preserves-page')
    resources.hold.clear();monitor.key('r')
    wait('BROWSER_EXTERNAL_RECOVERY_OK executions=17');resources.check(3,1);pixels('recovery')
    transcript.append('HOST_EXTERNAL_SOURCE_CACHE_REPLAY_OK\nHOST_EXTERNAL_PIXELS_OK\n')
    monitor.key('esc');wait('BROWSER_CLOSE_OK');wait('TERMINAL_INPUT_IDLE')
    ppm,_=capture('exit',deadline)
    for _ in range((max(ppm[0],ppm[1])+119)//120):
        monitor.mouse(process,'mouse_move -120 -120');time.sleep(.01)
    desktop.browser_probe_wait_pointer(monitor,screenshot,(0,0),'exit-home')
    pointer=[0,0];centre=(ppm[0]//2,ppm[1]//2)
    desktop.shortcut_probe_move_mouse(process,pointer,*centre,monitor=monitor.mouse)
    desktop.browser_probe_wait_pointer(monitor,screenshot,centre,'exit-centre')
    desktop.send_desktop_exit_click(process);wait('DESKTOP_EXIT_OK')
    offset=len(''.join(transcript));desktop.send_command(process,'help');wait(desktop.SHELL_HELP_MARKER,offset)
    transcript.append('HOST_EXTERNAL_SHELL_OK\n');validate_transcript(''.join(transcript))
    print('BROWSER_EXTERNAL_RUNTIME PASS local-large-HTTP-redirect-cache-reflow-cancel-recovery')
    return 0

def artifacts(args):
    from verify_text_artifacts import UNCHANGED,read_fat_file,image_program_path
    from run_qemu_math import digest,kernel_digest
    expected={k:v for k,v in UNCHANGED.items() if k not in ('BROWSER.PRG','HTMLWORK.PRG')}
    expected.update({'CURL.PRG':CURL_SHA,
        'JSTEST.PRG':'723765e1e695274750366d36858b1c5812239033c6e9847616fca2357904d440',
        'JSWORK.PRG':'4bc4bd9d4f2913e00adbaa04e39b61f5f0888f93f0c5272889a8b6064b3516d0'})
    report={'baseline':'3eab01ab','kernel_baseline':'361beac4','passed':False,'programs':{},'images':{}};start=time.monotonic()
    try:
        reference=ROOT/'build/codex-agent/r327a-network/accepted-reference'
        for profile,name in [('qemu','reist-os.img'),('vmware','reist-os-flat.vmdk')]:
            if kernel_digest(reference/profile/name)!=KERNELS[profile]:
                raise ValueError('accepted kernel reference differs '+profile)
        for name,wanted in expected.items():
            if digest(ROOT/'build/programs'/name)!=wanted:raise ValueError('protected program changed '+name)
            report['programs'][name]=wanted
        for name in ('BROWSER.PRG','HTMLWORK.PRG'):report['programs'][name]=digest(ROOT/'build/programs'/name)
        if digest(ROOT/'build/kernel.bin')!=KERNELS['qemu']:raise ValueError('common kernel changed')
        for profile,path in [('qemu',args.image),('vmware',ROOT/'build/vmware/reist-os/reist-os-flat.vmdk')]:
            if kernel_digest(path)!=KERNELS[profile]:raise ValueError('kernel changed '+profile)
            for name,wanted in report['programs'].items():
                if hashlib.sha256(read_fat_file(path,image_program_path(name))).hexdigest()!=wanted:
                    raise ValueError('packaged program differs '+profile+' '+name)
            for name in ('jsext.htm','ext.js'):
                if read_fat_file(path,'htdocs/'+name)!=(ROOT/'htdocs'/name).read_bytes():raise ValueError('fixture missing '+name)
            report['images'][profile]={'path':str(path),'sha256':digest(path),'kernel_sha256':KERNELS[profile]}
        report['passed']=True
    except (OSError,ValueError) as e:report['error']=str(e)
    report['elapsed_seconds']=round(time.monotonic()-start,3)
    args.log.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('BROWSER_EXTERNAL_ARTIFACTS '+('PASS' if report['passed'] else 'FAIL '+report['error']))
    return 0 if report['passed'] else 1

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qemu',type=Path);parser.add_argument('--verify-artifacts',action='store_true')
    parser.add_argument('--trace-network',action='store_true',help='bounded diagnostic packet prefixes')
    parser.add_argument('--image',type=Path,required=True);parser.add_argument('--log',type=Path,required=True)
    args=parser.parse_args()
    if args.log.exists():parser.error('refusing to overwrite evidence')
    args.log.parent.mkdir(parents=True,exist_ok=True);suppress_windows_test_dialogs()
    if args.verify_artifacts:return artifacts(args)
    original=desktop.run_browser_input_probe;original_command=desktop.qemu_command
    if args.trace_network:
        trace=args.log.with_suffix('.pcap').resolve()
        if trace.exists():parser.error('refusing to overwrite packet evidence')
        def traced_command(*a,**kw):
            return [*original_command(*a,**kw),'-object',
                f'filter-dump,id=r327trace,netdev=reistnet0,file={trace.as_posix()},maxlen=96']
        desktop.qemu_command=traced_command
    options={name:False for name,p in inspect.signature(desktop.run).parameters.items() if p.default is inspect.Parameter.empty}
    screenshot=args.log.with_suffix('.ppm')
    options.update(qemu=args.qemu,image=args.image,screenshot=screenshot,timeout=180.0,metrics_log=None,
        smp=1,browser_input_probe=True,browser_external_scripts=True)
    start=time.monotonic();resources=None
    try:
        with Resources() as resources:
            desktop.run_browser_input_probe=lambda *a:probe(resources,*a)
            return desktop.run(**options)
    except (OSError,RuntimeError,ValueError) as e:print('BROWSER_EXTERNAL_RUNTIME FAIL '+str(e));return 1
    finally:
        desktop.run_browser_input_probe=original
        desktop.qemu_command=original_command
        if resources is not None:
            args.log.with_suffix('.http.json').write_text(json.dumps({'requests':resources.requests,
                'events':resources.events,'errors':resources.errors},indent=2)+'\n',encoding='utf-8')
        evidence=screenshot.with_suffix('.browser.log')
        if evidence.exists():args.log.write_bytes(evidence.read_bytes())
        print(f'BROWSER_EXTERNAL_RUNTIME elapsed={time.monotonic()-start:.3f}s log={args.log}')
if __name__=='__main__':raise SystemExit(main())
