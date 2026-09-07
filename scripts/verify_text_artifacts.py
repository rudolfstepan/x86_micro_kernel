#!/usr/bin/env python3
"""R3.22: two cold HTML worker builds; unchanged core and actual image contents."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import time
import uuid

from build_html_engine import build as build_html
from build_user_program import build,find_zig
from build_user_sdk import sdk_artifacts,GUI_INCLUDE_ROOT,STORAGE_INCLUDE_ROOT
from build_system_programs import PROGRAMS as SOURCES,NETWORK_PARSER_PROGRAMS
from run_qemu_math import ROOT,KERNELS,PROGRAMS,digest,kernel_digest
from measure_cpp_baseline import suppress_windows_test_dialogs

EVIDENCE=ROOT/"build/codex-agent/r322-text"
PROOF=EVIDENCE/"html-rebuild.json"
UNCHANGED={name:value for name,value in PROGRAMS.items() if name!="HTMLWORK.PRG"}
UNCHANGED.update({
    "MATHTEST.PRG":"0bee6c4057aac105bb7eb87f63869902ccde11078fc40c69f258430b77c467c6",
    "TEXTTEST.PRG":"63776333af8e28e97e5a91196826194c471893d2fe3180f2d27f50ce202cf279"})

def inputs():
    sdk=sdk_artifacts(ROOT/"build/sdk")
    paths=set(SOURCES["HTMLWORK.PRG"])
    for directory in (ROOT/"userspace/gui/apps/browser",ROOT/"userspace/gui/include",
                      ROOT/"userspace/libc/include",ROOT/"userspace/sdk/include",
                      ROOT/"userspace/storage/include",sdk.include_dir):
        paths.update(directory.rglob("*.h")); paths.update(directory.rglob("*.hpp"))
    paths.update(ROOT/"scripts"/name for name in ("build_html_engine.py","build_user_program.py",
        "build_user_sdk.py","build_system_programs.py","verify_text_artifacts.py"))
    paths.update(ROOT/"third_party"/(name+suffix) for name in
        ("libhubbub","libparserutils","libcss","libwapcaplet") for suffix in (".tar.gz",".sha256"))
    paths.update((sdk.core_library,sdk.network_parser_library,sdk.startup_object,sdk.libc_library,
                  sdk.wapcaplet_library,sdk.library_dir/"libclang_rt.builtins-i386.a",find_zig()))
    return {str(p.resolve()):digest(p) for p in sorted(paths)}

def rebuild():
    report={"version":1,"passed":False,"inputs":inputs(),"products":[]}
    base=sdk_artifacts(ROOT/"build/sdk"); zig=find_zig()
    for label in ("a","b"):
        directory=EVIDENCE/("cold-"+label+"-"+uuid.uuid4().hex)
        sdk=sdk_artifacts(directory/"sdk")
        (sdk.library_dir/"pkgconfig").mkdir(parents=True)
        # Rebuild the affected parser libraries, not the unchanged OS/SDK.
        build_html(sdk,zig,False)
        worker=directory/"HTMLWORK.PRG"
        runtime=[base.core_library]
        if "HTMLWORK.PRG" in NETWORK_PARSER_PROGRAMS: runtime.append(base.network_parser_library)
        libraries=[sdk.library_dir/"libhubbub.a",sdk.library_dir/"libcss.a",base.wapcaplet_library,
                   sdk.library_dir/"libparserutils.a",base.libc_library,
                   base.library_dir/"libclang_rt.builtins-i386.a"]
        build(list(SOURCES["HTMLWORK.PRG"]),worker,zig,cpp=True,
            include_dirs=[base.cpp_include_dir,base.libc_include_dir,GUI_INCLUDE_ROOT,sdk.include_dir,
                          base.include_dir,STORAGE_INCLUDE_ROOT],
            libraries=libraries,runtime_objects=[base.startup_object],runtime_libraries=runtime,
            cache_directory=directory/"cache",compile_flags=["-DREIST_CSS_WORKER",
                "-ffunction-sections","-fdata-sections"])
        report["products"].append({"path":str(worker),"sha256":digest(worker),"bytes":worker.stat().st_size,
            "libraries":{p.name:digest(p) for p in libraries if p.parent==sdk.library_dir}})
    a,b=report["products"]
    if a["sha256"]!=b["sha256"] or a["libraries"]!=b["libraries"]:
        raise ValueError("independent cold HTML archives/worker differ: "+json.dumps(report["products"]))
    if report["inputs"]!=inputs(): raise ValueError("inputs changed during cold builds")
    report["passed"]=True
    return report

def verified_worker(report):
    if report.get("version")!=1 or report.get("passed") is not True or report.get("inputs")!=inputs():
        raise ValueError("stale or unaccepted cold-build proof")
    products=report.get("products",[])
    if len(products)!=2: raise ValueError("two independent products required")
    paths=[Path(p["path"]).resolve() for p in products]
    if paths[0]==paths[1] or any(not p.is_relative_to(EVIDENCE.resolve()) for p in paths):
        raise ValueError("invalid cold product paths")
    values=[digest(p) for p in paths]
    if values[0]!=values[1] or values!=[p["sha256"] for p in products]:
        raise ValueError("cold product digest mismatch")
    if products[0]["libraries"]!=products[1]["libraries"]:
        raise ValueError("cold library mismatch")
    return values[0]

def read_fat_file(image,path):
    # Read-only bounded walk of the existing native image data partition.
    # This binds packaged bytes; the reference gate validates the filesystem.
    with image.open("rb") as stream:
        length=image.stat().st_size
        def read(offset,size):
            if offset<0 or size<0 or offset+size>length: raise ValueError("FAT read bounds")
            stream.seek(offset); data=stream.read(size)
            if len(data)!=size: raise ValueError("FAT short read")
            return data
        base=8192*512; boot=read(base,512)
        reserved=struct.unpack_from("<H",boot,14)[0]; count=boot[16]
        sectors=struct.unpack_from("<I",boot,36)[0]; spc=boot[13]
        if (boot[510:]!=b"\x55\xaa" or struct.unpack_from("<H",boot,11)[0]!=512 or
            spc not in (1,2,4,8,16,32,64,128) or not reserved or count not in (1,2) or
            not sectors or base+(reserved+count*sectors)*512>=length):
            raise ValueError("invalid FAT geometry")
        fat=base+reserved*512; start=base+(reserved+count*sectors)*512; unit=spc*512
        def chain(cluster,limit):
            seen=set(); data=bytearray()
            for _ in range(16384):
                if cluster<2 or cluster in seen or cluster*4+4>sectors*512 or len(data)+unit>limit:
                    raise ValueError("FAT chain bounds/cycle")
                seen.add(cluster); data.extend(read(start+(cluster-2)*unit,unit))
                cluster=struct.unpack("<I",read(fat+4*cluster,4))[0]&0xfffffff
                if cluster>=0xffffff8: return bytes(data)
            raise ValueError("FAT chain capacity")
        cluster=struct.unpack_from("<I",boot,44)[0]
        components=path.upper().split("/")
        for level,name in enumerate(components):
            directory=chain(cluster,64*1024); entry=None
            for offset in range(0,len(directory),32):
                candidate=directory[offset:offset+32]
                if not candidate[0]: break
                if candidate[0]==229 or candidate[11]==15 or candidate[11]&8: continue
                title=candidate[:8].decode("ascii").strip(); ext=candidate[8:11].decode("ascii").strip()
                if title+("."+ext if ext else "")==name: entry=candidate; break
            if entry is None: raise ValueError("missing image entry "+path)
            cluster=(struct.unpack_from("<H",entry,20)[0]<<16)|struct.unpack_from("<H",entry,26)[0]
            final=level==len(components)-1
            if bool(entry[11]&16)==final: raise ValueError("wrong image entry type")
            if final:
                size=struct.unpack_from("<I",entry,28)[0]
                if not 0<size<=16*1024*1024: raise ValueError("invalid image program size")
                data=chain(cluster,16*1024*1024)
                if len(data)<size: raise ValueError("truncated image program")
                return data[:size]
        raise ValueError("empty image path")

def image_program_path(name):
    # Native image builder's existing 8.3 alias; the reader deliberately skips LFN entries.
    return {"BROWSER.PRG":"usr/gui/bin/browser.prg","GTEST.PRG":"libexec/reist/gtest.prg",
            "BENCHMARK.PRG":"usr/bin/benchm~1.prg"}.get(name,"usr/bin/"+name.lower())

def verify(image):
    if PROOF.stat().st_size>1024*1024: raise ValueError("cold proof capacity")
    worker=verified_worker(json.loads(PROOF.read_text()))
    expected={**UNCHANGED,"HTMLWORK.PRG":worker}
    report={"version":1,"passed":False,"programs":{},"images":{}}
    for name,value in expected.items():
        actual=digest(ROOT/"build/programs"/name)
        if actual!=value: raise ValueError("program changed: "+name)
        report["programs"][name]=actual
    if digest(ROOT/"build/kernel.bin")!=KERNELS["qemu"]: raise ValueError("common kernel changed")
    for profile,path in (("qemu",image),("vmware",ROOT/"build/vmware/reist-os/reist-os-flat.vmdk")):
        kernel=kernel_digest(path)
        if kernel!=KERNELS[profile]: raise ValueError("kernel changed: "+profile)
        for name,value in expected.items():
            guest=image_program_path(name)
            if hashlib.sha256(read_fat_file(path,guest)).hexdigest()!=value:
                raise ValueError("packaged program differs: "+profile+" "+name)
        report["images"][profile]={"path":str(path),"sha256":digest(path),"kernel_sha256":kernel}
    report["passed"]=True
    return report

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rebuild",action="store_true")
    parser.add_argument("--image",type=Path)
    parser.add_argument("--log",type=Path,required=True)
    args=parser.parse_args()
    if args.log.exists(): parser.error("refusing to overwrite evidence")
    if not args.rebuild and not args.image: parser.error("image required")
    suppress_windows_test_dialogs(); start=time.monotonic()
    try: report=rebuild() if args.rebuild else verify(args.image)
    except (OSError,ValueError,RuntimeError,KeyError,subprocess.SubprocessError) as error:
        report={"passed":False,"error":str(error)}
    report["elapsed_seconds"]=round(time.monotonic()-start,3)
    args.log.parent.mkdir(parents=True,exist_ok=True)
    args.log.write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print("TEXT_ARTIFACTS "+("PASS" if report["passed"] else "FAIL: "+report["error"])+
          f" elapsed={report['elapsed_seconds']}s log={args.log}")
    return 0 if report["passed"] else 1

if __name__=="__main__": raise SystemExit(main())
