"""Real i386 QuickJS core and bounded embedding, not a source-only port claim."""
import os
import hashlib
import re
import tarfile
from pathlib import Path
import subprocess
import sys
import unittest
import uuid
from unittest.mock import MagicMock,patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"scripts"))
from measure_cpp_baseline import suppress_windows_test_dialogs
from build_user_program import find_zig,freestanding_compile_prefix

class JavaScriptTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        suppress_windows_test_dialogs()
        os.environ["ZIG_GLOBAL_CACHE_DIR"]=str(ROOT/"build/codex-agent/r323-js/host-cache")
        os.environ["ZIG_LOCAL_CACHE_DIR"]=str(ROOT/"build/codex-agent/r323-js/test-local-cache")

    def command(self,command,env,timeout=90):
        result=subprocess.run(list(map(str,command)),env=env,capture_output=True,text=True,
            timeout=timeout,creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        return result.stdout

    def test_actual_i386_engine_o0_o2(self):
        suppress_windows_test_dialogs()
        import build_user_js as js
        import build_user_math as math
        import build_user_text as text
        directory=ROOT/"build/codex-agent/r323-js"/("host-"+uuid.uuid4().hex)
        _,generated=js.extract(directory)
        math_vendor=math.extract(directory/"math-upstream")
        text_vendor=text.extract(directory/"text-upstream")
        env=os.environ.copy(); env["ZIG_GLOBAL_CACHE_DIR"]=str(ROOT/"build/codex-agent/r323-js/host-cache")
        for opt in ("-O2","-O0"):
            output=directory/opt; output.mkdir()
            math_out=output/"math"; math_out.mkdir()
            text_out=output/"text"; text_out.mkdir()
            math_objects=math.compile_math(find_zig(),math_vendor,math_out,env,host=True,opt=opt)
            text_objects=text.compile_text(find_zig(),text_vendor,text_out,env,host=True,opt=opt)
            objects=js.compile_core(find_zig(),generated,output,env,host=True,opt=opt)
            exe=output/"js-host.exe"
            defines=["-D"+n+"=reist_math_"+n for n in math.FUNCTIONS+math.INTEGER_FUNCTIONS+
                     ("fegetround","fesetround","feclearexcept","fetestexcept")]
            self.command([find_zig(),"cc","-target","x86-windows-gnu",opt,"-fno-builtin",
                "-mno-sse","-mno-sse2","-mno-mmx","-I",js.JS/"include",
                "-I",ROOT/"userspace/math/include",*defines,ROOT/"test/js_host.c",
                *objects,*math_objects,*text_objects,"-o",exe],env)
            output_text=self.command([exe],env,30)
            self.assertIn("JS_HOST_OK",output_text); print(opt,output_text.strip())
            # Compile the actual facade in this fixture translation unit to
            # exercise its private allocator directly, without test exports or
            # a second implementation. Keep exactly the same core objects.
            fixture=output/"allocator.c"
            fixture.write_text('''#include <stdlib.h>
#include <string.h>
static int fail_backing;
static void *backing_malloc(size_t n) { return fail_backing?NULL:malloc(n); }
static void *backing_realloc(void *p,size_t n) { return fail_backing?NULL:realloc(p,n); }
#define malloc backing_malloc
#define realloc backing_realloc
#include "'''+(js.JS/"lib/engine.c").as_posix()+'''"
#undef malloc
#undef realloc
int *reist_libc_errno(void) { static int e; return &e; }
#define CHECK(x) do { if(!(x)) return __LINE__; } while(0)
static int fake_clock(void *p,uint64_t *now) { *now=*(uint64_t *)p; return 0; }
int main(void) {
    reist_js_engine owner={0}; owner.config.memory_limit=1024;
    JSMallocState state={0}; state.opaque=&owner;
    CHECK(!allocate(&state,0)); CHECK(!owner.used && !owner.count);
    unsigned char *p=allocate(&state,64); CHECK(p && usable(p)==64);
    memset(p,0x5a,64); size_t used=owner.used;
    CHECK(!resize(&state,p,SIZE_MAX)); CHECK(owner.used==used && owner.count==1);
    for(unsigned i=0;i<64;i++) CHECK(p[i]==0x5a);
    owner.oom=0; fail_backing=1;
    CHECK(!resize(&state,p,128)); CHECK(owner.used==used && usable(p)==64);
    CHECK(!allocate(&state,8)); CHECK(owner.used==used && owner.count==1);
    fail_backing=0; owner.oom=0;
    p=resize(&state,p,128); CHECK(p && usable(p)==128 && owner.used==used+64);
    for(unsigned i=0;i<64;i++) CHECK(p[i]==0x5a);
    p=resize(&state,p,32); CHECK(p && usable(p)==32 && owner.used==used-32);
    CHECK(!resize(&state,p,0)); release(&state,NULL);
    CHECK(!owner.used && !owner.count && !state.malloc_count && !state.malloc_size);
    CHECK(!allocate(&state,1024)); CHECK(owner.oom && !owner.used);
    uint64_t now=100; reist_js_status status;
    reist_js_config config={REIST_JS_VERSION,sizeof(config),1024*1024,16384,1024,1024,32,0,1,&now,fake_clock};
    reist_js_config invalid=config;
    for(unsigned field=0;field<12;field++) {
        invalid=config;
        switch(field) {
        case 0: invalid.version++; break; case 1: invalid.struct_size--; break;
        case 2: invalid.memory_limit--; break; case 3: invalid.memory_limit=129U*1024*1024; break;
        case 4: invalid.stack_limit=4095; break; case 5: invalid.stack_limit=16385; break;
        case 6: invalid.source_limit=REIST_JS_SOURCE_MAX+1; break;
        case 7: invalid.result_limit=REIST_JS_RESULT_MAX+1; break;
        case 8: invalid.job_limit=1025; break; case 9: invalid.reserved=1; break;
        case 10: invalid.seed=0; break; case 11: invalid.monotonic_ms=NULL; break;
        }
        CHECK(!reist_js_create(&invalid,&status) && status==REIST_JS_INVALID);
    }
    CHECK(!fesetround(FE_TONEAREST));
    reist_js_engine *engine=reist_js_create(&config,&status); CHECK(engine && !status);
    char output[16]; size_t needed;
    CHECK(reist_js_eval(engine,"40+2",4,101,output,sizeof(output),&needed)==REIST_JS_OK);
    CHECK(!strcmp(output,"42")); now=99;
    CHECK(reist_js_eval(engine,"1",1,101,output,sizeof(output),&needed)==REIST_JS_DEADLINE);
    CHECK(!output[0]); now=100;
    CHECK(reist_js_collect(engine,101)==REIST_JS_CLOSED);
    reist_js_destroy(&engine); CHECK(!engine); reist_js_destroy(&engine);
    return 0;
}
''')
            prefix=freestanding_compile_prefix(find_zig(),[js.JS/"include",js.JS/"private",
                ROOT/"userspace/math/include",ROOT/"userspace/libc/include",generated],include_repository_sdk=False)
            prefix[prefix.index("x86-freestanding")]="x86-windows-gnu"
            obj=output/"allocator.obj"; allocator_exe=output/"allocator.exe"
            self.command([*prefix,opt,"-fno-sanitize=all",*defines,"-c",fixture,"-o",obj],env)
            self.command([find_zig(),"cc","-target","x86-windows-gnu",obj,*objects[:-1],
                          *math_objects,*text_objects,"-o",allocator_exe],env)
            self.command([allocator_exe],env,30)

    def test_pinned_core_excludes_ambient_authority(self):
        import build_user_js as js
        directory=ROOT/"build/codex-agent/r323-js"/("extract-"+uuid.uuid4().hex)
        original,adapted=js.extract(directory)
        source=(adapted/"quickjs.c").read_text()
        self.assertIn("#define CONFIG_STACK_CHECK",source)
        self.assertNotIn("#define CONFIG_ATOMICS",source)
        self.assertNotIn("static int getTimezoneOffset(",source)
        self.assertNotIn("struct timeval tv;",source)
        self.assertNotIn("void JS_DumpMemoryUsage(FILE",source)
        self.assertIn("JSRuntime *JS_NewRuntime2(",source)
        self.assertIn("static int getTimezoneOffset(",(original/"quickjs.c").read_text())
        self.assertTrue((original/"LICENSE").is_file())

    def test_pins_metadata_and_drift(self):
        import build_user_js as js
        directory=ROOT/"build/codex-agent/r323-js"/("invalid-"+uuid.uuid4().hex)
        directory.mkdir(parents=True)
        invalid=directory/"bad.tar.xz"; invalid.write_bytes(b"bad")
        target=directory/"denied"
        with self.assertRaisesRegex(ValueError,"archive pin"): js.extract(target,invalid)
        self.assertFalse(target.exists())
        for kind,size in ((tarfile.SYMTYPE,1),(tarfile.REGTYPE,-1),(tarfile.REGTYPE,3*1024*1024+1),
                          (tarfile.REGTYPE,3*1024*1024)):
            member=tarfile.TarInfo("bad"); member.type=kind; member.size=size
            archive=MagicMock(); archive.__enter__.return_value=archive; archive.getmember.return_value=member
            with patch("build_user_js.tarfile.open",return_value=archive):
                with self.assertRaises(ValueError): js.extract(target)
            self.assertFalse(target.exists()); archive.extractfile.assert_not_called()
        for name in ("quickjs.c","quickjs.h","dtoa.c","libregexp.c","libunicode.c"):
            with self.assertRaises(ValueError): js.adapt(name,"unexpected upstream text")
        with patch("build_user_js.MEMBERS",("../escape",)):
            with self.assertRaises(ValueError): js.extract(target)
        self.assertFalse(target.exists())

    def test_sdk_archive_incremental_and_real_target_link(self):
        import build_user_js as js
        import build_user_math as math
        import build_user_text as text
        import build_user_program as program
        directory=ROOT/"build/codex-agent/r323-js"/("sdk-"+uuid.uuid4().hex)
        env=os.environ.copy(); sdk=directory/"sdk"; zig=find_zig()
        library=js.build_js(sdk,zig)
        math_library=math.build_math(sdk,zig)
        text_library=text.build_text(sdk,zig)
        self.assertTrue((sdk/"usr/include/reist/js/reist_js.h").is_file())
        self.assertFalse((sdk/"usr/include/quickjs.h").exists())
        self.assertIn("-lreistjs -lreisttext -lm -lreistc",(sdk/"usr/lib/pkgconfig/reistjs.pc").read_text())
        for name in js.MEMBERS: self.assertTrue((sdk/"usr/share/licenses/quickjs"/name).is_file())
        self.assertTrue((sdk/"usr/share/licenses/musl-math/src/math/i386/lrint.c").is_file())
        nm="C:/msys64/mingw64/bin/nm.exe"
        defined=set(re.findall(r"(?m)^\S+\s+[A-Za-z]\s+(\S+)$",self.command([nm,"--defined-only","--extern-only",library],env)))
        undefined=set(re.findall(r"(?m)^\s+U\s+(\S+)$",self.command([nm,"-u",library],env)))
        allowed=set(math.FUNCTIONS+math.INTEGER_FUNCTIONS+(
            "fegetround","memcpy","memmove","memset","memcmp","memchr","malloc","calloc","realloc","free",
            "abort","abs","strlen","strcmp","strncmp","strchr","strrchr","strcpy","snprintf","vsnprintf",
            "__divdi3","__moddi3","__udivdi3","__umoddi3"))
        self.assertFalse((undefined-defined)-allowed,sorted((undefined-defined)-allowed))
        self.assertTrue({"reist_js_create","reist_js_eval","reist_js_destroy","reist_js_collect","reist_js_get_stats"}<=defined)
        self.assertFalse({"JS_NewRuntime","JS_NewContext","JS_AddIntrinsicDate","JS_DumpMemoryUsage"}&defined)
        stamp=library.stat().st_mtime_ns; digest=hashlib.sha256(library.read_bytes()).hexdigest()
        with patch("build_user_js.compile_core",side_effect=AssertionError("unnecessary incremental compile")):
            js.build_js(sdk,zig,incremental=True)
        self.assertEqual(stamp,library.stat().st_mtime_ns)
        self.assertEqual(digest,hashlib.sha256(library.read_bytes()).hexdigest())
        # Missing retained source may not count as a complete installation.
        actual_is_file=Path.is_file
        missing=sdk/"usr/share/licenses/quickjs/quickjs.c"
        with patch.object(Path,"is_file",lambda path: False if path==missing else actual_is_file(path)):
            with patch("build_user_js.compile_core",side_effect=RuntimeError("required repair")):
                with self.assertRaisesRegex(RuntimeError,"required repair"): js.build_js(sdk,zig,incremental=True)
        import inspect
        self.assertNotIn("libreistjs",inspect.getsource(program.resolve_sysroot_runtime))
        base=ROOT/"build/sdk/usr/lib"
        includes,startup,runtime=program.resolve_sysroot_runtime(ROOT/"build/sdk",include_network_parsers=True)
        diagnostic=directory/"jstest-symbols.elf"; original_run=program.run
        def retain_symbols(command,environment):
            original_run(command,environment)
            if "ld.lld" in command:
                inspection=[arg for arg in command if arg!="--strip-all"]
                inspection[inspection.index("-o")+1]=str(diagnostic)
                inspection.append("--Map="+str(directory/"jstest.map"))
                original_run(inspection,environment)
        with patch("build_user_program.run",side_effect=retain_symbols):
            program.build([ROOT/"userspace/programs/jstest.c"],directory/"JSTEST.PRG",zig,
                elf_output=directory/"jstest.elf",include_dirs=[js.JS/"include",ROOT/"userspace/text/include",
                    ROOT/"userspace/math/include",ROOT/"userspace/libc/include",includes],
                libraries=[library,text_library,math_library,base/"libreistc.a",base/"libclang_rt.builtins-i386.a"],
                runtime_objects=[startup],runtime_libraries=runtime,cache_directory=directory/"caller-cache")
        self.assertEqual(program.elf_to_mypr(diagnostic.read_bytes()),(directory/"JSTEST.PRG").read_bytes())
        symbols=self.command([nm,"--defined-only",diagnostic],env)
        for name in ("memcpy","memset","reist_libc_errno","lrint"):
            self.assertRegex(symbols,r"(?m)^\S+ [Tt] "+name+r"$")
        origins={}; current=""
        for line in (directory/"jstest.map").read_text().splitlines():
            if ".a(" in line: current=line
            if line.split() and line.split()[-1] in ("memcpy","memset","reist_libc_errno","lrint"):
                origins[line.split()[-1]]=current
        for name in ("memcpy","memset","reist_libc_errno"): self.assertIn("libreistc.a",origins.get(name,""))
        self.assertIn("libm.a",origins.get("lrint",""))

    def test_public_headers_and_integer_formats(self):
        import build_user_js as js
        directory=ROOT/"build/codex-agent/r323-js"/("headers-"+uuid.uuid4().hex)
        directory.mkdir(parents=True); fixture=directory/"public.c"
        # Real target types + printf/scanf compiler format checking for every
        # ISO integer family, not just string existence or guessed LP64 types.
        code=['#include <inttypes.h>','#include <reist_js.h>','#include <reist_js_script.h>','#include <reist_js_files.h>',
              'int print(const char *,...) __attribute__((format(printf,1,2)));',
              'int scan(const char *,...) __attribute__((format(scanf,1,2)));',
              'int f(reist_js_engine **p) { reist_js_destroy(p); return 0; }',
              'void formats(void) {']
        for family in ("8","16","32","64","LEAST8","LEAST16","LEAST32","LEAST64",
                       "FAST8","FAST16","FAST32","FAST64","MAX","PTR"):
            integer=("int"+family.lower()+"_t" if family in ("MAX","PTR") else
                     "int"+("_" if not family.isdigit() else "")+family.lower()+"_t")
            for fmt in "diouxX":
                typed=("u" if fmt in "ouxX" else "")+integer
                name=family+fmt
                code += [f'{typed} v{name}=0; print("%" PRI{fmt}{family},v{name});']
                if fmt!="X": code += [f'scan("%" SCN{fmt}{family},&v{name});']
        code += ['}']; fixture.write_text("\n".join(code))
        prefix=freestanding_compile_prefix(find_zig(),[js.JS/"include",ROOT/"userspace/libc/include"])
        for language in ("c","c++"):
            self.command([*prefix,"-Werror","-x",language,"-c",fixture,"-o",directory/(language+".o")],os.environ.copy())

    def test_shell_layout_and_guest_validator(self):
        from run_qemu_js import validate_transcript
        source=(ROOT/"scripts/build_system_programs.py").read_text()
        self.assertIn('"JSTEST.PRG": ROOT / "userspace/programs/jstest.c"',source)
        self.assertIn("'usr/bin/jstest.prg' = 'JSTEST.PRG'",(ROOT/"scripts/build-windows.ps1").read_text())
        self.assertIn("usr/bin/jstest.prg=$(SYSTEM_PROGRAM_DIR)/JSTEST.PRG",(ROOT/"Makefile").read_text())
        caller=source.split('if name == "JSTEST.PRG":',1)[1].split('if name ',1)[0]
        for required in ("sdk.js_library","sdk.text_library","sdk.math_library","sdk.libc_library","Path(__file__).resolve()"):
            self.assertIn(required,caller)
        self.assertLess(caller.index("sdk.libc_library"),caller.index("libclang_rt.builtins-i386.a"))
        shell=(ROOT/"userspace/bin/shell.c").read_text().split("static char search_paths[",1)[1].split("};",1)[0]
        self.assertIn('"/usr/bin"',shell)
        lines=[]
        for run in range(2):
            lines += ["JS_VECTORS_OK","*** USER PROCESS PAGE FAULT ***","Faulting address: 0x00000004",
                      "JS_NONCOOPERATIVE_ENTERED"]
            for i,(mode,status) in enumerate((("normal",37),("fault",142),("hang",143),("normal",37))):
                lines += [f"JS_REAP_OK mode=--{mode} status={status} pid=7 generation={4*run+i+1}"]
            lines += ["JS_PARENT_OK","JS_RUNTIME_OK"]
        transcript="\n".join(lines)+"\n"; validate_transcript(transcript)
        for old,new in (("generation=8","generation=1"),("status=142","status=94"),
                        ("JS_PARENT_OK",""),("0x00000004","0xBFFF6FFC"),
                        ("JS_NONCOOPERATIVE_ENTERED",""),("*** USER PROCESS PAGE FAULT ***","")):
            with self.assertRaises(ValueError): validate_transcript(transcript.replace(old,new))
        with self.assertRaises(ValueError): validate_transcript(transcript+"*** USER PROCESS PAGE FAULT ***\n")

if __name__=="__main__": unittest.main()
