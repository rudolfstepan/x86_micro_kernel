#!/usr/bin/env python3
"""Bounded real Ring-3 FPU isolation and fault/reuse proof (headless snapshot)."""
import argparse
import hashlib
import json
import math
import os
import queue
import re
import shutil
import statistics
import subprocess
import threading
import time
import uuid
from pathlib import Path
import run_qemu_smoke as smoke
from run_qemu_system_layout import inject, send_and_wait

ROOT=Path(__file__).resolve().parents[1]
VMWARE_PREFLIGHT=("$busy=@(Get-Process qemu-system-i386,qemu-system-x86_64,zig,clang,"
    "gcc,cc1,cc1plus,nasm,ld,lld -ErrorAction SilentlyContinue); "
    "if ($busy.Count) { throw 'Concurrent VM/compiler blocks runtime measurement.' }; exit 0")


def vmware_command(clone, log, mode):
    subprocess.run(["C:/Program Files/PowerShell/7/pwsh.exe","-NoProfile","-Command",VMWARE_PREFLIGHT],
        check=True,timeout=15,capture_output=True,text=True,
        creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
    command=["C:/Program Files/PowerShell/7/pwsh.exe","-NoProfile","-File",
        str(ROOT/"scripts/run_vmware_mouse.ps1"),mode,
        "-SourcePackage",str(clone),"-GateLog",str(log)]
    try:
        return subprocess.run(command,capture_output=True,text=True,timeout=300,
            creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
    except subprocess.TimeoutExpired:
        # Exact UUID-owned clone only. No argument reinterpretation by -Command.
        cleanup=("$target=[IO.Path]::GetFullPath($env:REIST_FPU_GATE_VMX); "
            "$quoted='\"'+$target+'\"'; "
            "Get-CimInstance Win32_Process -ErrorAction Stop | Where-Object { "
            "($_.Name -eq 'vmware-vmx.exe' -or $_.Name -eq 'vmware.exe') -and "
            "$_.CommandLine -and $_.CommandLine.Contains($quoted) } | "
            "ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction Stop }")
        env=os.environ.copy(); env["REIST_FPU_GATE_VMX"]=str(clone/"reist-os.vmx")
        subprocess.run(["C:/Program Files/PowerShell/7/pwsh.exe","-NoProfile","-Command",cleanup],
            check=True,timeout=20,capture_output=True,text=True,env=env,
            creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
        raise


def vmware_fpu(args):
    from measure_cpp_baseline import suppress_windows_test_dialogs
    suppress_windows_test_dialogs()
    evidence=args.log.resolve()
    if evidence.exists(): raise ValueError("refusing to overwrite FPU evidence")
    destination=evidence.parent/("fpu-workstation-"+uuid.uuid4().hex)
    destination.mkdir(parents=True)
    source=args.vmware_fpu_package.resolve()
    result={"passed":False,"source":str(source),"directory":str(destination)}
    start=time.monotonic()
    try:
        vmx=(source/"reist-os.vmx").read_text()
        for field in ('memsize = "1024"','numvcpus = "4"'):
            if field not in vmx: raise ValueError("VMware FPU profile mismatch")
        result["image_sha256"]=digest(source/"reist-os-flat.vmdk")
        result["vmx_sha256"]=digest(source/"reist-os.vmx")
        result["harness_sha256"]=digest(ROOT/"scripts/run_vmware_mouse.ps1")
        clone=destination/"package"; shutil.copytree(source,clone)
        log=destination/"serial.log"
        completed=vmware_command(clone,log,"-FpuIsolation")
        (destination/"command.log").write_text(completed.stdout+completed.stderr,encoding="utf-8")
        print(completed.stdout.strip(),flush=True)
        if completed.returncode: raise ValueError("Workstation failed: "+completed.stderr[-1800:])
        validate_transcript(log.read_text(encoding="utf-8-sig"),4)
        if (digest(source/"reist-os-flat.vmdk")!=result["image_sha256"] or
            digest(source/"reist-os.vmx")!=result["vmx_sha256"]):
            raise ValueError("source package changed during FPU proof")
        result["passed"]=True
    except (OSError,ValueError,subprocess.SubprocessError) as error:
        result["error"]=str(error)
    finally:
        result["seconds"]=time.monotonic()-start
        evidence.write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
    print("VMWARE_FPU "+("PASS" if result["passed"] else "FAIL: "+result.get("error","unknown")),flush=True)
    return 0 if result["passed"] else 1


def benchmark_rows(text):
    rows={}
    for area,name,value,unit in re.findall(
        r"\|\s*(CPU|RAM|HDD)\s*\|\s*([^|]+?)\s*\|\s*([0-9]+(?:[.,][0-9]+)?)\s+(MOp/s|MiB/s|KiB/s|x)\s*\|\s*OK\s*\|",text):
        key=area+"/"+name.strip()
        if key in rows: raise ValueError("duplicate benchmark row: "+key)
        number=float(value.replace(",","."))
        if not math.isfinite(number) or number<=0: raise ValueError("invalid benchmark rate")
        rows[key]={"value":number,"unit":unit}
    expected={"CPU/Single CPU":"MOp/s","CPU/Multi CPU gesamt":"MOp/s",
              "CPU/Multi/Single":"x","RAM/Schreiben":"MiB/s","RAM/Lesen":"MiB/s",
              "HDD/Seq. Schreiben":"KiB/s","HDD/Seq. Lesen":"KiB/s"}
    if {k:v["unit"] for k,v in rows.items()}!=expected:
        raise ValueError("incomplete benchmark rows")
    return rows


def compare_benchmarks(samples):
    if len(samples)!=6 or [s["side"] for s in samples]!=["before","after"]*3:
        raise ValueError("expected three ordered fresh before/after pairs")
    result={}
    for key in ("CPU/Single CPU","CPU/Multi CPU gesamt"):
        before=statistics.median(s["rows"][key]["value"] for s in samples if s["side"]=="before")
        after=statistics.median(s["rows"][key]["value"] for s in samples if s["side"]=="after")
        if not math.isfinite(before) or not math.isfinite(after) or before<=0 or after<=0:
            raise ValueError("invalid CPU median")
        result[key]={"before":before,"after":after,"ratio":after/before}
        if after<0.95*before: raise ValueError("CPU regression: "+key+" "+repr(result[key]))
    return result


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream,"sha256").hexdigest()


def validate_rejection(text):
    if "REIST_FPU READY" in text or "BOOT_OK" in text or smoke.SHELL_PROMPT in text:
        raise ValueError("unsupported hardware reached admission")
    return ("REIST_FPU UNSUPPORTED cpu=0" in text and
            "Unsupported or inconsistent FPU context profile" in text)


def vmware_benchmark(args):
    from measure_cpp_baseline import suppress_windows_test_dialogs
    suppress_windows_test_dialogs()
    evidence=args.log.resolve()
    if evidence.exists(): raise ValueError("refusing to overwrite benchmark evidence")
    destination=evidence.parent/("workstation-"+uuid.uuid4().hex)
    destination.mkdir(parents=True)
    result={"passed":False,"samples":[],"minimum_cpu_ratio":0.95,
            "note":"RAM/HDD are short tick-quantized diagnostics, not a 5-percent guarantee",
            "harness_sha256":digest(ROOT/"scripts/run_vmware_mouse.ps1"),
            "benchmark_source_sha256":digest(ROOT/"userspace/programs/benchmark.c"),
            "switch_sha256":digest(ROOT/"kernel/sched/switch.asm")}
    sources={"before":args.vmware_benchmark_before.resolve(),"after":args.vmware_benchmark_after.resolve()}
    result["sources"]={}
    try:
        for side,source in sources.items():
            vmx=(source/"reist-os.vmx").read_text()
            for field in ('memsize = "1024"','numvcpus = "4"'):
                if field not in vmx: raise ValueError("VMware comparison profile mismatch: "+side)
            result["sources"][side]={"directory":str(source),
                "image_sha256":digest(source/"reist-os-flat.vmdk"),
                "vmx_sha256":digest(source/"reist-os.vmx")}
        for pair in range(3):
            for side,source in sources.items():
                clone=destination/(str(pair)+"-"+side)
                shutil.copytree(source,clone)
                log=destination/(str(pair)+"-"+side+".log")
                start=time.monotonic()
                completed=vmware_command(clone,log,"-Benchmark")
                (destination/(str(pair)+"-"+side+"-command.log")).write_text(
                    completed.stdout+completed.stderr,encoding="utf-8")
                print(completed.stdout.strip(),flush=True)
                if completed.returncode: raise ValueError("Workstation gate failed: "+str(log)+" "+completed.stderr[-1500:])
                result["samples"].append({"pair":pair,"side":side,"log":str(log),
                    "seconds":time.monotonic()-start,"rows":benchmark_rows(log.read_text(encoding="utf-8-sig"))})
        for side,source in sources.items():
            if (digest(source/"reist-os-flat.vmdk")!=result["sources"][side]["image_sha256"] or
                digest(source/"reist-os.vmx")!=result["sources"][side]["vmx_sha256"]):
                raise ValueError("source package changed during comparison")
        result["medians"]=compare_benchmarks(result["samples"])
        result["passed"]=True
    except (OSError,ValueError,subprocess.SubprocessError) as error:
        result["error"]=str(error)
    finally:
        evidence.write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
    print("VMWARE_PAIRED "+("PASS" if result["passed"] else "FAIL: "+result.get("error","unknown")),flush=True)
    return 0 if result["passed"] else 1


def validate_transcript(text, cpus, profile="hardware"):
    if profile not in ("hardware","tcg"): raise ValueError("unknown FPU profile")
    faults="FPU_FAULTS_OK mf=144 xm=147 gp=141" if profile=="hardware" else \
        "FPU_TCG_FAULTS_OK mf=144 gp_align=141 sse=workstation-required"
    terminal="FPU_OK" if profile=="hardware" else "FPU_TCG_OK"
    for marker in ("FPU_BEGIN", "FPU_PREEMPT_OK parent", "FPU_PREEMPT_OK child",
                   faults, "FPU_REUSE_OK", terminal):
        if len(re.findall(r"(?m)^"+re.escape(marker)+r"\r?$",text))!=2:
            raise ValueError("missing/duplicate marker: "+marker)
    for vector in ((16,19,13) if profile=="hardware" else (16,13)):
        # Actual vector and exact child statuses are both required.
        if len(re.findall(r"Exception: [^\r\n]+ \(IRQ "+str(vector)+r"\)",text))!=2:
            raise ValueError("missing/duplicate user fault vector: "+str(vector))
    ap=re.findall(r"REIST_FPU AP_CONTEXT_OK cpu=(\d+)",text)
    if sorted(map(int,ap))!=list(range(1,cpus)):
        raise ValueError("incomplete AP context proof: "+repr(ap))
    if "PANIC" in text or "TEST_FAIL" in text:
        raise ValueError("kernel/test failure in transcript")


def qemu_fpu_command(args):
    command=smoke.qemu_command(args.qemu,args.image,no_apic=args.no_apic,
                               memory="1024M",nic="e1000",smp=args.smp)
    if args.unsupported:
        # Test only the missing FP profile. Pentium lacks the integer CMOV
        # baseline too and faults before this kernel reaches CPU admission.
        if "-cpu" in command:
            command[command.index("-cpu")+1]+=",-sse,-sse2"
        else:
            command.extend(["-cpu","qemu32,-sse,-sse2"])
    return command


def run(args):
    if args.log.exists(): raise ValueError("refusing to overwrite guest evidence")
    start=time.monotonic()
    deadline=start+(60 if args.unsupported else 180)
    command=qemu_fpu_command(args)
    process=subprocess.Popen(command,stdin=subprocess.PIPE,stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT,text=True,encoding="utf-8",
                             errors="replace",bufsize=0,
                             creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
    chunks=queue.Queue(); transcript=[]; finished=threading.Event()
    reader=threading.Thread(target=smoke.reader,args=(process.stdout,chunks,finished),daemon=True)
    reader.start(); error=None
    try:
        if args.unsupported:
            # Only this negative boot proof expects the explicit fatal profile
            # rejection. Normal guests keep the shared fail-marker policy.
            error="missing unsupported-profile rejection"
            while time.monotonic()<deadline:
                smoke.drain(chunks,transcript)
                if validate_rejection("".join(transcript)):
                    error=None; break
                if process.poll() is not None: break
                try: transcript.append(chunks.get(timeout=0.05))
                except queue.Empty: pass
        else:
            error,position=smoke.wait_for_line(process,chunks,transcript,finished,smoke.SHELL_PROMPT,deadline)
            for _ in range(2):
                if error: break
                inject(process,"/libexec/reist/gtest.prg fpu-tcg")
                for marker in ("FPU_BEGIN","FPU_TCG_OK",smoke.SHELL_PROMPT):
                    error,position=smoke.wait_for_line(process,chunks,transcript,finished,
                                                      marker,deadline,after=position)
                    if error: break
                if not error:
                    error,position=send_and_wait(process,chunks,transcript,finished,"path",
                        "PATH=C:\\bin;C:\\sbin;C:\\usr\\bin;C:\\usr\\gui\\bin",deadline,position)
            if not error: validate_transcript("".join(transcript),args.smp,profile="tcg")
    except (OSError,ValueError,RuntimeError,TimeoutError) as caught:
        error=str(caught)
    finally:
        smoke.stop_process(process)
        finished.wait(timeout=1); smoke.drain(chunks,transcript); reader.join(timeout=1)
        args.log.parent.mkdir(parents=True,exist_ok=True)
        args.log.write_text("".join(transcript),encoding="utf-8")
    print(f"FPU_RUNTIME {'FAIL: '+error if error else 'PASS'} elapsed={time.monotonic()-start:.3f}s log={args.log}")
    return 1 if error else 0


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu",type=Path)
    parser.add_argument("--image",type=Path)
    parser.add_argument("--vmware-benchmark-before",type=Path)
    parser.add_argument("--vmware-benchmark-after",type=Path)
    parser.add_argument("--vmware-fpu-package",type=Path)
    parser.add_argument("--log",type=Path,required=True)
    parser.add_argument("--smp",type=int,choices=(1,4),default=1)
    parser.add_argument("--no-apic",action="store_true")
    parser.add_argument("--unsupported",action="store_true")
    args=parser.parse_args()
    if args.vmware_fpu_package:
        if args.qemu or args.image or args.vmware_benchmark_before or args.vmware_benchmark_after:
            parser.error("FPU Workstation requires only package and log")
        return vmware_fpu(args)
    if args.vmware_benchmark_before or args.vmware_benchmark_after:
        if not (args.vmware_benchmark_before and args.vmware_benchmark_after) or args.qemu or args.image:
            parser.error("benchmark requires only both VMware packages and log")
        return vmware_benchmark(args)
    if not args.qemu or not args.image: parser.error("QEMU and image are required")
    return run(args)


if __name__=="__main__":
    raise SystemExit(main())
