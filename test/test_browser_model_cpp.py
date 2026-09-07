"""R3.20 frozen model boundary and observed-input measurement regression tests."""
import copy
import hashlib
import json
import os
import platform
import re
import shutil
import statistics
import struct
import subprocess
import uuid
from pathlib import Path
import sys
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'scripts'))
from run_qemu_runtime_desktop import browser_model_commits, browser_model_latency, browser_model_crop, browser_model_guest_health
import build_user_program as builder
from measure_cpp_baseline import compile_mixed_host, git, suppress_windows_test_dialogs, model_ui_digests
from test_browser_response_cpp import call_graph, stack_paths
from test_browser_resources_cpp import undefined_symbols

BASE = '864f869a7862af219eedd7e42dee1abba14ebfdf'
C = 'userspace/gui/apps/browser/browser_model.c'
CPP = 'userspace/gui/apps/browser/browser_model.cpp'
DEPS = ['userspace/gui/lib/html_document.c', 'userspace/gui/lib/value_controls.c']
FLAGS = ['-O2','-UNDEBUG','-fno-builtin','-Wall','-Wextra','-Werror',
         '-I.','-Iuserspace/gui/include','-Iuserspace/gui/apps/browser']


def model_call_graph(data, assembly):
    """Prove bounded compiler switch branches before excluding non-call edges.

    No indirect call/tail-call is admitted. Only an indexed, range-guarded
    ELF32 .rodata jump table whose every relocated target is an instruction
    inside the SAME STT_FUNC has no effect on stack depth. Unknown shapes fail.
    """
    builder.validate_cpp_object(data)
    offset = struct.unpack_from('<I',data,32)[0]
    width,count = struct.unpack_from('<HH',data,46)
    sections = [struct.unpack_from('<10I',data,offset+i*width) for i in range(count)]
    symbols = {}
    functions = {}
    relocations = {}
    for index,s in enumerate(sections):
        if s[1] == 2:
            strings = sections[s[6]]; names = data[strings[4]:strings[4]+strings[5]]
            rows = [struct.unpack_from('<IIIBBH',data,p) for p in range(s[4],s[4]+s[5],16)]
            symbols[index] = rows
            for name,start,size,info,_,section in rows:
                if info & 15 == 2 and section:
                    functions[names[name:names.index(0,name)].decode()] = (section,start,start+size)
    for s in sections:
        if s[1] == 9:
            for p in range(s[4],s[4]+s[5],8):
                at,info = struct.unpack_from('<II',data,p)
                relocations[(s[7],at)] = (info & 255,symbols[s[6]][info >> 8])
    lines = assembly.splitlines(); current = None; instructions = []; tables = []
    instruction_addresses = set()
    for line in lines:
        match = re.match(r'\s*([0-9a-f]+):\s+(?!R_)([a-z]+)\s',line)
        if match: instruction_addresses.add(int(match[1],16))
    for i,line in enumerate(lines):
        start = re.fullmatch(r'[0-9a-f]+ <([^>]+)>:',line.strip())
        if start: current = start[1]; instructions = []; continue
        op = re.match(r'\s*([0-9a-f]+):\s+([a-z]+)\s+(.*)',line)
        if not op: continue
        address,code,operand = int(op[1],16),op[2],op[3]
        if code == 'jmp' and '*' in operand:
            shape = re.fullmatch(r'\*0x([0-9a-f]+)\(,%([a-z]+),4\)',operand)
            if not shape or current not in functions: raise ValueError('unproved indirect branch')
            base,register = int(shape[1],16),shape[2]
            recent = instructions[-5:]
            guard = next((j for j,x in enumerate(recent) if x[0]=='cmp' and
                re.fullmatch(r'\$0x[0-9a-f]+,%'+register,x[1])),None)
            branch = next((j for j in range((guard if guard is not None else len(recent))+1,len(recent))
                           if recent[j][0]=='ja'),None)
            if guard is None or branch is None:
                raise ValueError('jump table without range guard')
            entries = int(recent[guard][1].split(',')[0][1:],16)+1
            if not 1<=entries<=256: raise ValueError('jump table capacity')
            # Clang may schedule a flag-preserving mov between cmp and ja.
            between = recent[guard+1:branch]+recent[branch+1:]
            for code_after,operand_after in between:
                if code_after not in ('mov','lea') or operand_after.endswith('%'+register):
                    raise ValueError('jump table index changed after guard')
            section,low,high = functions[current]
            # ff /4 SIB disp32: relocation belongs to the 4-byte displacement.
            relocation = relocations.get((section,address+3))
            if not relocation or relocation[0]!=1: raise ValueError('missing table relocation')
            symbol = relocation[1]; table_section = symbol[5]; base += symbol[1]
            table = sections[table_section]
            if table[1]!=1 or table[2]&1 or base+entries*4>table[5]:
                raise ValueError('invalid readonly jump table')
            for n in range(entries):
                at = base+n*4; target_reloc = relocations.get((table_section,at))
                if not target_reloc or target_reloc[0]!=1 or target_reloc[1][5]!=section:
                    raise ValueError('unproved table target relocation')
                target = struct.unpack_from('<I',data,table[4]+at)[0]+target_reloc[1][1]
                if not low<=target<high or target not in instruction_addresses:
                    raise ValueError('table target leaves function')
            tables.append(dict(function=current,entries=entries,file_offset=table[4]+base))
            lines[i] = ''  # proven intra-function control flow, not a stack edge
        instructions.append((code,operand))
    return call_graph('\n'.join(lines)),tables


class ModelCppTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        suppress_windows_test_dialogs()
        cls.directory = ROOT/'build/codex-agent/r320'/('model-'+uuid.uuid4().hex)
        cls.directory.mkdir()  # inherited workspace ACL; mkdtemp hardens ACL on Python 3.14
        print('MODEL_EVIDENCE',cls.directory)
        cls.baseline = cls.directory/'oracle.c'
        data = git('show',BASE+':'+C)
        assert git('rev-parse',BASE+':'+C).strip() == b'6b0de40d251a7c1ba70e2989cf361f3bb0a7b737'
        cls.baseline.write_bytes(data)
        cls.exports = re.findall(r'^(?:int|void|uint32_t) (browser_\w+)\(',data.decode(),re.M)
        assert len(cls.exports) == 6
        cls.env = os.environ.copy()
        cls.env['ZIG_GLOBAL_CACHE_DIR'] = str(ROOT/'build/codex-agent/r320/zig-global')
        cls.env['ZIG_LOCAL_CACHE_DIR'] = str(ROOT/'build/codex-agent/r320/zig-local')

    def test_differential_and_checked_values(self):
        fixture = 'test/test_browser_model_host.c'
        renames = ['-D'+name+'=baseline_'+name for name in self.exports]
        for opt in ('-O0','-O2'):
            exe = self.directory/('behavior'+opt+'.exe')
            compile_mixed_host(['test/test_browser_model_cpp_host.cpp',fixture,CPP,self.baseline,*DEPS],
                exe,[*FLAGS,opt],{str(self.baseline):renames,fixture:['-Dmain=fixture_main']},self.env)
            result = subprocess.run([str(exe)],cwd=ROOT,env=self.env,capture_output=True,text=True,timeout=15)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertRegex(result.stdout,r'BROWSER_MODEL_CPP_OK checks=\d+')
            print(opt,result.stdout.strip())

    def test_unchecked_construction_is_rejected(self):
        for index,case in enumerate(('AddressEdit v;', 'AddressEdit v({},nullptr,0,nullptr,nullptr,nullptr);',
                'TextRange v;', 'TextRange v({},0,0);',
                'auto p=AddressEdit::open(nullptr,0,nullptr,nullptr,nullptr).value_if(); (void)p;')):
            source = self.directory/('reject'+str(index)+'.cpp')
            source.write_text('#include "browser_model.hpp"\nusing namespace reist::browser;\nvoid f(){'+case+'}\n')
            result = subprocess.run([str(builder.find_zig()),'cc',*builder.cpp_compile_flags(),*FLAGS,
                '-Iuserspace/cpp/include','-c',str(source),'-o',str(source.with_suffix('.o'))],
                cwd=ROOT,env=self.env,capture_output=True,text=True,timeout=30)
            self.assertNotEqual(result.returncode,0,case)
            self.assertIn('error:',result.stderr); self.assertNotIn('file not found',result.stderr)

    def test_i386_profile_and_each_entrypoint_stack(self):
        evidence = {}
        objdump = shutil.which('objdump') or r'C:\msys64\mingw64\bin\objdump.exe'
        for name,source in (('c',self.baseline),('cpp',ROOT/CPP)):
            obj = self.directory/('target-'+name+'.o')
            flags = builder.cpp_compile_flags() if name=='cpp' else ['-std=c11']
            subprocess.run([*builder.freestanding_compile_prefix(builder.find_zig()),*flags,
                '-Iuserspace/cpp/include','-Iuserspace/libc/include','-Iuserspace/gui/include',
                '-Iuserspace/gui/apps/browser','-Xclang','-stack-usage-file','-Xclang',str(obj.with_suffix('.su')),
                '-c',str(source),'-o',str(obj)],cwd=ROOT,env=self.env,check=True,timeout=60)
            data = obj.read_bytes(); builder.validate_cpp_object(data)
            external = undefined_symbols(data)
            self.assertLessEqual(external,{'memcpy','memset','reist_gui_range_state_initialize',
                'reist_gui_value_result_initialize','reist_gui_range_configure','reist_gui_range_set'})
            rows = [line.split('\t') for line in obj.with_suffix('.su').read_text().splitlines()]
            self.assertTrue(rows); self.assertTrue(all(row[2]=='static' for row in rows))
            frames = {row[0].rsplit(':',1)[1]:int(row[1]) for row in rows}
            assembly = subprocess.check_output([objdump,'-dr','--no-show-raw-insn',str(obj)],text=True,timeout=15)
            obj.with_suffix('.dis').write_text(assembly)
            graph,tables = model_call_graph(data,assembly); self.assertEqual(set(frames),set(graph))
            self.assertTrue(tables)
            for table in tables:
                corrupt = bytearray(data)
                struct.pack_into('<I',corrupt,table['file_offset'],0xffffffff)
                with self.assertRaisesRegex(ValueError,'table target leaves function'):
                    model_call_graph(corrupt,assembly)
            paths = {root:stack_paths(frames,graph,root,external) for root in self.exports}
            evidence[name] = dict(frames=frames,graph=graph,verified_local_switch_tables=tables,external=sorted(external),paths=paths,
                peaks={root:max(n for _,n in values) for root,values in paths.items()})
        evidence['limit_delta_bytes'] = 256
        (self.directory/'stack-profile.json').write_text(json.dumps(evidence,indent=2))
        self.assertLessEqual(set(evidence['cpp']['external']),set(evidence['c']['external']))
        for root in self.exports:
            self.assertLessEqual(evidence['cpp']['peaks'][root],evidence['c']['peaks'][root]+256,root)
            for leaf,cost in evidence['cpp']['paths'][root]:
                if leaf in evidence['cpp']['external']:
                    baseline = [n for target,n in evidence['c']['paths'][root] if target==leaf]
                    self.assertTrue(baseline,(root,leaf)); self.assertLessEqual(cost,min(baseline)+256,(root,leaf))
        print('model per-entrypoint stack:',evidence['c']['peaks'],evidence['cpp']['peaks'])

    def test_paired_frozen_timing(self):
        fixture = 'test/test_browser_model_cpp_bench.c'
        for path in DEPS:
            self.assertEqual((ROOT/path).read_bytes().replace(b'\r\n',b'\n'),git('show',BASE+':'+path).replace(b'\r\n',b'\n'))
        executables = [self.directory/'baseline.exe',self.directory/'candidate.exe']
        for source,exe in zip((self.baseline,CPP),executables):
            compile_mixed_host([fixture,source,*DEPS],exe,FLAGS,environment=self.env)
        pairs = [[json.loads(subprocess.check_output([str(exe)],timeout=15,cwd=ROOT)) for exe in executables] for _ in range(5)]
        medians = {key:[statistics.median(pair[i][key] for pair in pairs) for i in (0,1)] for key in ('address_ns','scrollbar_ns')}
        evidence = dict(baseline_commit=BASE,baseline_sha256=hashlib.sha256(self.baseline.read_bytes()).hexdigest(),
            source_fixture_sha256_lf={p:hashlib.sha256((ROOT/p).read_bytes().replace(b'\r\n',b'\n')).hexdigest()
                for p in [fixture,CPP,'userspace/gui/apps/browser/browser_model.h','userspace/gui/apps/browser/browser_model.hpp',*DEPS]},
            platform=platform.platform(),machine=platform.machine(),processor=platform.processor(),
            compiler=subprocess.check_output([str(builder.find_zig()),'version'],timeout=10).decode().strip(),
            flags=FLAGS,cpp_flags=builder.cpp_compile_flags(),lto=False,iterations_per_sample=200000,
            process_deadline_seconds=15,clock='QueryPerformanceCounter outside loops',pairs_c_cpp=pairs,
            medians_c_cpp_ns=medians,limits=dict(ratio=1.2,cpp_ns=50000))
        (self.directory/'paired-model.json').write_text(json.dumps(evidence,indent=2))
        self.assertEqual(os.name,'nt'); self.assertEqual(evidence['compiler'],'0.16.0')
        for key,(c,cpp) in medians.items():
            self.assertTrue(all(pair[i][key]>0 for pair in pairs for i in (0,1)))
            self.assertLessEqual(cpp,c*1.2,key); self.assertLessEqual(cpp,50000,key)
        print('paired model ns:',medians)

    def test_single_production_boundary_and_accepted_ui_provenance(self):
        source = (ROOT/'scripts/build_system_programs.py').read_text()
        self.assertEqual(source.count('ROOT / "'+CPP+'"'),1)
        self.assertNotIn('ROOT / "'+C+'"',source); self.assertFalse((ROOT/C).exists())
        app = (ROOT/'userspace/gui/apps/browser/main.c').read_text()
        for name in self.exports:
            if name != 'browser_build_layout': self.assertTrue(name+'(' in app,name)
        # CSS scenes replaced this legacy layout caller; preserve and test its
        # C entrypoint without inventing a new production layout algorithm.
        self.assertTrue('browser_scene_raster_forms(' in app)
        self.assertTrue('layout->total_height=scenes[candidate].total_height;' in app)
        saved = json.loads((ROOT/'build/codex-agent/r320a/accepted-c/baseline.json').read_text())
        self.assertTrue(saved['passed']); self.assertEqual(saved['harness_fixture_sha256'],model_ui_digests())
        runtime = (ROOT/'scripts/test-reist-runtime.ps1').read_text()
        self.assertIn("'runtime-desktop-browser-model' {",runtime)
        self.assertIn('--model-ui-pair',runtime)
        self.assertIn('build/codex-agent/r320a/accepted-c',runtime)


