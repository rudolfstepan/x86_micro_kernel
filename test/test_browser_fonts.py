"""Actual pinned TrueType behavior, not a bitmap-font or source-pattern proof."""
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from build_browser_fonts import extract,flags,sources
from build_user_program import find_zig,cpp_compile_flags
from measure_cpp_baseline import suppress_windows_test_dialogs

class BrowserFontsTests(unittest.TestCase):
    def test_real_truetype(self):
        suppress_windows_test_dialogs()
        for opt in ('-O0','-O2'):
            with self.subTest(opt=opt),tempfile.TemporaryDirectory(prefix='reist-ttf-host-') as tmp:
                root,fonts=extract(tmp);exe=root/'font-test.exe';zig=str(find_zig());objects=[]
                env=os.environ.copy();env['ZIG_GLOBAL_CACHE_DIR']=str(ROOT/'build/codex-agent/font-host-cache')
                env['ZIG_LOCAL_CACHE_DIR']=str(root/'cache')
                common=['-I'+str(ROOT),*flags(root),opt,'-DNDEBUG','-ffunction-sections','-fdata-sections']
                for i,source in enumerate([*sources(root),ROOT/'userspace/gui/apps/browser/font_engine.cpp',ROOT/'test/browser_fonts_host.cpp']):
                    obj=root/(str(i)+'.o');objects.append(obj)
                    command=[zig,'cc',*common,* (cpp_compile_flags() if source.suffix=='.cpp' else ['-std=c11']),'-c',str(source),'-o',str(obj)]
                    r=subprocess.run(command,capture_output=True,text=True,env=env,timeout=60)
                    self.assertEqual(r.returncode,0,r.stderr)
                r=subprocess.run([zig,'cc',*map(str,objects),'-o',str(exe)],capture_output=True,text=True,env=env,timeout=60)
                self.assertEqual(r.returncode,0,r.stderr)
                for mode in ('glyphs','sizes','broken','quota','cache'):
                    with self.subTest(mode=mode):
                        r=subprocess.run([str(exe),mode,str(fonts/'LiberationSerif-Regular.ttf')],capture_output=True,text=True,timeout=15)
                        self.assertEqual(r.returncode,0,r.stdout+r.stderr)
                        self.assertIn('BROWSER_TTF_OK',r.stdout)

if __name__=='__main__':unittest.main()
