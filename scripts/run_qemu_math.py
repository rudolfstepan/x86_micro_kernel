#!/usr/bin/env python3
"""Bounded headless libm proof and unchanged accepted performance artifacts."""
import argparse
import hashlib
import json
from pathlib import Path
import queue
import re
import struct
import subprocess
import threading
import time

import run_qemu_smoke as smoke
from run_qemu_system_layout import inject
from measure_cpp_baseline import suppress_windows_test_dialogs

ROOT=Path(__file__).resolve().parents[1]
KERNELS={
    "qemu":"360739585ff3c46ac6ca097fab8fa86911b7e6f7037fa4c4030e083ade950cdd",
    "vmware":"49a2a5defc545c9687add43418f47b5cd1db03e4f93ac6a9fc5ead4086681a2c"}
PROGRAMS={
    "BROWSER.PRG":"e52aaa1c502993ff729c54d31eed5cd7330ecb3208e45fedac6596473fb271e0",
    "HTMLWORK.PRG":"20c4d026c264878aa70bacb9ec5f2865d9a4814968994dde80811a76cf42643d",
    "GTEST.PRG":"48de1c2e41255309083ba67d3649e218c2a15a3ce12622237be2c4f52026d6c0",
    "BENCHMARK.PRG":"b001fb18597e4122dc1dad928649c8c281c71bea0cee7b19887074e13facbfb3"}
MAX_TRANSCRIPT=4*1024*1024


def digest(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source,"sha256").hexdigest()


def kernel_digest(image):
    # Existing native BIOS v3 manifests, both A/B payloads; never boot/mutate.
    hashes=[]
    with image.open("rb") as source:
        for relative in (0,96):
            source.seek((2048+relative)*512)
            manifest=source.read(512)
            if len(manifest)!=512 or manifest[:8]!=b"X86BOOT2" or struct.unpack_from("<I",manifest,8)[0]!=3:
                raise ValueError("invalid native v3 manifest: "+str(image))
            if sum(struct.unpack("<128I",manifest))&0xffffffff:
                raise ValueError("native manifest checksum")
            lba,size=struct.unpack_from("<II",manifest,24)
            if lba not in (128,3136) or not 4096<=size<=1536*1024 or (2048+lba)*512+size>image.stat().st_size:
                raise ValueError("native kernel bounds")
            source.seek((2048+lba)*512)
            data=source.read(size)
            actual=hashlib.sha256(data).hexdigest()
            if len(data)!=size or actual!=manifest[48:80].hex():
                raise ValueError("native kernel digest mismatch")
            hashes.append(actual)
    if hashes[0]!=hashes[1]: raise ValueError("native A/B kernels differ")
    return hashes[0]


def verify_artifacts(args):
    results={"baseline":"0301d708", "passed":False, "kernels":{}, "programs":{}}
    try:
        for profile,path in (("qemu",args.image),("vmware",ROOT/"build/vmware/reist-os/reist-os-flat.vmdk")):
            value=kernel_digest(path)
            results["kernels"][profile]={"path":str(path),"kernel_sha256":value,"image_sha256":digest(path)}
            if value!=KERNELS[profile]: raise ValueError(profile+" kernel changed")
        if digest(ROOT/"build/kernel.bin")!=KERNELS["qemu"]: raise ValueError("common kernel is not accepted QEMU profile")
        for name,expected in PROGRAMS.items():
            value=digest(ROOT/"build/programs"/name); results["programs"][name]=value
            if value!=expected: raise ValueError(name+" changed")
        results["mathtest_sha256"]=digest(ROOT/"build/programs/MATHTEST.PRG")
        results["passed"]=True
    except (OSError,ValueError) as error:
        results["error"]=str(error)
    args.log.parent.mkdir(parents=True,exist_ok=True)
    args.log.write_text(json.dumps(results,indent=2)+"\n",encoding="utf-8")
    print("MATH_ARTIFACTS "+("PASS: both kernels and four existing programs unchanged" if results["passed"] else "FAIL: "+results["error"]))
    return 0 if results["passed"] else 1


