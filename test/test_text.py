"""Real bounded string formatting, not source patterns as runtime evidence."""
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys
import unittest
import uuid
from unittest.mock import MagicMock,patch
import tarfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"scripts"))
from build_user_program import find_zig,freestanding_compile_prefix
from measure_cpp_baseline import suppress_windows_test_dialogs

class TextTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        suppress_windows_test_dialogs()
        cls.directory=ROOT/"build/codex-agent/r322-text"/("host-"+uuid.uuid4().hex)
        cls.directory.mkdir(parents=True)
        cls.env=os.environ.copy()
        cls.env["ZIG_GLOBAL_CACHE_DIR"]=str(ROOT/"build/codex-agent/r322-text/zig-global")
        cls.env["ZIG_LOCAL_CACHE_DIR"]=str(cls.directory/"zig-local")

    def command(self,command,timeout=90):
        result=subprocess.run(list(map(str,command)),capture_output=True,text=True,env=self.env,
            timeout=timeout,creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        return result.stdout

    def test_public_headers(self):
        fixture=self.directory/"header.c"
        fixture.write_text('#include <stdio.h>\nint f(char *s) { return snprintf(s,8,"%d",42); }\n')
        prefix=freestanding_compile_prefix(find_zig(),[ROOT/"userspace/text/include"])
        for language in ("c","c++"):
            self.command([*prefix,"-Werror","-x",language,"-c",fixture,"-o",self.directory/(language+".o")])

    def test_actual_i386_o0_o2(self):
        from build_user_text import extract,compile_text
        vendor=extract(self.directory/"upstream")
        for opt in ("-O0","-O2"):
            output=self.directory/opt; output.mkdir()
            objects=compile_text(find_zig(),vendor,output,self.env,host=True,opt=opt)
            exe=output/"text-host.exe"
            self.command([find_zig(),"cc","-target","x86-windows-gnu",opt,"-fno-builtin",
                "-mno-sse","-mno-sse2","-mno-mmx","-I",ROOT/"userspace/math/include",
                "-I",ROOT/"userspace/libc/include",ROOT/"test/text_host.c",*objects,"-o",exe])
            self.assertIn("TEXT_HOST_OK reference_samples=128",self.command([exe],30))

    def test_sdk_archive_and_incremental(self):
        from build_user_text import build_text
        root=self.directory/"sdk"; library=build_text(root,find_zig())
        self.assertTrue((root/"usr/include/reist/text/stdio.h").is_file())
        self.assertIn("-lreisttext -lm -lreistc",(root/"usr/lib/pkgconfig/reisttext.pc").read_text())
        self.assertTrue((root/"usr/share/licenses/musl-text/src/stdio/vfprintf.c").is_file())
        defined=set(re.findall(r"(?m)^\S+\s+[A-Za-z]\s+(\S+)$",self.command([
            "C:/msys64/mingw64/bin/nm.exe","--defined-only","--extern-only",library])))
        undefined=set(re.findall(r"(?m)^\s+U\s+(\S+)$",self.command([
            "C:/msys64/mingw64/bin/nm.exe","-u",library])))
        self.assertTrue({"snprintf","vsnprintf"}<=defined)
        self.assertFalse({"vfprintf","FILE","stdout","frexpl","wctomb","strerror"}&defined)
        self.assertEqual(undefined-defined,{"memcpy","memset","scalbn","reist_libc_errno","__udivdi3","__divdi3"})
        assembly=self.command(["C:/msys64/mingw64/bin/objdump.exe","-d",library])
        self.assertNotRegex(assembly,r"\b(?:xmm[0-9]+|mm[0-7]|cr[034])\b")
        stamp=library.stat().st_mtime_ns; digest=hashlib.sha256(library.read_bytes()).hexdigest()
        build_text(root,find_zig(),incremental=True)
        self.assertEqual(stamp,library.stat().st_mtime_ns)
        self.assertEqual(digest,hashlib.sha256(library.read_bytes()).hexdigest())
        # Link the actual guest consumer against the existing SDK. Its strong
        # byte functions must win over compiler-rt's arithmetic-only fallbacks.
        from build_user_program import build,resolve_sysroot_runtime
        sdk=ROOT/"build/sdk"; base=sdk/"usr/lib"
        includes,startup,runtime=resolve_sysroot_runtime(sdk,include_network_parsers=True)
        elf=self.directory/"texttest.elf"
        diagnostic=self.directory/"texttest-symbols.elf"
        import build_user_program as program_builder
        original_run=program_builder.run
        def retain_symbols(command,environment):
            original_run(command,environment)
            if "ld.lld" in command:
                inspection=[arg for arg in command if arg!="--strip-all"]
                inspection[inspection.index("-o")+1]=str(diagnostic)
                inspection.append("--Map="+str(self.directory/"texttest.map"))
                original_run(inspection,environment)
        with patch("build_user_program.run",side_effect=retain_symbols):
            build([ROOT/"userspace/programs/texttest.c"],self.directory/"TEXTTEST.PRG",find_zig(),
                elf_output=elf,include_dirs=[ROOT/"userspace/text/include",ROOT/"userspace/math/include",
                    ROOT/"userspace/libc/include",includes],
                libraries=[library,base/"libm.a",base/"libreistc.a",base/"libclang_rt.builtins-i386.a"],
                runtime_objects=[startup],runtime_libraries=runtime,cache_directory=self.directory/"caller-cache")
        self.assertEqual(program_builder.elf_to_mypr(diagnostic.read_bytes()),
                         (self.directory/"TEXTTEST.PRG").read_bytes())
        symbols=self.command(["C:/msys64/mingw64/bin/nm.exe","--defined-only",diagnostic])
        for name in ("memcpy","memset","reist_libc_errno"):
            self.assertTrue(re.search(r"(?m)^\S+ [Tt] "+name+r"$",symbols),name+" is not a strong text symbol")
        origins={}; current=""
        for line in (self.directory/"texttest.map").read_text().splitlines():
            if ".a(" in line: current=line
            if line.split() and line.split()[-1] in ("memcpy","memset","reist_libc_errno"):
                origins[line.split()[-1]]=current
        for name in ("memcpy","memset","reist_libc_errno"):
            self.assertIn("libreistc.a",origins.get(name,""))
        self.assertIn("__udivdi3",symbols)

    def test_pin_metadata_and_adapter_drift(self):
        from build_user_text import extract,adapt
        invalid=self.directory/"bad.tar.gz"; invalid.write_bytes(b"bad")
        target=self.directory/"denied"
        with self.assertRaisesRegex(ValueError,"SHA-256"): extract(target,invalid)
        self.assertFalse(target.exists())
        for kind,size in ((tarfile.SYMTYPE,1),(tarfile.REGTYPE,-1),(tarfile.REGTYPE,256*1024+1),(tarfile.REGTYPE,256*1024)):
            member=tarfile.TarInfo("bad"); member.type=kind; member.size=size
            archive=MagicMock(); archive.__enter__.return_value=archive; archive.getmember.return_value=member
            with patch("build_user_text.tarfile.open",return_value=archive):
                with self.assertRaises(ValueError): extract(target)
            self.assertFalse(target.exists()); archive.extractfile.assert_not_called()
        with self.assertRaises(ValueError): adapt("vfprintf.c","unexpected upstream text")

    def test_shell_layout_and_link_recipe(self):
        source=(ROOT/"scripts/build_system_programs.py").read_text()
        self.assertIn('"TEXTTEST.PRG": ROOT / "userspace/programs/texttest.c"',source)
        self.assertIn("'usr/bin/texttest.prg' = 'TEXTTEST.PRG'",(ROOT/"scripts/build-windows.ps1").read_text())
        self.assertIn("usr/bin/texttest.prg=$(SYSTEM_PROGRAM_DIR)/TEXTTEST.PRG",(ROOT/"Makefile").read_text())
        caller=source.split('if name == "TEXTTEST.PRG":',1)[1].split('if name ',1)[0]
        for required in ("sdk.text_library","sdk.math_library","sdk.libc_library","Path(__file__).resolve()"):
            self.assertIn(required,caller)
        self.assertLess(caller.index("sdk.libc_library"),caller.index("libclang_rt.builtins-i386.a"))
        shell=(ROOT/"userspace/bin/shell.c").read_text().split("static char search_paths[",1)[1].split("};",1)[0]
        self.assertIn('"/usr/bin"',shell)
        from build_user_program import resolve_sysroot_runtime
        import inspect
        self.assertNotIn("libreisttext",inspect.getsource(resolve_sysroot_runtime))

    def test_guest_validator(self):
        from run_qemu_text import validate_transcript
        lines=[]
        for run in range(2):
            lines += ["TEXT_VECTORS_OK","*** USER PROCESS PAGE FAULT ***","Faulting address: 0x00000004"]
            for i,(mode,status) in enumerate((("normal",37),("fault",142),("hold",143),("normal",37))):
                lines += [f"TEXT_REAP_OK mode=--{mode} status={status} pid=7 generation={4*run+i+1}"]
            lines += ["TEXT_PARENT_OK","TEXT_RUNTIME_OK"]
        transcript="\n".join(lines)+"\n"; validate_transcript(transcript)
        for old,new in (("generation=8","generation=1"),("status=142","status=94"),
                        ("TEXT_PARENT_OK",""),("0x00000004","0xBFFF6FFC"),
                        ("*** USER PROCESS PAGE FAULT ***","")):
            with self.assertRaises(ValueError): validate_transcript(transcript.replace(old,new))
        with self.assertRaises(ValueError): validate_transcript(transcript+"*** USER PROCESS PAGE FAULT ***\n")

if __name__=="__main__": unittest.main()
