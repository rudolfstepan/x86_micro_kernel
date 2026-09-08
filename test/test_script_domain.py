"""Compile actual kernel attenuation/visibility paths with faultable backends."""
from pathlib import Path
import os, subprocess, sys, unittest, uuid
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from build_user_program import find_zig
from measure_cpp_baseline import suppress_windows_test_dialogs
from test_reist_probe_domain import function


class ScriptDomainTests(unittest.TestCase):
    def test_worker_restricts_before_engine_or_script(self):
        worker = (ROOT / 'userspace/js/js_worker.c').read_text()
        main = function(worker, 'int main(')
        self.assertIn('if(x86os_process_restrict_script()) return 70;', main)
        self.assertLess(main.index('x86os_process_restrict_script'), main.index('reist_libc_init_process'))
        self.assertNotIn('x86os_puts(', worker)
        self.assertIn('x86os_ipc_release(incoming)', main)

    def test_native_o0_o2(self):
        suppress_windows_test_dialogs()
        folder = ROOT / 'build/codex-agent/r334-script-domain' / ('host-' + uuid.uuid4().hex)
        folder.mkdir(parents=True)
        proc = (ROOT / 'kernel/proc/process.c').read_text()
        header = (ROOT / 'kernel/proc/process.h').read_text()
        syscalls = (ROOT / 'kernel/syscall/syscall_table.c').read_text()
        old = subprocess.run(['git', 'show', '270754bd:kernel/proc/process.c'], cwd=ROOT,
                             capture_output=True, text=True, timeout=10, check=True).stdout
        definitions = header[header.index('#define MAX_PROGRAMS'):header.index('struct vfs_node;')]
        code = '\n'.join(function(proc, name) for name in (
            'static void profile_allow(', 'static bool initialize_domain_profile(',
            'bool process_syscall_allowed(', 'int process_restrict_script(',
            'int process_get_info_for(', 'int process_get_info('))
        code += '\n' + function(old, 'static bool initialize_domain_profile(').replace(
            'initialize_domain_profile', 'baseline_profile')
        code += '\n' + '\n'.join(function(syscalls, name) for name in (
            'static int syscall_process_restrict(', 'static int syscall_process_info(',
            'static int syscall_process_identity_of('))
        dispatch = function(syscalls, 'void syscall_handler(').split('// Validate syscall index')[0]
        code += '\n' + dispatch + '\n ++dispatches; (void)arg1; (void)arg2; (void)arg3; (void)result; (void)timing_start; }\n'
        source = (ROOT / 'test/test_script_domain_host.c').read_text().replace(
            '/* DEFINITIONS */', definitions).replace('/* PRODUCTION */', code)
        (folder / 'host.c').write_text(source, encoding='utf-8')
        env = os.environ.copy()
        env['ZIG_GLOBAL_CACHE_DIR'] = str(ROOT / 'build/zig-global-cache')
        env['ZIG_LOCAL_CACHE_DIR'] = str(folder / 'cache')
        for opt in ('-O0', '-O2'):
            exe = folder / (opt + '.exe')
            for cmd in ([find_zig(), 'cc', '-target', 'x86-windows-gnu', opt, '-std=c11',
                         '-fno-sanitize=all', '-I', ROOT, folder/'host.c', '-o', exe], [exe]):
                result = subprocess.run(list(map(str, cmd)), cwd=ROOT, env=env, capture_output=True,
                    text=True, timeout=90, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('SCRIPT_DOMAIN_HOST_OK', result.stdout)


if __name__ == '__main__': unittest.main()