class ModelTelemetryTests(unittest.TestCase):
    def test_setup_waits_for_composed_focus_without_reinjecting_input(self):
        import run_qemu_runtime_desktop as runner
        blank=(64,64,b'\xff\xff\xff'*(64*64))
        pixels=bytearray(blank[2]);start=(20*64+20)*3
        pixels[start:start+20*3]=b'\xff\xfd\xe0'*20
        ready=(64,64,bytes(pixels))
        capture=mock.Mock(side_effect=((blank,'old.ppm'),(ready,'new.ppm')))
        with mock.patch.object(runner.time,'sleep'), mock.patch.object(runner.time,'monotonic',return_value=1):
            self.assertEqual(runner.browser_model_initial(capture,40,40,2),(ready,'new.ppm',(10,10)))
        self.assertEqual(capture.call_count,2)
        capture=mock.Mock(return_value=(blank,'old.ppm'))
        with mock.patch.object(runner.time,'sleep'), mock.patch.object(runner.time,'monotonic',return_value=1):
            with self.assertRaisesRegex(RuntimeError,'focus pixels'):
                runner.browser_model_initial(capture,40,40,2)
        self.assertEqual(capture.call_count,32)
        with mock.patch.object(runner.time,'monotonic',return_value=2):
            with self.assertRaisesRegex(RuntimeError,'focus pixels'):
                runner.browser_model_initial(capture,40,40,2)
        self.assertEqual(capture.call_count,32)

    def test_boot_panic_is_terminal_without_waiting_for_browser_ready(self):
        browser_model_guest_health('REIST_SMP READY failed=0\n')
        for failure in ('KERNEL PANIC','kernel panic','BROWSER_PROBE_FAIL','DESKTOP_BROWSER_FAIL'):
            with self.assertRaisesRegex(RuntimeError,failure) as error:
                browser_model_guest_health('boot\n'+failure+'\n'+'detail '*1000)
            self.assertLessEqual(len(str(error.exception)),721)

    def test_complete_ordered_state_and_frame_acknowledgements(self):
        lines = ''.join(f'BROWSER_MODEL_COMMIT ordinal={n} length={min(n,32)} '
                        f'scroll={48 if n>32 and n%2 else 0} body={4+max(0,n-32)} chrome={3+min(n,32)}\n'
                        for n in range(1,65))
        self.assertEqual(len(browser_model_commits(lines,64)),64)
        self.assertEqual(browser_model_commits('BROWSER_MODEL_COMMIT ordinal=1 length=1 scroll=0 body=4 chrome=4',1),[])
        for bad in (lines+lines, lines.replace('ordinal=2 ', 'ordinal=3 '),
                    lines.replace('length=32 ', 'length=33 ',1), lines.replace('scroll=48 ', 'scroll=0 ',1),
                    lines.replace('body=5 ', 'body=6 ',1), lines.replace('chrome=5\n', 'chrome=4\n',1)):
            with self.assertRaises(RuntimeError):
                browser_model_commits(bad,64)
        with self.assertRaises(RuntimeError):
            browser_model_commits(lines,63)

    def test_frozen_latency_bounds_and_raw_clock(self):
        samples = [dict(ordinal=n, dispatch_ns=n*1_000_000_000,
                        paint_observed_ns=n*1_000_000_000+100_000_000) for n in range(1,65)]
        self.assertEqual(browser_model_latency(samples),[100,100])
        paired = copy.deepcopy(samples)
        for s in paired: s['paint_observed_ns']+=21_000_000
        self.assertEqual(browser_model_latency(paired,samples),[121,121])
        for change in ('p95','max','clock','sequence','overlap','paired'):
            bad = copy.deepcopy(paired if change=='paired' else samples)
            if change=='p95':
                for s in bad: s['paint_observed_ns']=s['dispatch_ns']+250_000_001
            elif change=='max': bad[0]['paint_observed_ns']=bad[0]['dispatch_ns']+500_000_001
            elif change=='clock': bad[0]['paint_observed_ns']=bad[0]['dispatch_ns']
            elif change=='sequence': bad.pop()
            elif change=='overlap': bad[1]['dispatch_ns']=bad[0]['dispatch_ns']
            else:
                for s in bad: s['paint_observed_ns']+=1
            with self.assertRaises(RuntimeError,msg=change):
                browser_model_latency(bad,samples)

    def test_pixel_crop_requires_exact_bounded_region(self):
        ppm=(4,3,bytes(range(36)))
        self.assertEqual(browser_model_crop(ppm,1,1,2,2),bytes(range(15,21))+bytes(range(27,33)))
        for box in ((-1,0,1,1),(0,0,5,1),(0,0,1,4),(0,0,0,1)):
            with self.assertRaises(RuntimeError): browser_model_crop(ppm,*box)


if __name__ == '__main__':
    unittest.main()
