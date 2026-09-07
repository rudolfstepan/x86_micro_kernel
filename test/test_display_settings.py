"""Real-code Display settings admission and lifecycle tests (no GUI mocks as proof)."""
import os
import re
from pathlib import Path
import subprocess
import sys
import unittest
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from build_user_program import find_zig
from measure_cpp_baseline import suppress_windows_test_dialogs
from test_display_abi_minimal import function


class DisplaySettingsTests(unittest.TestCase):
    def test_runner_separates_boot_probe_and_armed_applet_fault(self):
        from run_qemu_display_settings import validate_faults
        from run_qemu_smoke import REIST_PROBE_MARKERS, REIST_PROBE_COMPLETION_MARKER
        fault = ('*** USER PROCESS EXCEPTION ***\n'
                 'Exception: Invalid Opcode (IRQ 6)\nProcess terminated.\n')
        boot = fault + '\n'.join((*REIST_PROBE_MARKERS, REIST_PROBE_COMPLETION_MARKER)) + '\nBOOT_OK\n'
        applet = 'DISPLAY_APPLET_FAULT\n' + fault
        validate_faults(boot + applet)
        for bad in (fault + boot + applet, boot + fault + applet,
                    boot + applet + fault, boot + fault,
                    boot + 'DISPLAY_APPLET_FAULT\n',
                    boot.replace('REIST_PROBE REINTEGRATED', 'missing') + applet,
                    boot.replace(REIST_PROBE_COMPLETION_MARKER, 'missing') + applet,
                    boot.replace('CRASH_DETECTED','TEMP').replace('CRASH_RECOVERED','CRASH_DETECTED').replace('TEMP','CRASH_RECOVERED') + applet,
                    boot + applet + 'USER PROCESS PAGE FAULT',
                    boot + applet.replace('Invalid Opcode (IRQ 6)', 'Divide Error'),
                    boot.replace('Invalid Opcode (IRQ 6)', 'Divide Error') + applet):
            with self.subTest(bad=bad):
                with self.assertRaises(RuntimeError):
                    validate_faults(bad)

    def test_runner_waits_for_applied_selection_before_save_click(self):
        from run_qemu_display_settings import DisplayProof
        # QMP admits all three keys immediately; guest application of the last
        # one is deliberately delayed. Exercise the real runner, not a copy.
        for missing in (False, True):
            keys, clicks = [], []
            applied = False
            proof = DisplayProof.__new__(DisplayProof)
            proof.monitor = SimpleNamespace(key=keys.append)
            proof.text = lambda: ''
            proof.screenshot = lambda *args: None
            def wait(pattern, offset=0):
                nonlocal applied
                if pattern.startswith('DISPLAY_SELECTION_READY '):
                    self.assertEqual(keys, ['down'] * 3)
                    self.assertIn('1280x720', pattern)
                    if missing:
                        raise TimeoutError('selection not applied')
                    applied = True
                else:
                    self.assertTrue(applied)
            def click(x, y):
                self.assertTrue(applied, 'Save raced ahead of queued selection')
                clicks.append((x, y))
            proof.wait, proof.click = wait, click
            if missing:
                with self.assertRaises(TimeoutError):
                    proof.save((90, 71, 620, 452), 3, '1280x720', 'proof')
                self.assertEqual(clicks, [])
            else:
                proof.save((90, 71, 620, 452), 3, '1280x720', 'proof')
                self.assertEqual(clicks, [(140, 466)])
            self.assertEqual(keys, ['down'] * 3)

    def compile_and_run(self, name, sources):
        suppress_windows_test_dialogs()
        directory = ROOT / 'build/codex-agent/r321/host'
        directory.mkdir(parents=True, exist_ok=True)
        env = os.environ.copy()
        env['ZIG_GLOBAL_CACHE_DIR'] = str(ROOT / 'build/zig-global-cache')
        env['ZIG_LOCAL_CACHE_DIR'] = str(ROOT / 'build/zig-cache')
        for optimization in ('-O0', '-O2'):
            exe = directory / (name + optimization + '.exe')
            result = subprocess.run([str(find_zig()), 'cc', '-std=c11',
                optimization, '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                '-Iinclude', '-Iuserspace/config/include', '-Iuserspace/sdk/include',
                '-Iuserspace/gui/include', '-Iuserspace/storage/include', '-I.',
                '-Ibuild/codex-agent/r321/host', *sources, '-o', str(exe)],
                cwd=ROOT, env=env, capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('DISPLAY_TEST_OK', result.stdout)

    def test_setting_parser_and_mode_admission(self):
        self.compile_and_run('settings', ['test/test_display_settings_host.c',
            'userspace/config/lib/display_settings.c'])

    def test_applet_native_controls_and_owned_async_save(self):
        self.compile_and_run('applet', ['test/test_display_applet_host.c',
            'userspace/gui/apps/display/display_model.c',
            'userspace/config/lib/display_settings.c', 'userspace/config/lib/config.c',
            'userspace/gui/lib/control.c', 'userspace/gui/lib/value_controls.c'])

    def test_real_mode_programming_readback_and_cleanup(self):
        source = (ROOT / 'drivers/video/display_control.c').read_text()
        declarations = source[source.index('#define DISPI_INDEX'):source.index('static kernel_mutex_t')]
        names = ['static bool vmware_probe_apertures(', 'static bool vmware_memory_limits(',
            'void display_control_prepare(',
            'static int activate_vmware(', 'static int display_control_activate_locked(',
            'static int display_control_mode_query_locked(', 'int display_control_mode_query(',
            'static int display_control_mode_admit_locked(', 'int display_control_activate_mode(',
            'int display_control_activate(', 'static int display_control_deactivate_locked(',
            'int display_control_deactivate(']
        production = '\n'.join(function(source, name) for name in names)
        # Host replaces only privileged IRQ save/restore instructions; mode
        # admission, indexed I/O, readback and rollback remain production code.
        production = re.sub(r'__asm__ __volatile__\(.*?\);',
            lambda m: 'old_flags = 0U;' if '"=r"' in m[0] else '(void)old_flags;',
            production, flags=re.S)
        fixture = (ROOT / 'test/test_display_mode_host.c').read_text()
        fixture = fixture.replace('/* DECLARATIONS */', declarations).replace('/* PRODUCTION */', production)
        directory = ROOT / 'build/codex-agent/r321/host'
        directory.mkdir(parents=True, exist_ok=True)
        generated = directory / 'mode.c'
        generated.write_text(fixture, encoding='utf-8')
        self.compile_and_run('mode', [str(generated)])

    def test_real_driver_broker_and_syscall_boundaries(self):
        driver = (ROOT / 'userspace/drivers/video/vmware_svga2d.c').read_text()
        broker = (ROOT / 'userspace/gui/compositor/desktop_surface_runtime.c').read_text()
        syscall = (ROOT / 'kernel/syscall/syscall_table.c').read_text()
        declarations = driver[driver.index('#define SVGA2D_HEARTBEAT_MS'):driver.index('static void bytes_zero(')]
        production = '\n'.join(function(driver, name) for name in [
            'static void bytes_zero(', 'static int driver_command(', 'static int wait_idle(',
            'static int activate(', 'static int activate_mode(', 'static int deactivate(',
            'static int submit_2d(', 'static int request_valid(', 'static int handle_request('])
        production += '\n' + '\n'.join(function(broker, name) for name in [
            'static void clear_bytes(', 'int desktop_surface_runtime_allow_display(',
            'int desktop_surface_runtime_take_display(', 'static int queue_display_applet('])
        production += '\n' + function(syscall, 'static int syscall_display_mode(')
        fixture = '#define DISPLAY_RING3_HOST\n' + (ROOT / 'test/test_display_mode_host.c').read_text()
        fixture = fixture.replace('/* RING3_DECLARATIONS */', declarations).replace('/* RING3_PRODUCTION */', production)
        directory = ROOT / 'build/codex-agent/r321/host'
        directory.mkdir(parents=True, exist_ok=True)
        generated = directory / 'ring3.c'
        generated.write_text(fixture, encoding='utf-8')
        self.compile_and_run('ring3', [str(generated)])


if __name__ == '__main__':
    unittest.main()
