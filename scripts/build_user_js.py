"""Pinned opt-in QuickJS core; explicit generated omissions, no fake OS APIs."""
from concurrent.futures import ThreadPoolExecutor
import hashlib
import os
from pathlib import Path
import re
import subprocess
import tarfile
import tempfile

from build_user_program import freestanding_compile_prefix,find_zig
from build_user_math import copy_changed

ROOT=Path(__file__).resolve().parents[1]
ARCHIVE=ROOT/"third_party/quickjs-2026-06-04.tar.xz"
SHA256="b376e839b322978313d929fd20663b11ba58b75df5a46c126dd19ea2fa70ad2a"
JS=ROOT/"userspace/quickjs"
SOURCES=("quickjs.c","cutils.c","libregexp.c","libunicode.c","dtoa.c")
MEMBERS=SOURCES+("quickjs.h","quickjs-atom.h","quickjs-opcode.h","cutils.h","list.h",
    "libregexp.h","libregexp-opcode.h","libunicode.h","libunicode-table.h","dtoa.h","LICENSE","VERSION")

def replace(text,old,new):
    if text.count(old)!=1: raise ValueError("QuickJS adapter drift: "+old[:70])
    return text.replace(old,new,1)

def omit_function(text,name):
    # Pinned upstream C definitions close at column zero. Reject absent,
    # duplicate or changed declaration/body layout rather than guessing.
    pattern=r"(?ms)^[^\n;]*\b"+re.escape(name)+r"\([^;]*?\)\n\{.*?^\}\n"
    matches=list(re.finditer(pattern,text))
    if len(matches)!=1: raise ValueError("QuickJS function omission drift: "+name)
    match=matches[0]
    return text[:match.start()]+"/* REIST profile: omit "+name+". */\n"+text[match.end():]

def adapt(name,text):
    if name=="quickjs.c":
        text=replace(text,"#define CONFIG_ATOMICS","/* REIST: no OS threads or Atomics. */")
        text=replace(text,"#include <sys/time.h>\n#include <time.h>\n",'#include "reist_js_port.h"\n')
        start=text.index("/* default memory allocation functions with memory limitation */")
        end=text.index("void JS_SetMemoryLimit(",start)
        text=text[:start]+"/* REIST: runtime requires explicit JSMallocFunctions. */\n"+text[end:]
        text=omit_function(text,"JS_NewContext")
        text=omit_function(text,"getTimezoneOffset")
        start=text.index("/* Date */\n\nstatic int64_t math_mod")
        end=text.index("/* eval */",start)
        text=text[:start]+"/* REIST: Date not installed without wall-clock contract. */\n\n"+text[end:]
        text=replace(text,"""    } else if (p->class_id == JS_CLASS_DATE && s->ctx) {
        /* get_date_string() has no side effect */
        JSValue str = get_date_string(s->ctx, JS_MKPTR(JS_TAG_OBJECT, p), 0, NULL, 0x23); /* toISOString() */
        if (JS_IsException(str))
            goto default_obj;
        js_print_raw_string(s, str);
        JS_FreeValueRT(s->rt, str);
        comma_state = 2;
""","")
        text=replace(text,"    struct timeval tv;\n    gettimeofday(&tv, NULL);\n    ctx->random_state = ((int64_t)tv.tv_sec * 1000000) + tv.tv_usec;",
            "    ctx->random_state = reist_js_seed(JS_GetRuntimeOpaque(ctx->rt));")
        for function in ("js_malloc_dump_arenas","JS_DumpChar","JS_DumpString","JS_DumpAtoms",
            "JS_DumpShape","JS_DumpShapes","JS_DumpMemoryUsage","js_bigint_dump1","js_bigint_dump",
            "js_dump_value_write","print_atom","JS_DumpAtom","JS_DumpValue","JS_DumpValueRT",
            "JS_DumpObjectHeader","JS_DumpObject","JS_DumpGCObject","dump_token"):
            text=omit_function(text,function)
    elif name=="quickjs.h":
        text=replace(text,"#include <stdio.h>\n","")
        text=replace(text,"void JS_DumpMemoryUsage(FILE *fp, const JSMemoryUsage *s, JSRuntime *rt);\n","")
        for declaration in ("JSRuntime *JS_NewRuntime(void);\n","JSContext *JS_NewContext(JSRuntime *rt);\n",
                            "int JS_AddIntrinsicDate(JSContext *ctx);\n"):
            text=replace(text,declaration,"")
    elif name=="dtoa.c":
        text=replace(text,"#include <sys/time.h>\n","")
        text=replace(text,"#include <setjmp.h>\n","")
        text=omit_function(text,"mpb_dump")
    elif name=="libregexp.c":
        for function in ("lre_print_char","re_string_list_dump"):
            text=omit_function(text,function)
    elif name=="libunicode.c":
        text=omit_function(text,"cr_dump")
    return text

