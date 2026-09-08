"""Actual C++ external-script transport/response with OS-boundary mocks."""
import subprocess,sys,unittest,uuid
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from measure_cpp_baseline import compile_mixed_host,suppress_windows_test_dialogs

class ExternalScriptsTests(unittest.TestCase):
    def test_guest_evidence_requires_cache_and_exact_reap(self):
        from run_qemu_browser_external import validate_transcript
        markers=['BROWSER_EXTERNAL_INITIAL_OK executions=5','BROWSER_EXTERNAL_REFLOW_OK executions=5',
            'BROWSER_EXTERNAL_RELOAD_OK executions=10','BROWSER_EXTERNAL_CANCEL_SENT pid=29',
            'BROWSER_EXTERNAL_CANCEL_OK executions=12','BROWSER_EXTERNAL_RECOVERY_OK executions=17',
            'HOST_EXTERNAL_SOURCE_CACHE_REPLAY_OK','HOST_EXTERNAL_PIXELS_OK','BROWSER_CLOSE_OK',
            'DESKTOP_EXIT_OK','HOST_EXTERNAL_SHELL_OK']
        text='\n'.join(markers)+'\n'
        for i in range(14):
            text+=f'BROWSER_SCRIPT_FETCH_WORKER pid={20+i} generation={i+1}\n'
            text+=f'BROWSER_SCRIPT_FETCH_REAP pid={20+i} generation={i+1} status={143 if i==9 else 0}\n'
        validate_transcript(text)
        for marker in markers:
            with self.assertRaises(ValueError):validate_transcript(text.replace(marker,''))
        for old,new in [('status=143','status=0'),('pid=33 generation=14','pid=20 generation=1'),
                        ('BROWSER_SCRIPT_FETCH_REAP pid=20','lost pid=20')]:
            with self.assertRaises(ValueError):validate_transcript(text.replace(old,new))
        with self.assertRaises(ValueError):validate_transcript(text+'\nKERNEL PANIC')

    def test_actual_loader_and_response(self):
        suppress_windows_test_dialogs()
        folder=ROOT/'build/codex-agent/r327-external-js'/('host-'+uuid.uuid4().hex)
        folder.mkdir(parents=True)
        sources=['test/script_fetch_host.cpp','userspace/gui/apps/browser/script_fetch.cpp',
            'userspace/gui/apps/browser/browser_response.cpp','userspace/gui/apps/browser/browser_resources.cpp',
            'userspace/gui/apps/browser/html_protocol.c','userspace/gui/apps/browser/script_protocol.c',
            'userspace/programs/curl_http.c','userspace/gui/lib/html_document.c']
        for opt in ('-O0','-O2'):
            exe=folder/(opt+'.exe')
            compile_mixed_host([ROOT/p for p in sources],exe,[opt,'-Iuserspace/gui/apps/browser',
                '-Iuserspace/sdk/include','-Iuserspace/gui/include','-ffunction-sections','-fdata-sections'])
            p=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
            self.assertEqual(p.returncode,0,p.stdout+p.stderr)
            self.assertIn('SCRIPT_FETCH_OK',p.stdout)

if __name__=='__main__': unittest.main()
