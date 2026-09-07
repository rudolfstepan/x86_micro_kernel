"""Actual binary64 algorithms, i386 environment and opt-in SDK contract."""
import os
from pathlib import Path
import subprocess
import sys
import unittest
import uuid
import hashlib
import re
from unittest.mock import patch,MagicMock
import tarfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"scripts"))
from build_user_program import find_zig,freestanding_compile_prefix
from measure_cpp_baseline import suppress_windows_test_dialogs


class MathTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        suppress_windows_test_dialogs()
        cls.directory=ROOT/"build/codex-agent/r321-math"/("host-"+uuid.uuid4().hex)
        cls.directory.mkdir(parents=True)
        cls.env=os.environ.copy()
        cls.env["ZIG_GLOBAL_CACHE_DIR"]=str(ROOT/"build/codex-agent/r321-math/zig-global")
        cls.env["ZIG_LOCAL_CACHE_DIR"]=str(cls.directory/"zig-local")

    def test_public_i386_headers(self):
        suppress_windows_test_dialogs()
        command=freestanding_compile_prefix(find_zig(),[ROOT/"userspace/math/include"])
        source='#include <math.h>\n#include <fenv.h>\ndouble probe(double x) { return sqrt(x)+sin(x); }\n'
        # Emit real objects: Zig's freestanding syntax-only driver reports
        # FileNotFound even for a retained, existing input. Object compilation
        # also verifies both language frontends and the target calling boundary.
        fixture=self.directory/"public-headers.c"
        fixture.write_text(source,encoding="ascii")
        result=subprocess.run([*command,"-Werror","-x","c","-c",str(fixture),"-o",str(self.directory/"public-c.o")],
            text=True,capture_output=True,timeout=30,env=self.env,
            creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        result=subprocess.run([*command,"-Werror",
            "-std=c++20","-x","c++","-c",str(fixture),"-o",str(self.directory/"public-cpp.o")],text=True,
            capture_output=True,timeout=30,env=self.env,
            creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def command(self,command,timeout=90):
        result=subprocess.run(list(map(str,command)),capture_output=True,text=True,
            env=self.env,timeout=timeout,creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        return result.stdout

    def test_actual_i386_math_and_fenv_o0_o2(self):
        from build_user_math import extract,compile_math,FUNCTIONS,PUBLIC
        vendor=extract(self.directory/"upstream")
        zig=find_zig()
        for opt in ("-O0","-O2"):
            with self.subTest(opt=opt):
                output=self.directory/opt; output.mkdir()
                objects=compile_math(zig,vendor,output,self.env,host=True,opt=opt)
                exe=output/"math-host.exe"
                defines=["-D"+name+"=reist_math_"+name for name in FUNCTIONS+
                    ("fegetround","fesetround","feclearexcept","fetestexcept")]
                self.command([zig,"cc","-target","x86-windows-gnu",opt,"-fno-builtin",
                    "-mno-sse","-mno-sse2","-mno-mmx","-Wall","-Wextra","-Werror",
                    *defines,"-I",PUBLIC,ROOT/"test/math_host.c",*objects,"-o",exe])
                self.assertIn("MATH_HOST_OK functions=44 rounding=4 reference_samples=768",self.command([exe],30))

    def test_sdk_archive_dependency_closure_and_incremental(self):
        from build_user_math import build_math,FUNCTIONS
        root=self.directory/"sdk"
        library=build_math(root,find_zig())
        self.assertTrue((root/"usr/share/licenses/musl-math/COPYRIGHT").is_file())
        self.assertTrue((root/"usr/share/licenses/musl-math/src/math/sin.c").is_file())
        self.assertIn("-lm",(root/"usr/lib/pkgconfig/reistmath.pc").read_text())
        defined_text=self.command(["C:/msys64/mingw64/bin/nm.exe","--defined-only","--extern-only",library])
        defined=set(re.findall(r"(?m)^\S+\s+[A-Za-z]\s+(\S+)$",defined_text))
        self.assertTrue(set(FUNCTIONS)<=defined,sorted(set(FUNCTIONS)-defined))
        undefined_text=self.command(["C:/msys64/mingw64/bin/nm.exe","-u",library])
        undefined=set(re.findall(r"(?m)^\s+U\s+(\S+)$",undefined_text))
        self.assertFalse(undefined-defined,sorted(undefined-defined))
        assembly=self.command(["C:/msys64/mingw64/bin/objdump.exe","-d",library])
        self.assertNotRegex(assembly,r"\b(?:xmm[0-9]+|mm[0-7]|cr[034])\b")
        self.assertNotIn("drem",defined)
        self.assertNotIn("sqrtl",defined)
        stamp=library.stat().st_mtime_ns
        digest=hashlib.sha256(library.read_bytes()).hexdigest()
        build_math(root,find_zig(),incremental=True)
        self.assertEqual(stamp,library.stat().st_mtime_ns)
        self.assertEqual(digest,hashlib.sha256(library.read_bytes()).hexdigest())

    def test_pin_and_no_implicit_linkage(self):
        from build_user_math import SHA256,FUNCTIONS,extract
        self.assertEqual(len(FUNCTIONS),44)
        self.assertEqual((ROOT/"third_party/musl-1.2.6.sha256").read_text().split()[0],SHA256)
        invalid=self.directory/"invalid.tar.gz"; invalid.write_bytes(b"not the upstream archive")
        destination=self.directory/"denied"
        with self.assertRaisesRegex(ValueError,"SHA-256"): extract(destination,invalid)
        self.assertFalse(destination.exists())
        import inspect
        from build_user_program import resolve_sysroot_runtime
        self.assertNotIn("libm.a",inspect.getsource(resolve_sysroot_runtime))
        fenv=(ROOT/"userspace/math/lib/fenv.c").read_text()
        for denied in ("x86os_","malloc(","free(","cr0","cr4","cli","sti"):
            self.assertNotIn(denied,fenv)

    def test_real_shell_and_both_image_layouts(self):
        self.assertIn('"MATHTEST.PRG": ROOT / "userspace/programs/mathtest.c"',
            (ROOT/"scripts/build_system_programs.py").read_text())
        self.assertIn("'usr/bin/mathtest.prg' = 'MATHTEST.PRG'",(ROOT/"scripts/build-windows.ps1").read_text())
        self.assertIn("usr/bin/mathtest.prg=$(SYSTEM_PROGRAM_DIR)/MATHTEST.PRG",(ROOT/"Makefile").read_text())
        shell=(ROOT/"userspace/bin/shell.c").read_text()
        paths=shell.split("static char search_paths[",1)[1].split("};",1)[0]
        self.assertIn('"/usr/bin"',paths)

    def test_mathtest_links_byte_runtime_without_unused_compiler_archive(self):
        source=(ROOT/"scripts/build_system_programs.py").read_text()
        caller=source.split('if name == "MATHTEST.PRG":',1)[1].split('if name in ',1)[0]
        # Aggregate initialization emits memset. The arithmetic-only compiler
        # archive is not the byte runtime and has a recursive weak fallback.
        self.assertIn("sdk.libc_library",caller)
        self.assertNotIn("libclang_rt.builtins-i386.a",caller)
        self.assertIn("Path(__file__).resolve()",caller)

    def test_extraction_rejects_metadata_before_writes(self):
        from build_user_math import extract
        for kind,size in ((tarfile.SYMTYPE,1),(tarfile.REGTYPE,-1),
                          (tarfile.REGTYPE,256*1024+1),(tarfile.REGTYPE,256*1024)):
            with self.subTest(kind=kind,size=size):
                member=tarfile.TarInfo("ignored"); member.type=kind; member.size=size
                source=MagicMock(); source.__enter__.return_value=source
                source.getmember.return_value=member
                destination=self.directory/"metadata-denied"
                with patch("build_user_math.tarfile.open",return_value=source):
                    with self.assertRaises(ValueError): extract(destination)
                self.assertFalse(destination.exists())
                source.extractfile.assert_not_called()

    def test_guest_validator_rejects_missing_or_stale_proof(self):
        from run_qemu_math import validate_transcript
        lines=["REIST_SMP READY online=4 parked=3 failed=0"]
        lines += [f"REIST_FPU AP_CONTEXT_OK cpu={i}" for i in (1,2,3)]
        for run in range(2):
            lines += ["MATH_NUMERIC_OK functions=44","MATH_FENV_OK rounding=4"]
            for i,(mode,status) in enumerate((("normal",37),("fault",144),("hold",143),("normal",37))):
                lines += [f"MATH_REAP_OK mode=--{mode} status={status} pid=7 generation={run*4+i+1}"]
            lines += ["Exception: Floating point (IRQ 16)","MATH_PARENT_OK","MATH_RUNTIME_OK"]
        text="\n".join(lines)+"\n"
        validate_transcript(text,4)
        with self.assertRaises(ValueError):
            validate_transcript(text+"*** USER PROCESS PAGE FAULT ***\n",4)
        for old,new in (("IRQ 16","IRQ 6"),("generation=8","generation=1"),
                        ("status=144","status=94"),("MATH_PARENT_OK",""),
                        ("AP_CONTEXT_OK cpu=3","AP_CONTEXT_OK cpu=2")):
            with self.subTest(old=old):
                with self.assertRaises(ValueError): validate_transcript(text.replace(old,new),4)


if __name__=="__main__": unittest.main()
