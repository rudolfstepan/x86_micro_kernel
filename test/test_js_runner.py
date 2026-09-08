"""JS2 actual i386 runner/admission behavior and normal shell packaging."""
from pathlib import Path
import os, subprocess, sys, unittest, uuid
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from build_user_program import find_zig
from measure_cpp_baseline import suppress_windows_test_dialogs

class RunnerTests(unittest.TestCase):
    def test_common_runner_and_shell_layout(self):
        self.assertTrue((ROOT/'userspace/js/runner.cpp').is_file())
        windows=(ROOT/'scripts/build-windows.ps1').read_text()
        make=(ROOT/'Makefile').read_text()
        for name in ('JS','JSRUNTST'):
            path='usr/bin/'+name.lower()+'.prg'
            self.assertIn("'"+path+"' = '"+name+".PRG'",windows)
            self.assertIn(path+'=$(SYSTEM_PROGRAM_DIR)/'+name+'.PRG',make)
        shell=(ROOT/'userspace/bin/shell.c').read_text()
        self.assertIn('"/usr/bin"',shell.split('static char search_paths[',1)[1].split('};',1)[0])
        self.assertIn('x86os_spawnv(executable, argc, child_argv)',shell)

    def test_i386_actual_runner_o0_o2(self):
        suppress_windows_test_dialogs()
        directory=ROOT/'build/codex-agent/r335-js-runner'/('host-'+uuid.uuid4().hex)
        directory.mkdir(parents=True)
        env=os.environ.copy()
        env['ZIG_GLOBAL_CACHE_DIR']=str(ROOT/'build/zig-global-cache')
        env['ZIG_LOCAL_CACHE_DIR']=str(directory/'cache')
        for opt in ('-O0','-O2'):
            exe=directory/(opt+'.exe')
            command=[find_zig(),'c++','-target','x86-windows-gnu',opt,'-std=c++20',
                '-fno-sanitize=all','-fno-exceptions','-fno-rtti',
                '-I',ROOT/'userspace/js','-I',ROOT/'userspace/sdk/include',
                '-I',ROOT/'userspace/storage/include',ROOT/'test/js_runner_host.cpp',
                ROOT/'userspace/js/runner.cpp',ROOT/'userspace/js/js_session.cpp',ROOT/'userspace/js/file_broker.cpp',
                '-x','c++',ROOT/'userspace/js/file_protocol.c',ROOT/'userspace/js/script_protocol.c',ROOT/'userspace/js/js_protocol.c','-o',exe]
            for cmd,timeout in ((command,90),([exe],30)):
                result=subprocess.run(list(map(str,cmd)),env=env,capture_output=True,text=True,timeout=timeout,
                    creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('JS_RUNNER_HOST_OK',result.stdout)

    def test_guest_validator_rejects_incomplete_or_stale_proof(self):
        from run_qemu_js_runner import validate_transcript
        lines=[]
        for run in range(2):
            lines += ['JS_RUNNER_'+m for m in ('RUNTIME_OK','REALMS_OK','CANCEL_OK','SOURCE_OK',
                'STDOUT_OK','STDERR_OK','ARGV_OK')]
            lines += [f'JS_RUNNER_CASE index={i} status={s}' for i,s in enumerate((7,1,1,71,0,0))]
            lines += [f'JS_RUNNER_REAP mode={mode} pid=9 generation={4*run+i+1} status=0'
                for i,mode in enumerate(('script','browser','fresh'))]
            lines += [f'JS_RUNNER_TIMEOUT_OK pid=9 generation={4*run+4}',
                'Arguments: shell 42','Arguments: guest 42']
        text='\n'.join(lines)+'\n'; validate_transcript(text)
        for old,new in (('JS_RUNNER_REALMS_OK',''),('generation=8','generation=4'),
                        ('status=71','status=0'),('Arguments: shell 42',''),
                        ('JS_RUNNER_CANCEL_OK',''),('mode=fresh','mode=script')):
            with self.assertRaises(ValueError): validate_transcript(text.replace(old,new))
        with self.assertRaises(ValueError): validate_transcript(text+'JS_RUNNER_TEST_FAIL\n')
        file_lines=[]
        for run in range(2):
            file_lines += [f'JS_FILES_CASE index={i} status={s}' for i,s in enumerate((0,1,71,124,0))]
            file_lines += ['JS_FILES_CANCEL_REUSE_OK','JS_FILE_SHELL_OK 230 64']
        complete=text+'\n'.join(file_lines)+'\n';validate_transcript(complete,True)
        for old,new in (('JS_FILES_CASE index=3 status=124','JS_FILES_CASE index=3 status=0'),
                        ('JS_FILES_CANCEL_REUSE_OK',''),('JS_FILE_SHELL_OK','MISSING')):
            with self.assertRaises(ValueError):validate_transcript(complete.replace(old,new),True)

    def test_external_browser_interleaved_trace_remains_strict(self):
        from run_qemu_browser_external import validate_transcript
        markers=['BROWSER_EXTERNAL_INITIAL_OK executions=5','BROWSER_EXTERNAL_REFLOW_OK executions=5',
            'BROWSER_EXTERNAL_RELOAD_OK executions=10','BROWSER_EXTERNAL_CANCEL_SENT pid=19',
            'BROWSER_EXTERNAL_CANCEL_OK executions=12','BROWSER_EXTERNAL_RECOVERY_OK executions=17',
            'HOST_EXTERNAL_SOURCE_CACHE_REPLAY_OK','HOST_EXTERNAL_PIXELS_OK','BROWSER_CLOSE_OK',
            'DESKTOP_EXIT_OK','HOST_EXTERNAL_SHELL_OK']
        rows=[]
        for i in range(14):
            rows += [f'BROWSER_SCRIPT_FETCH_WORKER pid={10+i} generation={i+1}',
                f'BROWSER_SCRIPT_FETCH_REAP pid={10+i} generation={i+1} status={143 if i==9 else 0}']
        raw='\n'.join(markers+rows)+'\n'; validate_transcript(raw)
        noise='REIST_NETWORK ARP_RESOLUTION_QUEUED\nREIST_NETWORK ARP_RESOLUTION_MEDIATED\n'
        broken=raw.replace('WORKER pid=20','WORK'+noise+'ER pid=20')
        saved=broken; validate_transcript(broken); self.assertEqual(saved,broken)
        # Every literal/numeric position can be interrupted; never invent a byte.
        record='BROWSER_SCRIPT_FETCH_WORKER pid=20 generation=11'
        for at in range(1,len(record)):
            validate_transcript(raw.replace(record,record[:at]+noise+record[at:]))
        for old,new in (('REAP pid=20 generation=11','REAP pid=20 generation=10'),
                        ('BROWSER_SCRIPT_FETCH_REAP pid=20','MISSING pid=20'),
                        ('status=143','status=0'),('HOST_EXTERNAL_PIXELS_OK','')):
            with self.assertRaises(ValueError): validate_transcript(broken.replace(old,new))
        with self.assertRaises(ValueError): validate_transcript(broken+rows[0]+'\n')
        for bad_noise in ('REIST_NETWORK ARP_RESOLUTION_QUEUED',
                          'REIST_NETWORK UNKNOWN\n','REIST_NETWORK ARP_RESOLUTION_QUEUED junk\n',
                          'REIST_NETWORK ARP_RESOLUTION_QUEUED\n'*129):
            with self.assertRaises(ValueError): validate_transcript(broken.replace(noise,bad_noise))
        for fatal in ('KERNEL PANIC','KERNEL '+noise+'PANIC','*** USER PROCESS PAGE FAULT ***'):
            with self.assertRaises(ValueError): validate_transcript(broken+fatal+'\n')

if __name__=='__main__': unittest.main()
