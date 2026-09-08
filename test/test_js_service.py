"""Real i386 framing/owner behavior and the separately executed guest worker."""
from pathlib import Path
import os,subprocess,sys,unittest,uuid
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"scripts"))
from build_user_program import find_zig
from measure_cpp_baseline import suppress_windows_test_dialogs

class ServiceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        suppress_windows_test_dialogs()
        cls.directory=ROOT/"build/codex-agent/r324-js-service"/("host-"+uuid.uuid4().hex)
        cls.directory.mkdir(parents=True)
        cls.env=os.environ.copy()
        cls.env["ZIG_GLOBAL_CACHE_DIR"]=str(ROOT/"build/codex-agent/r324-js-service/cache")
        cls.env["ZIG_LOCAL_CACHE_DIR"]=str(cls.directory/"cache")
    def run_command(self,command,timeout=90):
        p=subprocess.run(list(map(str,command)),env=self.env,capture_output=True,text=True,
            timeout=timeout,creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
        self.assertEqual(p.returncode,0,p.stdout+p.stderr)
        return p.stdout
    def test_actual_i386_protocol_and_session_o0_o2(self):
        for opt in ("-O0","-O2"):
            exe=self.directory/(opt+".exe")
            self.run_command([find_zig(),"c++","-target","x86-windows-gnu",opt,"-std=c++20",
                "-fno-sanitize=all","-fno-exceptions","-fno-rtti","-I",ROOT/"userspace/sdk/include",
                "-I",ROOT/"userspace/gui/apps/browser",ROOT/"test/js_service_host.cpp",
                ROOT/"userspace/gui/apps/browser/js_session.cpp",
                "-x","c++",ROOT/"userspace/gui/apps/browser/js_protocol.c","-o",exe])
            self.assertIn("JS_SERVICE_HOST_OK",self.run_command([exe],30))
    def test_real_target_links_and_layout(self):
        import build_system_programs as programs
        from build_user_sdk import sdk_artifacts
        from build_user_program import build
        sdk=sdk_artifacts(ROOT/"build/sdk")
        for name in ("JSWORK.PRG","JSIPCTST.PRG","JS.PRG","JSRUNTST.PRG"):
            libraries=[sdk.js_library,sdk.text_library,sdk.math_library] if name=="JSWORK.PRG" else [sdk.cpp_library]
            build(list(programs.PROGRAMS[name]),self.directory/name,find_zig(),
                include_dirs=[ROOT/('userspace/js' if name in ('JS.PRG','JSRUNTST.PRG') else 'userspace/gui/apps/browser'),
                    ROOT/'userspace/storage/include',sdk.js_include_dir,sdk.math_include_dir,
                    sdk.text_include_dir,sdk.cpp_include_dir,sdk.libc_include_dir,sdk.include_dir],
                libraries=[*libraries,sdk.libc_library,sdk.library_dir/"libclang_rt.builtins-i386.a"],
                runtime_objects=[sdk.startup_object],runtime_libraries=[sdk.core_library],
                cpp=name!="JSWORK.PRG",cache_directory=self.directory/"link-cache")
            self.assertGreater((self.directory/name).stat().st_size,0)
            path="usr/bin/"+name.lower()
            self.assertIn(f"'{path}' = '{name}'",(ROOT/"scripts/build-windows.ps1").read_text())
            self.assertIn(path+"=$(SYSTEM_PROGRAM_DIR)/"+name,(ROOT/"Makefile").read_text())
        shell=(ROOT/"userspace/bin/shell.c").read_text().split("static char search_paths[",1)[1].split("};",1)[0]
        self.assertIn('"/usr/bin"',shell)
    def test_guest_validator_rejects_missing_and_stale_proof(self):
        from run_qemu_js_service import validate_transcript
        lines=[]
        for run in range(2):
            lines += ["JS_SERVICE_RUNTIME_OK","JS_SERVICE_ORPHAN_OK","JS_SERVICE_FAULT_ENTERED",
                "JS_SERVICE_HANG_ENTERED","JS_SERVICE_STALE_ENTERED","*** USER PROCESS PAGE FAULT ***",
                "Faulting address: 0x00000004"]
            for i,(mode,status) in enumerate((("normal",0),("fault",142),("hang",143),("stale",74),("cancel",143),("fresh",0))):
                lines += [f"JS_SERVICE_REAP mode={mode} status={status} pid=9 generation={6*run+i+1}"]
        text="\n".join(lines)+"\n"; validate_transcript(text)
        for old,new in (("generation=12","generation=1"),("status=142","status=0"),
                        ("JS_SERVICE_ORPHAN_OK",""),("0x00000004","0x00000008"),("JS_SERVICE_HANG_ENTERED","")):
            with self.assertRaises(ValueError): validate_transcript(text.replace(old,new))
        with self.assertRaises(ValueError): validate_transcript(text+"JS_SERVICE_TEST_FAIL\n")
        restricted=text
        for marker in ("JS_SERVICE_FAULT_ENTERED","JS_SERVICE_HANG_ENTERED","JS_SERVICE_STALE_ENTERED"):
            restricted=restricted.replace(marker+"\n","")
        restricted=restricted.replace("JS_SERVICE_RUNTIME_OK", "JS_SERVICE_DOMAIN_OK\nJS_SERVICE_HANG_CONFIRMED\nJS_SERVICE_RUNTIME_OK")
        validate_transcript(restricted,True)
        for old,new in (("JS_SERVICE_DOMAIN_OK",""),("JS_SERVICE_HANG_CONFIRMED",""),("status=142","status=0"),
                        ("generation=12","generation=1"),("JS_SERVICE_ORPHAN_OK",""),
                        ("Faulting address: 0x00000004","")):
            with self.assertRaises(ValueError): validate_transcript(restricted.replace(old,new),True)
        for marker in ("JS_SERVICE_FAULT_ENTERED","JS_SERVICE_HANG_ENTERED","JS_SERVICE_STALE_ENTERED"):
            with self.assertRaises(ValueError): validate_transcript(restricted+marker+"\n",True)
if __name__=="__main__": unittest.main()
