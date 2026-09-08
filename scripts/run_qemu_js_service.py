#!/usr/bin/env python3
"""Bounded real JS service proof and protected a09d8841 image contents."""
import argparse,json,hashlib,queue,re,subprocess,threading,time
from pathlib import Path
import run_qemu_smoke as smoke
from run_qemu_system_layout import inject
from measure_cpp_baseline import suppress_windows_test_dialogs

def validate_transcript(text,restricted_worker=False):
    worker_markers=("JS_SERVICE_FAULT_ENTERED","JS_SERVICE_HANG_ENTERED","JS_SERVICE_STALE_ENTERED")
    markers=("JS_SERVICE_RUNTIME_OK","JS_SERVICE_ORPHAN_OK")
    if restricted_worker:
        markers+=("JS_SERVICE_DOMAIN_OK","JS_SERVICE_HANG_CONFIRMED")
        if any(marker in text for marker in worker_markers):
            raise ValueError("restricted worker acquired terminal authority")
    else: markers+=worker_markers
    for marker in markers:
        if len(re.findall(r"(?m)^"+marker+r"\r?$",text))!=2: raise ValueError("missing/duplicate "+marker)
    rows=re.findall(r"(?m)^JS_SERVICE_REAP mode=(\w+) status=(\d+) pid=(\d+) generation=(\d+)\r?$",text)
    modes=["normal","fault","hang","stale","cancel","fresh"]*2
    if [row[0] for row in rows]!=modes: raise ValueError("missing/incorrect ordered reap")
    statuses={"normal":{0},"fault":{142},"hang":{143},"stale":{74,143},"cancel":{74,143},"fresh":{0}}
    for mode,status,pid,generation in rows:
        if int(status) not in statuses[mode] or int(pid)<=0 or int(generation)<=0: raise ValueError("invalid child outcome")
    if len({(r[2],r[3]) for r in rows})!=12: raise ValueError("stale worker identity")
    if text.count("*** USER PROCESS PAGE FAULT ***")!=2 or len(re.findall(r"(?m)^Faulting address: 0x00000004\r?$",text))!=2:
        raise ValueError("missing or unexpected actual fault")
    if smoke.failure_marker(text) or "JS_SERVICE_TEST_FAIL" in text: raise ValueError("guest/test failure")

