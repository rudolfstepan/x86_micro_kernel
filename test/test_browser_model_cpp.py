"""R3.20 frozen model boundary and observed-input measurement regression tests."""
import copy
from pathlib import Path
import sys
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'scripts'))
from run_qemu_runtime_desktop import browser_model_commits, browser_model_latency, browser_model_crop, browser_model_guest_health


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
