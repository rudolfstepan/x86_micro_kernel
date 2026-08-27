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
        self.assertIn('numvcpus = "4"', source)
        self.assertIn('cpuid.coresPerSocket = "4"', source)
        self.assertIn('RemoteDisplay.vnc.enabled = "TRUE"', source)
        self.assertIn('RemoteDisplay.vnc.ip = "127.0.0.1"', source)
        self.assertIn('RemoteDisplay.vnc.port = "5909"', source)

    def test_runner_has_bounded_gui_input_and_cleanup(self):
        source = (ROOT / "scripts/run_vmware_mouse.ps1").read_text(
            encoding="utf-8"
        )
        self.assertIn("[ValidateRange(20, 120)]", source)
        self.assertIn("[ValidateRange(1, 20)]", source)
        self.assertIn("$attempt -le $InjectionAttempts", source)
        self.assertIn("$publishDeadline = (Get-Date).AddSeconds(30)", source)
        self.assertIn("TcpListener", source)
        self.assertIn("TcpClient", source)
        self.assertIn('RFB 003.008\\n', source)
        self.assertIn("SendPointer(stream, x, y, 0)", source)
        self.assertIn("SendPointer(stream, movedX, movedY, 1)", source)
        self.assertIn("transport=rfb-loopback", source)
        self.assertIn("ReceiveTimeout = 2000", source)
        self.assertNotIn("mouse_event", source)
        self.assertNotIn("SetCursorPos", source)
        self.assertIn("$workstation", source)
        self.assertIn("'-x'", source)
        self.assertIn("$workstationProcess = $null", source)
        self.assertIn("$workstationProcess = Start-Process", source)
        self.assertIn("$workstationProcessId = $workstationProcess.Id", source)
        self.assertIn("gate-owned VMware UI", source)
        self.assertIn("Get-Process vmware-vmx", source)
        self.assertIn("$vmxProcessId = 0", source)
        self.assertIn("$vmxProcessId = $vmxProcesses[0].Id", source)
        self.assertIn(
            "Stop-Process -Id $vmxProcessId -Force -ErrorAction SilentlyContinue",
            source,
        )
        self.assertNotIn("Stop-Process -Id $vmxProcesses[0].Id", source)
        self.assertNotIn("Get-Process vmware-vmx | Stop-Process", source)
        self.assertNotIn("& $vmrun -T ws stop", source)

    def test_runner_rejects_stale_session_and_serial_log(self):
        source = (ROOT / "scripts/run_vmware_mouse.ps1").read_text(
            encoding="utf-8"
        )
        self.assertIn("Workstation already has a running VM", source)
        self.assertIn("vmrun reports no VM but a VMware VMX process", source)
        self.assertIn("VMware Workstation UI is already running", source)
        self.assertIn("$packagePrefix", source)
        self.assertIn("$staleLocks", source)
        self.assertIn("Refusing to remove VMware lock outside package", source)
        self.assertIn("Remove-Item -LiteralPath $lockPath -Recurse -Force",
                      source)
        self.assertIn("Remove-Item -LiteralPath $serial -Force", source)
        self.assertIn("Test-Path -LiteralPath $serial", source)
        self.assertIn("$serialPublished", source)
        self.assertIn("if ($launched)", source)

    def test_runner_requires_ordered_end_to_end_markers(self):
        source = (ROOT / "scripts/run_vmware_mouse.ps1").read_text(
            encoding="utf-8"
        )
        for marker in (
            "USB: xHCI HID ready",
            "mouse-port=",
            "REIST_SMP SCHEDULER_READY cpus=4 probe_mask=0000000E",
            "REIST_GUI COMPOSITOR_READY",
            "DESKTOP_OK",
            "DESKTOP_EXPLORER_OK",
            "DESKTOP_MOUSE_OK",
            "REIST_GUI COMPOSITOR_AP_EXEC cpu=",
        ):
            self.assertIn(marker, source)
        self.assertIn("$hid -lt $scheduler", source)
        self.assertIn("$scheduler -lt $ready", source)
        self.assertIn("$ready -lt $desktop", source)
        self.assertIn("$desktop -lt $explorer", source)
        self.assertIn("$explorer -lt $ap", source)
        self.assertIn("$ap -lt $mouse", source)

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