def extract(directory,archive=ARCHIVE):
    with Path(archive).open("rb") as stream:
        if hashlib.file_digest(stream,"sha256").hexdigest()!=SHA256: raise ValueError("QuickJS archive pin")
    if (ROOT/"third_party/quickjs-2026-06-04.sha256").read_text().split()[0]!=SHA256:
        raise ValueError("QuickJS sidecar pin")
    original=directory/"upstream"; generated=directory/"generated"
    with tarfile.open(archive,"r:xz") as packed:
        selected=[]; total=0
        if len(MEMBERS)>32 or len(set(MEMBERS))!=len(MEMBERS): raise ValueError("QuickJS member count")
        for name in MEMBERS:
            if Path(name).name!=name or name in (".",".."): raise ValueError("QuickJS member path")
            member=packed.getmember("quickjs-2026-06-04/"+name); total+=member.size
            if not member.isfile() or not 0<=member.size<=3*1024*1024 or total>8*1024*1024:
                raise ValueError("QuickJS member bounds")
            selected.append((name,member))
        for name,member in selected:
            data=packed.extractfile(member).read(member.size+1)
            if len(data)!=member.size: raise ValueError("QuickJS member length")
            original.mkdir(parents=True,exist_ok=True); generated.mkdir(parents=True,exist_ok=True)
            (original/name).write_bytes(data)
            (generated/name).write_text(adapt(name,data.decode()),encoding="utf-8")
    return original,generated

def compile_core(zig,generated,destination,env,host=False,opt="-O2"):
    directories=[JS/"include",JS/"private",ROOT/"userspace/text/include",ROOT/"userspace/math/include",
                 ROOT/"userspace/libc/include",generated]
    prefix=freestanding_compile_prefix(zig,directories,include_repository_sdk=False)
    # Zig's Windows Debug default injects UBSan and large helper frames. Match
    # the freestanding runtime (which has no sanitizer runtime) in both builds;
    # this does not disable QuickJS stack checks or compiler optimization tests.
    prefix += [opt,"-fno-sanitize=all","-std=gnu11","-DCONFIG_VERSION=\"2026-06-04\"",
               "-ffunction-sections","-fdata-sections","-Wno-unused-function","-Wno-unused-parameter"]
    if host:
        from build_user_math import FUNCTIONS,INTEGER_FUNCTIONS
        prefix[prefix.index("x86-freestanding")]="x86-windows-gnu"
        prefix += ["-D"+n+"=reist_math_"+n for n in FUNCTIONS+INTEGER_FUNCTIONS+
                   ("fegetround","fesetround","feclearexcept","fetestexcept")]
        prefix += ["-Dsnprintf=reist_text_snprintf","-Dvsnprintf=reist_text_vsnprintf"]
    sources=[generated/name for name in SOURCES]+[JS/"lib/script.c",JS/"lib/files.c",JS/"lib/engine.c"]
    def one(item):
        index,source=item; output=destination/(str(index)+(".obj" if host else ".o"))
        environment=env.copy(); environment["ZIG_LOCAL_CACHE_DIR"]=str(destination/("cache-"+str(index)))
        result=subprocess.run([*prefix,"-c",str(source),"-o",str(output)],capture_output=True,text=True,
            env=environment,timeout=90,creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
        return output,(source.name+":\n"+result.stderr if result.returncode else "")
    with ThreadPoolExecutor(max_workers=4) as workers: results=list(workers.map(one,enumerate(sources)))
    errors=[error for _,error in results if error]
    if errors: raise RuntimeError("\n".join(errors))
    return [output for output,_ in results]

def build_js(root,zig,incremental=False):
    root=Path(root); library=root/"usr/lib/libreistjs.a"
    inputs=(ARCHIVE,ARCHIVE.with_suffix("").with_suffix(".sha256"),Path(__file__),zig,
        ROOT/"scripts/build_user_program.py",*JS.rglob("*.h"),*JS.joinpath("lib").glob("*.c"),
        *ROOT.joinpath("userspace/libc/include").rglob("*.h"),
        *ROOT.joinpath("userspace/math/include").glob("*.h"),*ROOT.joinpath("userspace/text/include").glob("*.h"))
    copy_changed(JS/"include/reist_js.h",root/"usr/include/reist/js/reist_js.h")
    copy_changed(JS/"include/reist_js_script.h",root/"usr/include/reist/js/reist_js_script.h")
    copy_changed(JS/"include/reist_js_files.h",root/"usr/include/reist/js/reist_js_files.h")
    metadata=root/"usr/lib/pkgconfig/reistjs.pc"
    content=("prefix=${pcfiledir}/../..\nlibdir=${prefix}/lib\nincludedir=${prefix}/include\n"
        "Name: reistjs\nDescription: isolated REIST JavaScript core profile 1\nVersion: 1.0.0\n"
        "Cflags: -I${includedir}/reist/js\n"
        "Libs: -L${libdir} -lreistjs -lreisttext -lm -lreistc ${libdir}/libclang_rt.builtins-i386.a\n")
    if not metadata.is_file() or metadata.read_text()!=content:
        metadata.parent.mkdir(parents=True,exist_ok=True); metadata.write_text(content,encoding="ascii")
    license_root=root/"usr/share/licenses/quickjs"
    if incremental and library.is_file() and all((license_root/name).is_file() for name in MEMBERS) and all(
        path.stat().st_mtime_ns<=library.stat().st_mtime_ns for path in inputs): return library
    with tempfile.TemporaryDirectory(prefix="reist-js-",dir=root) as temporary:
        directory=Path(temporary); original,generated=extract(directory)
        env=os.environ.copy(); env["ZIG_GLOBAL_CACHE_DIR"]=str(root/"js-cache")
        objects=compile_core(zig,generated,directory,env)
        candidate=directory/"libreistjs.a"
        subprocess.run([str(zig),"ar","rcs",str(candidate),*map(str,objects)],env=env,check=True,
            capture_output=True,text=True,timeout=90,creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
        candidate.replace(library)
        for name in MEMBERS: copy_changed(original/name,license_root/name)
    return library

if __name__=="__main__":
    import argparse
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--incremental",action="store_true")
    args=parser.parse_args(); print(build_js(args.output,find_zig(),args.incremental))
