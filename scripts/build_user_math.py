"""Pinned, allocation-free binary64 libm; no Linux runtime or default linkage."""
from concurrent.futures import ThreadPoolExecutor
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile

from build_user_program import freestanding_compile_prefix

ROOT=Path(__file__).resolve().parents[1]
ARCHIVE=ROOT/"third_party/musl-1.2.6.tar.gz"
SHA256="d585fd3b613c66151fc3249e8ed44f77020cb5e6c1e635a616d3f9f82460512a"
MATH_ROOT=ROOT/"userspace/math"
PUBLIC=MATH_ROOT/"include"
FUNCTIONS=tuple("fabs sqrt cbrt floor ceil trunc round rint nearbyint sin cos tan asin acos "
    "atan atan2 sinh cosh tanh asinh acosh atanh exp exp2 expm1 log log2 log10 log1p pow "
    "hypot fmod remainder remquo frexp ldexp scalbn scalbln modf copysign fmin fmax fdim nextafter".split())
SUPPORT=tuple("__sin __cos __tan __rem_pio2 __rem_pio2_large __expo2 "
    "__math_divzero __math_invalid __math_oflow __math_uflow __math_xflow "
    "exp_data log_data log2_data pow_data sqrt_data".split())
HEADERS=tuple("exp_data log_data log2_data pow_data sqrt_data".split())
INTEGER_FUNCTIONS=("lrint",)
MEMBERS=("COPYRIGHT","src/internal/libm.h","arch/generic/fp_arch.h","include/features.h",
    "src/math/i386/sqrtl.c",
    *("src/math/"+name+".c" for name in FUNCTIONS+SUPPORT),
    *("src/math/i386/"+name+".c" for name in INTEGER_FUNCTIONS),
    *("src/math/"+name+".h" for name in HEADERS))


def extract(destination, archive=ARCHIVE):
    with Path(archive).open("rb") as stream:
        if hashlib.file_digest(stream,"sha256").hexdigest()!=SHA256:
            raise ValueError("musl archive SHA-256 mismatch")
    with tarfile.open(archive,"r:gz") as source:
        selected=[]; total=0
        if len(MEMBERS)>128: raise ValueError("musl member count")
        for name in MEMBERS:
            if name.startswith(("/","\\")) or "\\" in name or any(p in ("", ".", "..") for p in name.split("/")):
                raise ValueError("invalid musl numerical path")
            member=source.getmember("musl-1.2.6/"+name)
            total+=member.size
            if not member.isfile() or not 0<=member.size<=256*1024 or total>2*1024*1024:
                raise ValueError("invalid musl numerical member")
            selected.append((name,member))
        # Validate all selected metadata before writing anything.
        for name,member in selected:
            stream=source.extractfile(member)
            if stream is None: raise ValueError("missing musl numerical member")
            data=stream.read(member.size+1)
            if len(data)!=member.size: raise ValueError("musl member length")
            target=destination/name
            target.parent.mkdir(parents=True,exist_ok=True)
            target.write_bytes(data)
    return destination


def source_files(vendor):
    return tuple(vendor/("src/math/"+name+".c") for name in FUNCTIONS)+tuple(
        vendor/("src/math/i386/"+name+".c") for name in INTEGER_FUNCTIONS)+tuple(
        vendor/("src/math/"+name+".c") for name in SUPPORT)+(vendor/"src/math/i386/sqrtl.c", MATH_ROOT/"lib/fenv.c")


def includes(vendor):
    return [PUBLIC,MATH_ROOT/"private",vendor/"src/internal",vendor/"arch/generic",vendor/"include"]


def compile_math(zig,vendor,destination,environment,host=False,opt="-O2"):
    prefix=freestanding_compile_prefix(zig,includes(vendor),include_repository_sdk=False)
    if host:
        prefix[prefix.index("x86-freestanding")]="x86-windows-gnu"
        prefix += ["-D"+name+"=reist_math_"+name for name in FUNCTIONS+INTEGER_FUNCTIONS+
                   ("fegetround","fesetround","feclearexcept","fetestexcept")]
    # acosh's generic x87 evaluation path needs extended sqrt internally,
    # not a public long-double API. Keep the upstream i386 helper unchanged.
    prefix += [opt,"-Dhidden=__attribute__((visibility(\"hidden\")))",
               "-DREIST_MATH_BUILD_INTERNAL", "-Dsqrtl=reist_math_internal_sqrtl",
               "-Dweak_alias(a,b)="]  # No obsolete drem alias in profile 1.
    def compile_one(item):
        index,source=item
        obj=destination/(str(index)+(".obj" if host else ".o"))
        local_env=environment.copy()
        local_env["ZIG_LOCAL_CACHE_DIR"]=str(destination/("cache-"+str(index)))
        try:
            subprocess.run([*prefix,"-std=c11","-c",str(source),"-o",str(obj)],
                env=local_env,check=True,capture_output=True,text=True,timeout=90,
                creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
        except subprocess.CalledProcessError as error:
            raise RuntimeError(source.name+": "+error.stderr[-4000:]) from error
        return obj
    with ThreadPoolExecutor(max_workers=4) as pool:
        return list(pool.map(compile_one,enumerate(source_files(vendor))))


def copy_changed(source,destination):
    if not destination.is_file() or source.read_bytes()!=destination.read_bytes():
        destination.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(source,destination)


def build_math(root,zig,incremental=False):
    root=Path(root)
    library=root/"usr/lib/libm.a"
    headers=tuple(PUBLIC.glob("*.h"))
    inputs=(ARCHIVE,Path(__file__),ROOT/"scripts/build_user_program.py",zig,
            *MATH_ROOT.rglob("*.h"),MATH_ROOT/"lib/fenv.c")
    for header in headers: copy_changed(header,root/"usr/include/reist/math"/header.name)
    metadata=root/"usr/lib/pkgconfig/reistmath.pc"
    content=("prefix=${pcfiledir}/../..\nlibdir=${prefix}/lib\nincludedir=${prefix}/include\n"
        "Name: reistmath\nDescription: REIST binary64 math profile 1 (musl 1.2.6)\n"
        "Version: 1.0.0\nCflags: -I${includedir}/reist/math\nLibs: -L${libdir} -lm\n")
    if not metadata.is_file() or metadata.read_text()!=content:
        metadata.parent.mkdir(parents=True,exist_ok=True); metadata.write_text(content,encoding="ascii")
    license_root=root/"usr/share/licenses/musl-math"
    if (incremental and library.is_file() and (license_root/"COPYRIGHT").is_file() and
        all(p.stat().st_mtime_ns<=library.stat().st_mtime_ns for p in inputs)):
        return library
    with tempfile.TemporaryDirectory(prefix="reist-math-",dir=root) as temporary:
        directory=Path(temporary)
        vendor=extract(directory/"upstream")
        environment=os.environ.copy()
        environment["ZIG_GLOBAL_CACHE_DIR"]=str(root/"math-cache")
        environment["ZIG_LOCAL_CACHE_DIR"]=str(directory/"cache")
        objects=compile_math(zig,vendor,directory,environment)
        candidate=directory/"libm.a"
        subprocess.run([str(zig),"ar","rcs",str(candidate),*map(str,objects)],
            env=environment,check=True,capture_output=True,text=True,timeout=90,
            creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
        for name in MEMBERS: copy_changed(vendor/name,license_root/name)
        library.parent.mkdir(parents=True,exist_ok=True)
        candidate.replace(library)
    return library
