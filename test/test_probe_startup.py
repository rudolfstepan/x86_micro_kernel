"""Run the production probe transaction with adversarial scheduler admission.

The complete spawn and Ring-3 startup functions are extracted verbatim. Only
the contiguous startup-report cases (identity, self-test, progress, readiness)
are selected from the large report dispatcher; unrelated protocol cases are
not modeled. Process, IPC and protected-storage fault points are test doubles.
"""
import os
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'scripts'))
from build_user_program import find_zig
from measure_cpp_baseline import suppress_windows_test_dialogs
from test_boot_service_ready import function


class ProbeStartupTests(unittest.TestCase):
    def test_immediate_execution_and_all_startup_rollback_stages(self):
        suppress_windows_test_dialogs()
        source = (ROOT/'kernel/init/supervisor.c').read_text()
        probe = (ROOT/'userspace/programs/reist_probe.c').read_text()
        report = function(source, 'int supervisor_probe_report(')
        report = report[:report.index('    if (report_type == REIST_REPORT_WCET_BASELINE)')]
        report = report.replace('int supervisor_probe_report(', 'static int startup_report(')
        report += '    return -1;\n}\n'
        production = function(source, 'static bool probe_control_valid(')+'\n'
        # Wrappers below invoke this exact validation before publication.
        production += report+'\n'+function(probe, 'static int report_startup(')+'\n'
        if 'static void probe_abort_prepared_spawn(' in source:
            production += function(source, 'static void probe_abort_prepared_spawn(')+'\n'
        production += function(source, 'static bool probe_spawn_next(')+'\n'
        directory = ROOT/'build/codex-agent/r12e/host'
        directory.mkdir(parents=True, exist_ok=True)
        fixture = (ROOT/'test/test_probe_startup_host.c').read_text().replace('/* PRODUCTION */',production)
        generated = directory/'startup.c'
        generated.write_text(fixture,encoding='utf-8')
        env = os.environ.copy()
        env['ZIG_GLOBAL_CACHE_DIR'] = str(ROOT/'build/codex-agent/r12e/zig-global')
        env['ZIG_LOCAL_CACHE_DIR'] = str(ROOT/'build/codex-agent/r12e/zig-local')
        for opt in ('-O0','-O2'):
            exe = directory/('startup'+opt+'.exe')
            result = subprocess.run([str(find_zig()),'cc','-std=c11',opt,'-UNDEBUG',
                '-Wall','-Wextra','-Werror','-Wno-unused-function','-I.',str(generated),'-o',str(exe)],
                cwd=ROOT,env=env,capture_output=True,text=True,timeout=60)
            self.assertEqual(result.returncode,0,result.stderr)
            result = subprocess.run([str(exe)],cwd=ROOT,capture_output=True,text=True,timeout=10)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('PROBE_STARTUP_HOST_OK',result.stdout)
            print(opt,result.stdout.strip())


if __name__=='__main__': unittest.main()
