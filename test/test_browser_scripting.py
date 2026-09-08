"""Executable browser bindings and private document-protocol regression proof."""
import os, subprocess, sys, unittest, uuid
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"scripts"))
from measure_cpp_baseline import suppress_windows_test_dialogs
from build_user_program import find_zig

class BrowserScriptingTests(unittest.TestCase):
    def test_guest_evidence_rejects_missing_and_stale_results(self):
        from run_qemu_browser_scripting import validate_transcript
        markers=['BROWSER_SCRIPT_DOM_OK','BROWSER_SCRIPT_REFLOW_OK','BROWSER_SCRIPT_NAVIGATION_OK',
            'JS_SERVICE_HANG_ENTERED','BROWSER_SCRIPT_HANG_CONTAINED_OK','JS_SERVICE_FAULT_ENTERED',
            'BROWSER_SCRIPT_FAULT_CONTAINED_OK','BROWSER_SCRIPT_RECOVERY_OK','HOST_BROWSER_SCRIPT_TITLE_PIXELS_OK','BROWSER_CLOSE_OK',
            'HOST_BROWSER_SCRIPT_RESTART_OK','HOST_BROWSER_SCRIPT_ATTRIBUTE_PIXELS_OK']
        text='\n'.join(markers)+ '\n*** USER PROCESS PAGE FAULT ***\nFaulting address: 0x00000004\n'
        text+='\n'.join(f'BROWSER_JS_WORKER pid={11+i} generation={i+1} fixture={mode}' for i,mode in enumerate((0,0,2,1,0)))
        validate_transcript(text)
        for marker in markers:
            with self.assertRaises(ValueError): validate_transcript(text.replace(marker,''))
        for suffix in ('\nBROWSER_PROBE_FAIL','\nKERNEL PANIC','\n*** USER PROCESS PAGE FAULT ***'):
            with self.assertRaises(ValueError): validate_transcript(text+suffix)
        with self.assertRaises(ValueError): validate_transcript(text.replace('pid=15 generation=5','pid=11 generation=1'))
    def test_owned_bridge_replay_and_cancellation(self):
        from measure_cpp_baseline import compile_mixed_host
        directory=ROOT/"build/codex-agent/r326-dom-attributes"/("owner-"+uuid.uuid4().hex)
        directory.mkdir(parents=True)
        sources=[ROOT/"test/browser_scripting_host.cpp",*[ROOT/p for p in (
            "userspace/gui/apps/browser/js_session.cpp", "userspace/gui/apps/browser/js_protocol.c",
            "userspace/gui/apps/browser/script_fetch.cpp", "userspace/gui/apps/browser/browser_response.cpp",
            "userspace/gui/apps/browser/browser_resources.cpp", "userspace/programs/curl_http.c",
            "userspace/gui/apps/browser/script_protocol.c", "userspace/gui/apps/browser/browser_scene.c",
            "userspace/gui/apps/browser/html_protocol.c", "userspace/gui/apps/browser/browser_forms.c",
            "userspace/gui/lib/font.c", "userspace/gui/lib/html_document.c")]]
        for opt in ("-O0","-O2"):
            exe=directory/(opt+".exe")
            compile_mixed_host(sources,exe,[opt,"-DBROWSER_OWNER_HOST","-Iuserspace/gui/apps/browser",
                "-Iuserspace/sdk/include","-Iuserspace/gui/include","-ffunction-sections","-fdata-sections"])
            p=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
            self.assertEqual(p.returncode,0,p.stdout+p.stderr)
            self.assertIn("BROWSER_OWNER_REPLAY_CANCEL_OK",p.stdout)
    def test_actual_quickjs_binding(self):
        self.assertTrue((ROOT/"assets/browser/dom.js").is_file(),"browser DOM binding missing")
        import build_user_js as js, build_user_math as math, build_user_text as text
        suppress_windows_test_dialogs()
        directory=ROOT/"build/codex-agent/r326-dom-attributes"/("host-"+uuid.uuid4().hex)
        _,generated=js.extract(directory)
        mv=math.extract(directory/"math-upstream"); tv=text.extract(directory/"text-upstream")
        env=os.environ.copy(); env["ZIG_GLOBAL_CACHE_DIR"]=str(ROOT/"build/codex-agent/r323-js/host-cache")
        env["ZIG_LOCAL_CACHE_DIR"]=str(directory/"cache")
        for opt in ("-O0","-O2"):
            out=directory/opt; out.mkdir(); mo=out/"math"; mo.mkdir(); to=out/"text"; to.mkdir()
            objects=js.compile_core(find_zig(),generated,out,env,host=True,opt=opt)
            objects+=math.compile_math(find_zig(),mv,mo,env,host=True,opt=opt)
            objects+=text.compile_text(find_zig(),tv,to,env,host=True,opt=opt)
            exe=out/"browser-binding.exe"
            command=[find_zig(),"c++","-target","x86-windows-gnu",opt,"-fno-sanitize=all",
                "-std=c++20","-fno-exceptions","-fno-rtti","-I",js.JS/"include",
                ROOT/"test/browser_scripting_host.cpp",*objects,"-o",exe]
            for args,timeout in ((command,90),([exe,ROOT/"assets/browser/dom.js"],30)):
                p=subprocess.run(list(map(str,args)),env=env,capture_output=True,text=True,timeout=timeout,
                    creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
                self.assertEqual(p.returncode,0,p.stdout+p.stderr)
            self.assertIn("BROWSER_BINDING_OK",p.stdout)

if __name__=="__main__": unittest.main()
