"""Bounded normal-shell JS2, console and separated-realm guest proof."""
import argparse,queue,re,subprocess,threading,time
from pathlib import Path
import run_qemu_smoke as smoke
from measure_cpp_baseline import suppress_windows_test_dialogs

def inject(process,text):
    special={" ":"spc","/":"slash",".":"dot","-":"minus","(":"shift-9",")":"shift-0"}
    keys=[]
    for c in text:
        if "a"<=c<="z" or "0"<=c<="9": keys.append(c)
        elif c in special: keys.append(special[c])
        else: raise ValueError("unsupported command character")
    process.stdin.write(smoke.QEMU_MUX_SWITCH); process.stdin.flush()
    time.sleep(smoke.KEY_INTERVAL_SECONDS)
    try:
        for key in [*keys,"ret"]:
            process.stdin.write("sendkey "+key+"\n"); process.stdin.flush()
            time.sleep(smoke.KEY_INTERVAL_SECONDS)
    finally:
        process.stdin.write(smoke.QEMU_MUX_SWITCH); process.stdin.flush()

def validate_transcript(text):
    for marker in ("JS_RUNNER_RUNTIME_OK","JS_RUNNER_REALMS_OK","JS_RUNNER_CANCEL_OK","JS_RUNNER_SOURCE_OK",
                   "JS_RUNNER_STDOUT_OK","JS_RUNNER_STDERR_OK","JS_RUNNER_ARGV_OK"):
        if len(re.findall(r"(?m)^"+marker+r"\r?$",text))!=2: raise ValueError("missing/duplicate "+marker)
    cases=re.findall(r"(?m)^JS_RUNNER_CASE index=(\d+) status=(\d+)\r?$",text)
    if cases!=[(str(i),str(code)) for i,code in enumerate((7,1,1,71,0,0))]*2:
        raise ValueError("missing/wrong CLI case results")
    rows=re.findall(r"(?m)^JS_RUNNER_REAP mode=(\w+) pid=(\d+) generation=(\d+) status=(\d+)\r?$",text)
    if [r[0] for r in rows]!=["script","browser","fresh"]*2: raise ValueError("wrong reap order")
    if any(int(r[1])<=0 or int(r[2])<=0 or r[3]!="0" for r in rows): raise ValueError("invalid normal reap")
    timed=re.findall(r"(?m)^JS_RUNNER_TIMEOUT_OK pid=(\d+) generation=(\d+)\r?$",text)
    identities=[(r[1],r[2]) for r in rows]+timed
    if len(timed)!=2 or len(set(identities))!=8: raise ValueError("missing deadline/stale generation")
    if text.count("Arguments: shell 42")!=2 or text.count("Arguments: guest 42")!=2:
        raise ValueError("normal shell or VFS argv missing")
    if "JS_RUNNER_TEST_FAIL" in text or "*** USER PROCESS PAGE FAULT ***" in text or smoke.failure_marker(text):
        raise ValueError("guest failure")

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
            if overflow.is_set() or smoke.failure_marker(transcript) or "JS_RUNNER_TEST_FAIL" in transcript or "*** USER PROCESS PAGE FAULT ***" in transcript:
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
            # Also prove standalone invocation from a fresh shell: two newly
            # allocated worker slots may legitimately both have generation1.
            inject(process,"jsruntst"); at=wait("JS_RUNNER_RUNTIME_OK\n",at); at=wait(smoke.SHELL_PROMPT,at)
            inject(process,"js -e print(42)"); at=wait("\n42\n",at); at=wait(smoke.SHELL_PROMPT,at)
            inject(process,"js /htdocs/hello.js shell 42"); at=wait("Arguments: shell 42\n",at); at=wait(smoke.SHELL_PROMPT,at)
            inject(process,"js --help"); at=wait("js: usage:",at); at=wait(smoke.SHELL_PROMPT,at)
            inject(process,"help"); at=wait("Built-ins: cd path pwd history help exit",at); at=wait(smoke.SHELL_PROMPT,at)
        validate_transcript(transcript)
    except (OSError,ValueError,RuntimeError,TimeoutError) as caught: error=str(caught)
    finally:
        stopped.set(); smoke.stop_process(process); thread.join(timeout=2)
        try: drain()
        except ValueError as caught: error=str(caught)
        for stream in (process.stdin,process.stdout):
            if stream: stream.close()
        args.log.parent.mkdir(parents=True,exist_ok=True); args.log.write_text(transcript,encoding="utf-8")
    print(f"JS_RUNNER_RUNTIME {'FAIL: '+error if error else 'PASS'} elapsed={time.monotonic()-start:.3f}s log={args.log}")
    return 1 if error else 0

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu",type=Path,required=True)
    parser.add_argument("--image",type=Path,required=True)
    parser.add_argument("--log",type=Path,required=True)
    args=parser.parse_args()
    if args.log.exists(): parser.error("refusing to overwrite evidence")
    if not args.qemu.is_file() or not args.image.is_file(): parser.error("existing QEMU/image required")
    suppress_windows_test_dialogs()
    return run(args)
if __name__=="__main__": raise SystemExit(main())
