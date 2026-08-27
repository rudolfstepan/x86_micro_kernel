import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class VmwareMouseTests(unittest.TestCase):
    def test_runtime_mode_dispatches_dedicated_runner(self):
        source = (ROOT / "scripts/test-reist-runtime.ps1").read_text(
            encoding="utf-8"
        )
        self.assertIn("'vmware-mouse'", source)
        self.assertIn("$VmwareMouseRunner", source)
        self.assertIn("& $VmwareMouseRunner", source)
        self.assertIn("$Mode -ne 'vmware-mouse'", source)

    def test_runner_requires_virtual_hid_without_passthrough(self):
        source = (ROOT / "scripts/run_vmware_mouse.ps1").read_text(
            encoding="utf-8"
        )
        self.assertIn('usb_xhci.present = "TRUE"', source)
        self.assertIn('mouse.vusb.present = "TRUE"', source)
        self.assertIn('mouse.vusb.useBasicMouse = "TRUE"', source)
        self.assertIn('usb.generic.allowHID = "FALSE"', source)

    def test_runner_has_bounded_gui_input_and_cleanup(self):
        source = (ROOT / "scripts/run_vmware_mouse.ps1").read_text(
            encoding="utf-8"
        )
        self.assertIn("[ValidateRange(20, 120)]", source)
        self.assertIn("[ValidateRange(1, 20)]", source)
        self.assertIn("$attempt -le $InjectionAttempts", source)
        self.assertIn("SetForegroundWindow", source)
        self.assertIn("mouse_event", source)
        self.assertIn("keybd_event", source)
        self.assertIn("$start.Kill()", source)
        self.assertIn("Get-Process vmware-vmx", source)
        self.assertIn("$vmxProcesses[0].Id", source)
        self.assertNotIn("Get-Process vmware-vmx | Stop-Process", source)

    def test_runner_rejects_stale_session_and_serial_log(self):
        source = (ROOT / "scripts/run_vmware_mouse.ps1").read_text(
            encoding="utf-8"
        )
        self.assertIn("Workstation already has a running VM", source)
        self.assertIn("Remove-Item -LiteralPath $serial -Force", source)
        self.assertIn("Test-Path -LiteralPath $serial", source)
        self.assertIn("$serialPublished", source)

    def test_runner_requires_ordered_end_to_end_markers(self):
        source = (ROOT / "scripts/run_vmware_mouse.ps1").read_text(
            encoding="utf-8"
        )
        for marker in (
            "USB: xHCI HID ready",
            "mouse-port=",
            "REIST_GUI COMPOSITOR_READY",
            "DESKTOP_OK",
            "DESKTOP_EXPLORER_OK",
            "DESKTOP_MOUSE_OK",
        ):
            self.assertIn(marker, source)
        self.assertIn("$hid -lt $ready", source)
        self.assertIn("$ready -lt $desktop", source)
        self.assertIn("$desktop -lt $explorer", source)
        self.assertIn("$explorer -lt $mouse", source)

    def test_runner_fails_closed_on_compositor_or_kernel_failure(self):
        source = (ROOT / "scripts/run_vmware_mouse.ps1").read_text(
            encoding="utf-8"
        )
        self.assertIn("*** KERNEL PANIC ***", source)
        self.assertIn("REIST_GUI COMPOSITOR_DEGRADED", source)
        self.assertIn("REIST_GUI COMPOSITOR_RESTARTED", source)
        self.assertIn("Assert-NoForbiddenMarker", source)


if __name__ == "__main__":
    unittest.main()
