"""Opt-in pinned C string formatter with a private bounded memory stream."""
from concurrent.futures import ThreadPoolExecutor
import hashlib
import os
from pathlib import Path
import subprocess
import tarfile
import tempfile

from build_user_program import freestanding_compile_prefix
from build_user_math import ARCHIVE,SHA256,copy_changed

ROOT=Path(__file__).resolve().parents[1]
TEXT=ROOT/"userspace/text"
MEMBERS=("COPYRIGHT","src/stdio/snprintf.c","src/stdio/vsnprintf.c","src/stdio/vfprintf.c",
    "src/math/frexpl.c","src/math/scalbn.c","src/internal/libm.h","arch/generic/fp_arch.h")

def extract(destination,archive=ARCHIVE):
    with Path(archive).open("rb") as stream:
        if hashlib.file_digest(stream,"sha256").hexdigest()!=SHA256:
            raise ValueError("musl archive SHA-256 mismatch")
    with tarfile.open(archive,"r:gz") as source:
        selected=[]; total=0
        if len(MEMBERS)>32: raise ValueError("musl text member count")
        for name in MEMBERS:
            if name.startswith(("/","\\")) or "\\" in name or any(p in ("", ".", "..") for p in name.split("/")):
                raise ValueError("invalid musl text path")
            member=source.getmember("musl-1.2.6/"+name); total+=member.size
            if not member.isfile() or not 0<=member.size<=256*1024 or total>1024*1024:
                raise ValueError("invalid musl text member")
            selected.append((name,member))
        for name,member in selected:
            stream=source.extractfile(member)
            if stream is None: raise ValueError("missing musl text member")
            data=stream.read(member.size+1)
            if len(data)!=member.size: raise ValueError("musl text length")
            target=destination/name; target.parent.mkdir(parents=True,exist_ok=True); target.write_bytes(data)
    return destination

def replace_once(text,old,new):
    if text.count(old)!=1: raise ValueError("musl formatter adapter drift: "+old[:48])
    return text.replace(old,new)

def adapt(name,text):
    if name=="vfprintf.c":
        # Only intmax_t/uintmax_t are used, already supplied by stdint.h.
        # Do not install a fake inttypes/POSIX header for an unused include.
        text=replace_once(text,"#include <inttypes.h>\n","")
        # Stream-only specialization; would-have-written counts stay upstream.
        start="\tchar pad[256];\n\tif (fl & (LEFT_ADJ | ZERO_PAD) || l >= w) return;\n\tl = w - l;\n\tmemset(pad, c, l>sizeof pad ? sizeof pad : l);\n\tfor (; l >= sizeof pad; l -= sizeof pad)\n\t\tout(f, pad, sizeof pad);\n\tout(f, pad, l);"
        text=replace_once(text,start,"\tif (fl & (LEFT_ADJ | ZERO_PAD) || l >= w) return;\n\treist_text_repeat(f, c, (size_t)(w-l));")
        text=replace_once(text,"\t\t\tif (w<0) fl|=LEFT_ADJ, w=-w;",
            "\t\t\tif (w==INT_MIN) goto overflow;\n\t\t\tif (w<0) fl|=LEFT_ADJ, w=-w;")
        # A valid int precision plus a negative decimal exponent can exceed
        # INT_MAX before the upstream result-length checks. Preserve those
        # checks, widen only intermediate precision/rounding arithmetic.
        text=replace_once(text,"long double y, int w, int p, int fl", "long double y, int w, int64_t p, int fl")
        text=replace_once(text,"\tint e2=0, e, i, j, l;", "\tint e2=0, e, i, l;\n\tint64_t j;")
        for extra in (9,18):
            text=replace_once(text,f"\t\tpad(f, '0', p+{extra}, {extra}, 0);",
                "\t\treist_text_repeat(f, '0', p>0 ? (size_t)p : 0);")
    elif name=="vsnprintf.c":
        text=replace_once(text,"\t\t.cookie = &c,","\t\t.cookie = &c,\n\t\t.remaining = c.n,")
    else: raise ValueError("unexpected formatter adaptation")
    return text

