"""R3.31 real bounded mouse policy, writer, UI model and broker tests."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from build_user_program import find_zig
from measure_cpp_baseline import suppress_windows_test_dialogs


class MouseSettingsTests(unittest.TestCase):
    def run_native(self, name, sources):
        suppress_windows_test_dialogs()
        env = os.environ.copy()
        env['ZIG_GLOBAL_CACHE_DIR'] = str(ROOT / 'build/zig-global-cache')
        env['ZIG_LOCAL_CACHE_DIR'] = str(ROOT / 'build/zig-cache')
        with tempfile.TemporaryDirectory(prefix='reist-mouse-') as directory:
            for optimization in ('-O0', '-O2'):
                with self.subTest(optimization=optimization):
                    exe = Path(directory) / (name + optimization + '.exe')
                    build = subprocess.run([str(find_zig()), 'cc', '-std=c11',
                        optimization, '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                        '-I.', '-Iinclude', '-Iuserspace/sdk/include',
                        '-Iuserspace/config/include', '-Iuserspace/gui/include',
                        '-Iuserspace/storage/include', *sources, '-o', str(exe)],
                        cwd=ROOT, env=env, capture_output=True, text=True, timeout=60)
                    self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
                    run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
                    self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                    self.assertIn('MOUSE_TEST_OK', run.stdout)

    def test_atomic_config_batch(self):
        self.run_native('config', ['test/test_mouse_config_host.c',
            'userspace/config/lib/config.c', 'userspace/config/lib/display_settings.c'])

    def test_policy_and_boundaries(self):
        self.run_native('settings', ['test/test_mouse_settings_host.c',
            'userspace/config/lib/mouse_settings.c', 'userspace/config/lib/config.c'])

    def test_actual_applet_controls_and_lifecycle(self):
        self.run_native('applet', ['test/test_mouse_applet_host.c',
            'userspace/gui/apps/mouse/mouse_model.c', 'userspace/config/lib/mouse_settings.c',
            'userspace/config/lib/config.c', 'userspace/gui/lib/control.c',
            'userspace/gui/lib/value_controls.c'])

    def test_broker_authority_and_stale_requests(self):
        from test_display_abi_minimal import function
        source = (ROOT / 'userspace/gui/compositor/desktop_surface_runtime.c').read_text()
        code = '\n'.join(function(source, name) for name in (
            'static void clear_bytes(', 'int desktop_surface_runtime_allow_mouse(',
            'static int mouse_surface_owned(', 'int desktop_surface_runtime_take_mouse(',
            'static int queue_mouse_applet('))
        with tempfile.TemporaryDirectory(prefix='reist-mouse-broker-') as directory:
            path = Path(directory) / 'broker.c'
            path.write_text((ROOT / 'test/test_mouse_broker_host.c').read_text().replace('/* PRODUCTION */', code))
            self.run_native('broker', [str(path)])


if __name__ == '__main__':
    unittest.main()
