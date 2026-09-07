#!/usr/bin/env python3
"""Bounded isolated JS guest and protected binary proof; no visible VM."""
import argparse
from pathlib import Path
import queue
import re
import subprocess
import threading
import time
import run_qemu_smoke as smoke
from run_qemu_system_layout import inject
from measure_cpp_baseline import suppress_windows_test_dialogs

MAX_TRANSCRIPT=4*1024*1024

def validate_transcript(text):
    for marker in ("JS_VECTORS_OK","JS_PARENT_OK","JS_RUNTIME_OK"):
        if len(re.findall(r"(?m)^"+re.escape(marker)+r"\r?$",text))!=2:
            raise ValueError("missing/duplicate marker "+marker)
    if text.count("JS_NONCOOPERATIVE_ENTERED")!=2:
        raise ValueError("missing actual noncooperative worker entry")
    records=re.findall(r"(?m)^JS_REAP_OK mode=(--\w+) status=(\d+) pid=(\d+) generation=(\d+)\r?$",text)
    if [(mode,status) for mode,status,_,_ in records]!=[
        ("--normal","37"),("--fault","142"),("--hang","143"),("--normal","37")]*2:
        raise ValueError("incomplete ordered normal/fault/kill/reap proof")
    identities=[(pid,generation) for _,_,pid,generation in records]
    if len(set(identities))!=8 or any(int(pid)<=0 or int(gen)<=0 for pid,gen in identities):
        raise ValueError("stale/missing child identity")
    if text.count("*** USER PROCESS PAGE FAULT ***")!=2:
        raise ValueError("missing or unexpected actual pagefault")
    if len(re.findall(r"(?m)^Faulting address: 0x00000004\r?$",text))!=2:
        raise ValueError("wrong JavaScript fixture fault address")
    if smoke.failure_marker(text) or "JS_TEST_FAIL" in text:
        raise ValueError("kernel/test failure")

def run(args):
    subprocess.run(["C:/Program Files/PowerShell/7/pwsh.exe","-NoProfile","-Command",
        "$busy=@(Get-Process qemu-system-i386,qemu-system-x86_64,vmware-vmx,zig,clang,gcc,cc1,cc1plus,nasm,ld,lld -ErrorAction SilentlyContinue); if ($busy.Count) { throw 'Concurrent VM/compiler' }; exit 0"],
        check=True,capture_output=True,text=True,timeout=15,
        creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
    start=time.monotonic(); deadline=start+180
    process=subprocess.Popen(smoke.qemu_command(args.qemu,args.image,memory="1024M",nic="e1000",smp=1),
        stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
        text=True,encoding="utf-8",errors="replace",bufsize=0,
        creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
    chunks=queue.Queue(65536); stopped=threading.Event(); overflow=threading.Event(); transcript=""

    def reader():
        while not stopped.is_set():
            char=process.stdout.read(1)
            if not char: return
            try: chunks.put(char,timeout=1)
            except queue.Full: overflow.set(); return

    thread=threading.Thread(target=reader,daemon=True); thread.start()

    def drain():
        nonlocal transcript
        batch=[]
        for _ in range(65536):
            try: batch.append(chunks.get_nowait())
            except queue.Empty: break
        if len(transcript)+len(batch)>MAX_TRANSCRIPT: raise ValueError("transcript capacity")
        transcript+="".join(batch)

    def wait(marker,after):
        while time.monotonic()<deadline:
            drain()
            if (overflow.is_set() or smoke.failure_marker(transcript) or
                "JS_TEST_FAIL" in transcript or transcript.count("*** USER PROCESS PAGE FAULT ***")>2):
                raise ValueError("guest failure/output overflow")
            position=transcript.find(marker,after)
            if position>=0: return position+len(marker)
            if process.poll() is not None: raise ValueError("QEMU exited before "+marker)
            stopped.wait(0.01)
        raise TimeoutError("deadline before "+marker)

    error=None
    try:
        smoke.configure_qemu_host_timers(process)
        position=wait(smoke.SHELL_PROMPT,0)
        for _ in range(2):
            inject(process,"jstest")
            position=wait("JS_RUNTIME_OK\n",position)
            position=wait(smoke.SHELL_PROMPT,position)
            inject(process,"help")
            position=wait("Built-ins: cd path pwd history help exit",position)
            position=wait(smoke.SHELL_PROMPT,position)
        validate_transcript(transcript)
    except (OSError,ValueError,RuntimeError,TimeoutError) as caught:
        error=str(caught)
    finally:
        stopped.set(); smoke.stop_process(process); thread.join(timeout=2)
        try: drain()
        except ValueError as caught: error=str(caught)
        for stream in (process.stdin,process.stdout):
            if stream: stream.close()
        args.log.parent.mkdir(parents=True,exist_ok=True)
        args.log.write_text(transcript,encoding="utf-8")
    print(f"JS_RUNTIME {'FAIL: '+error if error else 'PASS'} elapsed={time.monotonic()-start:.3f}s log={args.log}")
    return 1 if error else 0


def verify_artifacts(args):
    import hashlib,json
    from verify_text_artifacts import UNCHANGED,read_fat_file,image_program_path
    from run_qemu_math import ROOT,KERNELS,digest,kernel_digest
    expected={**UNCHANGED,"HTMLWORK.PRG":"c40c114e593a1251ce803ac46e0a8639b87ff061dacfe52958a5faa1f3996da8"}
    report={"baseline":"6bc51cbf","passed":False,"programs":{},"images":{}}
    start=time.monotonic()
    try:
        for name,wanted in expected.items():
            actual=digest(ROOT/"build/programs"/name)
            if actual!=wanted: raise ValueError("changed program "+name)
            report["programs"][name]=actual
        if digest(ROOT/"build/kernel.bin")!=KERNELS["qemu"]: raise ValueError("common kernel changed")
        test=digest(ROOT/"build/programs/JSTEST.PRG")
        for profile,path in (("qemu",args.image),("vmware",ROOT/"build/vmware/reist-os/reist-os-flat.vmdk")):
            kernel=kernel_digest(path)
            if kernel!=KERNELS[profile]: raise ValueError("changed kernel "+profile)
            for name,wanted in {**expected,"JSTEST.PRG":test}.items():
                actual=hashlib.sha256(read_fat_file(path,image_program_path(name))).hexdigest()
                if actual!=wanted: raise ValueError("packaged bytes differ "+profile+" "+name)
            report["images"][profile]={"path":str(path),"sha256":digest(path),"kernel_sha256":kernel}
        report["jstest_sha256"]=test; report["passed"]=True
    except (OSError,ValueError) as error: report["error"]=str(error)
    report["elapsed_seconds"]=round(time.monotonic()-start,3)
    args.log.parent.mkdir(parents=True,exist_ok=True)
    args.log.write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print("JS_ARTIFACTS "+("PASS" if report["passed"] else "FAIL: "+report["error"])+
          f" elapsed={report['elapsed_seconds']}s log={args.log}")
    return 0 if report["passed"] else 1


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu",type=Path)
    parser.add_argument("--verify-artifacts",action="store_true")
    parser.add_argument("--image",type=Path,required=True)
    parser.add_argument("--log",type=Path,required=True)
    args=parser.parse_args()
    if args.log.exists(): parser.error("refusing to overwrite evidence")
    if args.verify_artifacts: return verify_artifacts(args)
    if not args.qemu or not args.qemu.is_file() or not args.image.is_file():
        parser.error("existing QEMU/image required")
    suppress_windows_test_dialogs()
    return run(args)

if __name__=="__main__": raise SystemExit(main())