def validate_transcript(text,cpus):
    for marker in ("MATH_NUMERIC_OK functions=44","MATH_FENV_OK rounding=4","MATH_PARENT_OK","MATH_RUNTIME_OK"):
        if len(re.findall(r"(?m)^"+re.escape(marker)+r"\r?$",text))!=2:
            raise ValueError("missing/duplicate marker "+marker)
    records=re.findall(r"(?m)^MATH_REAP_OK mode=(--\w+) status=(\d+) pid=(\d+) generation=(\d+)\r?$",text)
    expected=[("--normal","37"),("--fault","144"),("--hold","143"),("--normal","37")]*2
    if [(mode,status) for mode,status,_,_ in records]!=expected:
        raise ValueError("incomplete ordered normal/fault/kill/reap proof")
    identities=[(pid,generation) for _,_,pid,generation in records]
    if len(set(identities))!=8 or any(int(pid)<=0 or int(gen)<=0 for pid,gen in identities):
        raise ValueError("stale/missing child generation")
    if len(re.findall(r"Exception: [^\r\n]+ \(IRQ 16\)",text))!=2:
        raise ValueError("missing actual x87 #MF vector16")
    ap=sorted(map(int,re.findall(r"REIST_FPU AP_CONTEXT_OK cpu=(\d+)",text)))
    if ap!=list(range(1,cpus)): raise ValueError("incomplete AP context readiness")
    if cpus>1 and f"REIST_SMP READY online={cpus} parked={cpus-1} failed=0" not in text:
        raise ValueError("incomplete SMP readiness")
    if smoke.failure_marker(text) or "MATH_TEST_FAIL" in text or "USER PROCESS PAGE FAULT" in text:
        raise ValueError("kernel/test failure")


def run(args):
    subprocess.run(["C:/Program Files/PowerShell/7/pwsh.exe","-NoProfile","-Command",
        "$busy=@(Get-Process qemu-system-i386,qemu-system-x86_64,vmware-vmx,zig,clang,gcc,cc1,cc1plus,nasm,ld,lld -ErrorAction SilentlyContinue); if ($busy.Count) { throw 'Concurrent VM/compiler' }; exit 0"],
        check=True,capture_output=True,text=True,timeout=15,
        creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
    start=time.monotonic(); deadline=start+180
    process=subprocess.Popen(smoke.qemu_command(args.qemu,args.image,memory="1024M",nic="e1000",smp=args.smp),
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
                "MATH_TEST_FAIL" in transcript or "USER PROCESS PAGE FAULT" in transcript):
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
            inject(process,"mathtest")
            position=wait("MATH_RUNTIME_OK\n",position)
            position=wait(smoke.SHELL_PROMPT,position)
            inject(process,"help")
            position=wait("Built-ins: cd path pwd history help exit",position)
            position=wait(smoke.SHELL_PROMPT,position)
        validate_transcript(transcript,args.smp)
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
    print(f"MATH_RUNTIME {'FAIL: '+error if error else 'PASS'} elapsed={time.monotonic()-start:.3f}s log={args.log}")
    return 1 if error else 0


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify-artifacts",action="store_true")
    parser.add_argument("--qemu",type=Path)
    parser.add_argument("--image",type=Path,required=True)
    parser.add_argument("--log",type=Path,required=True)
    parser.add_argument("--smp",type=int,choices=(1,4),default=1)
    args=parser.parse_args()
    if args.log.exists(): parser.error("refusing to overwrite evidence")
    if not args.image.is_file() or (not args.verify_artifacts and (not args.qemu or not args.qemu.is_file())):
        parser.error("existing image/QEMU required")
    suppress_windows_test_dialogs()
    return verify_artifacts(args) if args.verify_artifacts else run(args)


if __name__=="__main__": raise SystemExit(main())