def run(args):
    subprocess.run(["C:/Program Files/PowerShell/7/pwsh.exe","-NoProfile","-Command",
        "$busy=@(Get-Process qemu-system-i386,qemu-system-x86_64,vmware-vmx,zig,clang,gcc,cc1,cc1plus,nasm,ld,lld -ErrorAction SilentlyContinue); if ($busy.Count) { throw 'Concurrent VM/compiler' }; exit 0"],
        check=True,capture_output=True,text=True,timeout=15,creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
    start=time.monotonic(); deadline=start+180
    process=subprocess.Popen(smoke.qemu_command(args.qemu,args.image,memory="1024M",nic="e1000",smp=1),
        stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
        text=True,encoding="utf-8",errors="replace",bufsize=0,creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
    chunks=queue.Queue(65536); stopped=threading.Event(); overflow=threading.Event(); transcript=""
    def reader():
        while not stopped.is_set():
            c=process.stdout.read(1)
            if not c: return
            try: chunks.put(c,timeout=1)
            except queue.Full: overflow.set(); return
    thread=threading.Thread(target=reader,daemon=True); thread.start()
    def drain():
        nonlocal transcript
        batch=[]
        for _ in range(65536):
            try: batch.append(chunks.get_nowait())
            except queue.Empty: break
        if len(transcript)+len(batch)>4*1024*1024: raise ValueError("transcript capacity")
        transcript+="".join(batch)
    def wait(marker,after):
        while time.monotonic()<deadline:
            drain()
            if overflow.is_set() or smoke.failure_marker(transcript) or "JS_SERVICE_TEST_FAIL" in transcript or transcript.count("*** USER PROCESS PAGE FAULT ***")>2:
                raise ValueError("guest failure/output overflow")
            at=transcript.find(marker,after)
            if at>=0: return at+len(marker)
            if process.poll() is not None: raise ValueError("QEMU exited before "+marker)
            stopped.wait(0.01)
        raise TimeoutError("deadline before "+marker)
    error=None
    try:
        smoke.configure_qemu_host_timers(process)
        at=wait(smoke.SHELL_PROMPT,0)
        for _ in range(2):
            inject(process,"jsipctst"); at=wait("JS_SERVICE_RUNTIME_OK\n",at); at=wait(smoke.SHELL_PROMPT,at)
            inject(process,"help"); at=wait("Built-ins: cd path pwd history help exit",at); at=wait(smoke.SHELL_PROMPT,at)
        validate_transcript(transcript,args.restricted_worker)
    except (OSError,ValueError,RuntimeError,TimeoutError) as caught: error=str(caught)
    finally:
        stopped.set(); smoke.stop_process(process); thread.join(timeout=2)
        try: drain()
        except ValueError as caught: error=str(caught)
        for stream in (process.stdin,process.stdout):
            if stream: stream.close()
        args.log.parent.mkdir(parents=True,exist_ok=True); args.log.write_text(transcript,encoding="utf-8")
    print(f"JS_SERVICE_RUNTIME {'FAIL: '+error if error else 'PASS'} elapsed={time.monotonic()-start:.3f}s log={args.log}")
    return 1 if error else 0

def artifacts(args):
    from verify_text_artifacts import UNCHANGED,read_fat_file,image_program_path
    from run_qemu_math import ROOT,KERNELS,digest,kernel_digest
    expected={**UNCHANGED,
        "HTMLWORK.PRG":"c40c114e593a1251ce803ac46e0a8639b87ff061dacfe52958a5faa1f3996da8",
        "JSTEST.PRG":"723765e1e695274750366d36858b1c5812239033c6e9847616fca2357904d440"}
    report={"baseline":"a09d8841","passed":False,"programs":{},"images":{}}
    start=time.monotonic()
    try:
        for name,wanted in expected.items():
            actual=digest(ROOT/"build/programs"/name)
            if actual!=wanted: raise ValueError("changed program "+name)
            report["programs"][name]=actual
        for name in ("JSWORK.PRG","JSIPCTST.PRG"): report["programs"][name]=digest(ROOT/"build/programs"/name)
        if digest(ROOT/"build/kernel.bin")!=KERNELS["qemu"]: raise ValueError("common kernel changed")
        for profile,path in (("qemu",args.image),("vmware",ROOT/"build/vmware/reist-os/reist-os-flat.vmdk")):
            kernel=kernel_digest(path)
            if kernel!=KERNELS[profile]: raise ValueError("changed kernel "+profile)
            for name,wanted in report["programs"].items():
                actual=hashlib.sha256(read_fat_file(path,image_program_path(name))).hexdigest()
                if actual!=wanted: raise ValueError("packaged bytes differ "+profile+" "+name)
            report["images"][profile]={"path":str(path),"sha256":digest(path),"kernel_sha256":kernel}
        report["passed"]=True
    except (OSError,ValueError) as error: report["error"]=str(error)
    report["elapsed_seconds"]=round(time.monotonic()-start,3)
    args.log.parent.mkdir(parents=True,exist_ok=True); args.log.write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print("JS_SERVICE_ARTIFACTS "+("PASS" if report["passed"] else "FAIL: "+report["error"])+f" elapsed={report['elapsed_seconds']}s log={args.log}")
    return 0 if report["passed"] else 1

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu",type=Path); parser.add_argument("--verify-artifacts",action="store_true")
    parser.add_argument("--restricted-worker",action="store_true")
    parser.add_argument("--image",type=Path,required=True); parser.add_argument("--log",type=Path,required=True)
    args=parser.parse_args()
    if args.log.exists(): parser.error("refusing to overwrite evidence")
    if args.verify_artifacts: return artifacts(args)
    if not args.qemu or not args.qemu.is_file() or not args.image.is_file(): parser.error("existing QEMU/image required")
    suppress_windows_test_dialogs(); return run(args)
if __name__=="__main__": raise SystemExit(main())
