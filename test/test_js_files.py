"""Actual bounded file broker and nested transport, no source-only authority proof."""
from pathlib import Path
import os, subprocess, sys, unittest, uuid
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from build_user_program import find_zig
from measure_cpp_baseline import suppress_windows_test_dialogs

class FilesTests(unittest.TestCase):
    def test_i386_broker_and_transport_o0_o2(self):
        suppress_windows_test_dialogs()
        directory=ROOT/'build/codex-agent/r336-js-files'/('host-'+uuid.uuid4().hex)
        directory.mkdir(parents=True)
        env=os.environ.copy()
        env['ZIG_GLOBAL_CACHE_DIR']=str(ROOT/'build/zig-global-cache')
        env['ZIG_LOCAL_CACHE_DIR']=str(directory/'cache')
        for opt in ('-O0','-O2'):
            exe=directory/(opt+'.exe')
            command=[find_zig(),'c++','-target','x86-windows-gnu',opt,'-std=c++20',
                '-fno-sanitize=all','-fno-exceptions','-fno-rtti',
                '-I',ROOT/'userspace/js','-I',ROOT/'userspace/sdk/include',
                '-I',ROOT/'userspace/quickjs/include',
                '-I',ROOT/'userspace/storage/include',ROOT/'test/js_files_host.cpp',
                ROOT/'userspace/js/file_broker.cpp',ROOT/'userspace/js/js_session.cpp',
                '-x','c++',ROOT/'userspace/js/file_protocol.c',ROOT/'userspace/js/script_protocol.c',
                ROOT/'userspace/js/js_protocol.c','-o',exe]
            for cmd,timeout in ((command,90),([exe],30)):
                result=subprocess.run(list(map(str,cmd)),env=env,capture_output=True,text=True,timeout=timeout,
                    creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('JS_FILES_HOST_OK',result.stdout)

if __name__=='__main__': unittest.main()