def compile_text(zig,vendor,destination,environment,host=False,opt="-O2"):
    selected=[]
    for name in ("snprintf.c","vsnprintf.c","vfprintf.c"):
        source=vendor/"src/stdio"/name
        if name!="snprintf.c":
            generated=destination/name; generated.write_text(adapt(name,source.read_text()),encoding="ascii")
            source=generated
        selected.append(source)
    selected += [vendor/"src/math/frexpl.c",TEXT/"lib/stream.c"]
    if host: selected += [vendor/"src/math/scalbn.c",ROOT/"userspace/math/lib/fenv.c"]
    prefix=freestanding_compile_prefix(zig,[TEXT/"private",TEXT/"include",ROOT/"userspace/libc/include",
        ROOT/"userspace/math/include",ROOT/"userspace/math/private",vendor/"src/internal",vendor/"arch/generic"],
        include_repository_sdk=False)
    prefix += [opt,"-Dhidden=__attribute__((visibility(\"hidden\")))"]
    if host:
        prefix[prefix.index("x86-freestanding")]="x86-windows-gnu"
        prefix += ["-Dsnprintf=reist_text_snprintf","-Dvsnprintf=reist_text_vsnprintf"]
    def one(item):
        index,source=item; obj=destination/(str(index)+(".obj" if host else ".o"))
        env=environment.copy(); env["ZIG_LOCAL_CACHE_DIR"]=str(destination/("cache-"+str(index)))
        result=subprocess.run([*prefix,"-std=c11","-c",str(source),"-o",str(obj)],env=env,
            capture_output=True,text=True,timeout=90,creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
        if result.returncode:
            errors="\n".join(line for line in result.stderr.splitlines() if "error:" in line)
            raise RuntimeError(source.name+": "+errors+"\n"+result.stderr[-3000:])
        return obj
    with ThreadPoolExecutor(max_workers=4) as pool: return list(pool.map(one,enumerate(selected)))

def build_text(root,zig,incremental=False):
    root=Path(root); library=root/"usr/lib/libreisttext.a"
    inputs=(ARCHIVE,Path(__file__),ROOT/"scripts/build_user_math.py",ROOT/"scripts/build_user_program.py",zig,
        *TEXT.rglob("*.h"),TEXT/"lib/stream.c",*ROOT.joinpath("userspace/libc/include").rglob("*.h"),
        *ROOT.joinpath("userspace/math/include").rglob("*.h"),ROOT/"userspace/math/private/endian.h")
    copy_changed(TEXT/"include/stdio.h",root/"usr/include/reist/text/stdio.h")
    metadata=root/"usr/lib/pkgconfig/reisttext.pc"
    content=("prefix=${pcfiledir}/../..\nlibdir=${prefix}/lib\nincludedir=${prefix}/include\n"
        "Name: reisttext\nDescription: REIST C string formatting profile 1 (musl 1.2.6)\nVersion: 1.0.0\n"
        "Cflags: -I${includedir}/reist/text -I${includedir}/reist/libc -I${includedir}/reist/math\n"
        "Libs: -L${libdir} -lreisttext -lm -lreistc ${libdir}/libclang_rt.builtins-i386.a\n")
    if not metadata.is_file() or metadata.read_text()!=content:
        metadata.parent.mkdir(parents=True,exist_ok=True); metadata.write_text(content,encoding="ascii")
    licenses=root/"usr/share/licenses/musl-text"
    if incremental and library.is_file() and (licenses/"COPYRIGHT").is_file() and all(
        p.stat().st_mtime_ns<=library.stat().st_mtime_ns for p in inputs): return library
    with tempfile.TemporaryDirectory(prefix="reist-text-",dir=root) as temporary:
        directory=Path(temporary); vendor=extract(directory/"upstream")
        env=os.environ.copy(); env["ZIG_GLOBAL_CACHE_DIR"]=str(root/"text-cache")
        objects=compile_text(zig,vendor,directory,env)
        candidate=directory/"libreisttext.a"
        subprocess.run([str(zig),"ar","rcs",str(candidate),*map(str,objects)],env=env,check=True,
            capture_output=True,text=True,timeout=90,creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
        library.parent.mkdir(parents=True,exist_ok=True); candidate.replace(library)
        for name in MEMBERS: copy_changed(vendor/name,licenses/name)
    return library
