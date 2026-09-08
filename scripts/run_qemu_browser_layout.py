#!/usr/bin/env python3
"""R3.28 real scanout: static CSS, two widths, wheel, worker faults and recovery."""
import argparse
import hashlib
import inspect
import json
import re
import time
from functools import partial
from pathlib import Path
import run_qemu_runtime_desktop as desktop
from measure_cpp_baseline import suppress_windows_test_dialogs

ROOT=Path(__file__).resolve().parents[1]


def artifacts(args):
    from run_qemu_browser_external import artifacts as protected
    from verify_text_artifacts import read_fat_file
    # Accepted R3.27 protected payloads and real FAT/kernel extraction, not just
    # build-directory hashes. Keep its frozen protection command unchanged.
    if protected(args): return 1
    report=json.loads(args.log.read_text(encoding="utf-8"))
    report.update(baseline="bbeffe56",fixture={})
    # Opt-in R3.30 must change only the compositor, not either browser process.
    # Keep imported/default R3.28/R3.29 acceptance semantics unchanged.
    pinned = {}
    if getattr(args, "resize_inset", 0):
        report.update(baseline="814fc7b7", resize_protected={})
        pinned = {
            "usr/gui/bin/browser.prg": "60a8b3a7cd955a19287ee9989b762101373419206613417ceec095714d7f6f3d",
            "usr/bin/htmlwork.prg": "88121b6cd91bf379489f0756ce2aeccfdfdbcad17e80de306d5fa0706c3bfee1",
        }
    try:
        for profile,path in (("qemu",args.image),("vmware",ROOT/"build/vmware/reist-os/reist-os-flat.vmdk")):
            for name, expected in pinned.items():
                actual = hashlib.sha256(read_fat_file(path, name)).hexdigest()
                if actual != expected: raise ValueError("protected resize payload differs " + profile + " " + name)
                report["resize_protected"][profile + "/" + name] = actual
            for name in ("layout.htm","layout.css"):
                actual=read_fat_file(path,"htdocs/"+name)
                if actual!=(ROOT/"htdocs"/name).read_bytes(): raise ValueError("fixture differs "+profile+" "+name)
                report["fixture"][profile+"/"+name]=hashlib.sha256(actual).hexdigest()
    except (OSError,ValueError) as e: report["passed"]=False; report["error"]=str(e)
    args.log.write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print("BROWSER_LAYOUT_ARTIFACTS "+("PASS" if report["passed"] else "FAIL "+report["error"]))
    return 0 if report["passed"] else 1


def probe(process,output,transcript,screenshot,deadline,monitor,resize_inset=0):
    captures={}
    details={}
    def wait(pattern,offset=0):
        while time.monotonic()<deadline:
            desktop.drain(output,transcript); text="".join(transcript)
            desktop.browser_model_guest_health(text)
            match=re.search(pattern,text[offset:])
            if match:return match
            time.sleep(.01)
        raise RuntimeError("layout deadline: "+pattern)
    def capture(label,limit):
        path=screenshot.with_name(screenshot.stem+"-"+label+".ppm")
        if path.exists():raise RuntimeError("existing scanout evidence")
        monitor.execute("screendump",{"filename":str(path.resolve())},limit)
        ppm=desktop.read_ppm(path)
        if ppm is None:raise RuntimeError("invalid scanout")
        captures[label]=hashlib.sha256(ppm[2]).hexdigest()
        return ppm,path
    def geometry(marker,offset=0):
        r=wait(marker+r" width=(\d+) height=(\d+) scene=(\d+) total=(\d+)\r?\n",offset)
        width,height,scene,total=map(int,r.groups())
        section="".join(transcript)[offset:]
        # Wait for both complete lines, then check against the fixture's CSS
        # box equation, independently of the renderer's layout algorithm.
        wait(r"BROWSER_LAYOUT_TILE ordinal=1 x=\d+ y=\d+ w=\d+ h=\d+\r?\n",offset)
        section="".join(transcript)[offset:]
        tiles=[tuple(map(int,v)) for v in re.findall(r"BROWSER_LAYOUT_TILE ordinal=\d+ x=(\d+) y=(\d+) w=(\d+) h=(\d+)\r?\n",section)[:2]]
        if len(tiles)!=2 or total<=height:raise RuntimeError("missing scrollable static scene")
        content=min(scene,720)-32; left=(scene-min(scene,720))//2+16
        a,b=tiles
        if content>=496:
            if not(a[0]==left and a[2]==(content-16)//2 and b[0]==left+a[2]+16 and b[1]==a[1]):raise RuntimeError("wide grid geometry "+str(tiles))
        elif not(a[0]==b[0]==left and a[2]==b[2]==content and b[1]==a[1]+a[3]+16):raise RuntimeError("narrow grid geometry "+str(tiles))
        for name in ("BUTTON","NAV"):
            match=wait("BROWSER_LAYOUT_"+name+r" x=(\d+) y=(\d+) w=(\d+) h=(\d+)\r?\n",offset)
            details[name]=tuple(map(int,match.groups()))
        nx,ny,nw,nh=details["NAV"]
        bx,by,bw,bh=details["BUTTON"]
        if nx+nw!=left+content or nh!=16 or bx!=left+21 or bw!=162 or bh!=44:
            raise RuntimeError("nav/button geometry "+str(details))
        return width,height,scene,tiles
    def pixels(label,width,height,scene,tiles,scroll,origin,colors=True):
        # Surface ACK precedes composition. Observe boundedly, never reinject.
        for attempt in range(32):
            ppm,_=capture(label+"-"+str(attempt),deadline)
            ox,oy=origin
            good=True
            for tile,color in zip(tiles,(bytes.fromhex("dcebd6"),bytes.fromhex("d8e7f6"))):
                x,y,w,h=tile
                # Sample a centre-bottom stripe, outside glyphs and corners.
                sy=oy+76+y+h-20-scroll
                if sy<oy+76 or sy+4>oy+height-22:
                    continue
                stripe=desktop.browser_model_crop(ppm,ox+x+w//2,sy,8,4)
                if stripe!=color*32:good=False
            for name in ("BUTTON","NAV"):
                x,y,w,h=details[name]; sy=oy+76+y-scroll
                if sy<oy+76 or sy+h>oy+height-22:continue
                region=desktop.browser_model_crop(ppm,ox+x,sy,w,h)
                if region.count(bytes.fromhex("203d65"))<(128 if name=="BUTTON" else 8):good=False
            if good and (not colors or all(ppm[2].count(c)>64 for c in (bytes.fromhex("dcebd6"),bytes.fromhex("d8e7f6")))):
                return ppm
            time.sleep(.01)
        raise RuntimeError("layout scanout pixels "+label)
    wait(r"BROWSER_INPUT_READY\r?\n"); monitor.key("l")
    width,height,scene,tiles=geometry("BROWSER_LAYOUT_INITIAL_OK")
    monitor.key("ret"); _,_,origin=desktop.browser_model_initial(capture,width,height,deadline); monitor.key("esc")
    pixels("wide",width,height,scene,tiles,0,origin)
    initial,_=capture("before-resize",deadline)
    for _ in range((max(initial[0],initial[1])+119)//120):
        monitor.mouse(process,"mouse_move -120 -120");time.sleep(.01)
    desktop.browser_probe_wait_pointer(monitor,screenshot,(0,0),"layout-home")
    pointer=[0,0];corner=(origin[0]+width-1-resize_inset,origin[1]+height-1-resize_inset)
    desktop.shortcut_probe_move_mouse(process,pointer,*corner,monitor=monitor.mouse)
    desktop.browser_probe_wait_pointer(monitor,screenshot,corner,"layout-grip")
    if resize_inset:
        transcript.append(f"HOST_LAYOUT_RESIZE_INSET={resize_inset} x={corner[0]} y={corner[1]}\n")
    offset=len("".join(transcript));monitor.mouse(process,"mouse_button 1");time.sleep(.12)
    desktop.shortcut_probe_move_mouse(process,pointer,corner[0]-(width-480),corner[1],monitor=monitor.mouse)
    time.sleep(.12);monitor.mouse(process,"mouse_button 0")
    width,height,scene,tiles=geometry("BROWSER_LAYOUT_REFLOW_OK",offset)
    if width!=480:raise RuntimeError("wrong actual resize width")
    desktop.shortcut_probe_move_mouse(process,pointer,origin[0]+40,origin[1]+100,monitor=monitor.mouse)
    desktop.browser_probe_wait_pointer(monitor,screenshot,tuple(pointer),"layout-body")
    for _ in range(4):monitor.mouse(process,"mouse_move 0 0 -1")
    down=wait(r"BROWSER_LAYOUT_WHEEL_DOWN_OK scroll=(\d+)\r?\n")
    pixels("narrow-scrolled",width,height,scene,tiles,int(down[1]),origin)
    for _ in range(4):monitor.mouse(process,"mouse_move 0 0 1")
    wait("BROWSER_LAYOUT_WHEEL_UP_OK")
    stable=pixels("narrow-top",width,height,scene,tiles,0,origin,False)
    body=lambda ppm:desktop.browser_model_crop(ppm,origin[0],origin[1]+76,scene,height-98)
    previous=body(stable)
    for key,marker in (("f","FAULT"),("h","HANG")):
        monitor.key(key);wait("BROWSER_LAYOUT_"+marker+"_CONTAINED_OK")
        ppm=pixels(marker.lower(),width,height,scene,tiles,0,origin,False)
        if body(ppm)!=previous:raise RuntimeError("old page pixels changed after worker "+marker)
    offset=len("".join(transcript));monitor.key("r")
    width,height,scene,tiles=geometry("BROWSER_LAYOUT_RECOVERY_OK",offset)
    pixels("recovery",width,height,scene,tiles,0,origin,False)
    transcript.append("HOST_LAYOUT_TWO_WIDTH_PIXELS_OK\nHOST_LAYOUT_OLD_PAGE_OK\n")
    monitor.key("esc");wait("BROWSER_CLOSE_OK");wait("TERMINAL_INPUT_IDLE")
    for _ in range((max(initial[0],initial[1])+119)//120):
        monitor.mouse(process,"mouse_move -120 -120");time.sleep(.01)
    desktop.browser_probe_wait_pointer(monitor,screenshot,(0,0),"layout-exit-home")
    pointer=[0,0];centre=(initial[0]//2,initial[1]//2)
    desktop.shortcut_probe_move_mouse(process,pointer,*centre,monitor=monitor.mouse)
    desktop.browser_probe_wait_pointer(monitor,screenshot,centre,"layout-exit-centre")
    desktop.send_desktop_exit_click(process);wait("DESKTOP_EXIT_OK")
    offset=len("".join(transcript));desktop.send_command(process,"help");wait(desktop.SHELL_HELP_MARKER,offset)
    transcript.append("HOST_LAYOUT_SHELL_OK\n")
    text="".join(transcript)
    workers=re.findall(r"BROWSER_LAYOUT_WORKER pid=(\d+) generation=(\d+) mode=(\d+)",text)
    reaps=re.findall(r"BROWSER_LAYOUT_REAP pid=(\d+) generation=(\d+) status=(\d+)",text)
    if not workers or len({w[:2] for w in workers})!=len(workers) or sorted(w[:2] for w in workers)!=sorted(r[:2] for r in reaps):
        raise RuntimeError("unreaped or stale HTMLWORK generation")
    statuses={r[:2]:int(r[2]) for r in reaps}
    if sum(w[2]=="1" for w in workers)!=1 or sum(w[2]=="2" for w in workers)!=1 or any((statuses[w[:2]]==0)!=(w[2]=="0") for w in workers):
        raise RuntimeError("missing actual HTMLWORK fault/hang exits")
    screenshot.with_suffix(".scanout.json").write_text(json.dumps(captures,indent=2)+"\n",encoding="utf-8")
    print("BROWSER_LAYOUT_RUNTIME PASS static-two-widths-pixels-wheel-fault-hang-recovery")
    return 0


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--verify-artifacts",action="store_true");p.add_argument("--qemu",type=Path)
    p.add_argument("--image",required=True,type=Path);p.add_argument("--log",required=True,type=Path)
    p.add_argument("--resize-inset",type=int,choices=range(13),default=0,
                   help="move inside the client corner; 12 tests the inner 16px decorated corner boundary")
    args=p.parse_args()
    if args.log.exists():p.error("refusing to overwrite evidence")
    args.log.parent.mkdir(parents=True,exist_ok=True);suppress_windows_test_dialogs()
    if args.verify_artifacts:return artifacts(args)
    options={name:False for name,value in inspect.signature(desktop.run).parameters.items() if value.default is inspect.Parameter.empty}
    screenshot=args.log.with_suffix(".ppm")
    options.update(qemu=args.qemu,image=args.image,screenshot=screenshot,timeout=180.0,metrics_log=None,smp=1,browser_input_probe=True)
    original=desktop.run_browser_input_probe
    desktop.run_browser_input_probe=partial(probe,resize_inset=args.resize_inset)
    start=time.monotonic()
    try:return desktop.run(**options)
    except (OSError,RuntimeError,ValueError) as e:print("BROWSER_LAYOUT_RUNTIME FAIL "+str(e));return 1
    finally:
        desktop.run_browser_input_probe=original
        evidence=screenshot.with_suffix(".browser.log")
        if evidence.exists():args.log.write_bytes(evidence.read_bytes())
        print(f"BROWSER_LAYOUT_RUNTIME elapsed={time.monotonic()-start:.3f}s log={args.log}")


if __name__=="__main__":raise SystemExit(main())
