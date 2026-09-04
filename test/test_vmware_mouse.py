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
        self.assertIn("'vmware-compositor-restart'", source)
        self.assertIn("-ExpectCompositorRestart", source)
        self.assertIn("'vmware-benchmark'", source)
        self.assertIn("& $VmwareMouseRunner -Benchmark", source)
        self.assertIn("'vmware-rename'", source)
        self.assertIn("& $VmwareMouseRunner -Rename", source)
        self.assertIn("'vmware-hover-cadence'", source)
        self.assertIn("& $VmwareMouseRunner -HoverCadence", source)

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
            "C:\\>",
            "REIST_GUI COMPOSITOR_READY generation=",
            "DESKTOP_OK",
            "DESKTOP_EXPLORER_OK",
            "DESKTOP_MOUSE_OK",
        ):
            self.assertIn(marker, source)
        self.assertIn("$hid -lt $scheduler", source)
        self.assertIn("$scheduler -lt $shell", source)
        self.assertIn("$shell -lt $explorer", source)
        self.assertIn("$explorer -lt $ready", source)
        self.assertIn("$ready -lt $desktop", source)
        self.assertIn("$desktop -lt $mouse", source)
        self.assertNotIn("$ap =", source)
        self.assertIn("$preShell", source)
        self.assertIn("Desktop marker appeared before explicit shell command",
                      source)

    def test_restart_mode_requires_replacement_before_mouse(self):
        source = (ROOT / "scripts/run_vmware_mouse.ps1").read_text(
            encoding="utf-8"
        )
        self.assertIn("[switch]$ExpectCompositorRestart", source)
        self.assertIn("REIST_GUI COMPOSITOR_TIMEOUT_ARMED epoch=1", source)
        self.assertIn("REIST_GUI COMPOSITOR_RESTARTED epoch=2", source)
        self.assertIn("$readyCount -ge 2", source)
        self.assertIn("$apCount -ge 2", source)
        self.assertIn("$replacementExplorer -lt $replacementReady", source)
        self.assertIn("$replacementReady -lt $replacementAp", source)
        self.assertIn("$replacementAp -lt $mouse", source)

    def test_runner_starts_desktop_only_after_shell_marker(self):
        source = (ROOT / "scripts/run_vmware_mouse.ps1").read_text(
            encoding="utf-8"
        )
        self.assertIn("SendCommand", source)
        self.assertIn("Send-ExplicitDesktopCommand", source)
        self.assertIn("'desktop'", source)
        wait = source.index(
            "C:\\>"
        )
        command = source.index("Send-ExplicitDesktopCommand", wait)
        desktop = source.index("DESKTOP_OK", command)
        self.assertLess(wait, command)
        self.assertLess(command, desktop)

    def test_runner_fails_closed_on_compositor_or_kernel_failure(self):
        source = (ROOT / "scripts/run_vmware_mouse.ps1").read_text(
            encoding="utf-8"
        )
        self.assertIn("*** KERNEL PANIC ***", source)
        self.assertIn("REIST_GUI COMPOSITOR_DEGRADED", source)
        self.assertIn("REIST_GUI COMPOSITOR_RESTARTED", source)
        self.assertIn("Assert-NoForbiddenMarker", source)
        self.assertIn("Wait-PostSuccessStability", source)
        self.assertIn("PostSuccessStabilitySeconds", source)
        self.assertIn("Boot marker repeated during post-success stability", source)
        self.assertIn("REIST_FATAL", source)
        self.assertIn("REIST_RUNTIME_DEGRADATION", source)
        self.assertIn("DRIVER_DEGRADED", source)
        self.assertIn("SERVICE_DEGRADED", source)
        self.assertIn("REIST_STORAGE RECOVERY_WAIT_", source)
        self.assertIn(
            "BIOS loader marker repeated during post-success stability",
            source,
        )

    def test_benchmark_mode_is_bounded_and_requires_complete_hdd_proof(self):
        source = (ROOT / "scripts/run_vmware_mouse.ps1").read_text(
            encoding="utf-8"
        )
        self.assertIn("[ValidateRange(30, 360)]", source)
        self.assertIn("[switch]$Benchmark", source)
        self.assertIn("Benchmark and compositor-restart modes are exclusive",
                      source)
        self.assertIn("Send-ExplicitBenchmarkCommand", source)
        self.assertIn("'benchmark'", source)
        for marker in (
            "BENCHMARK_STATUS phase=hdd-create",
            "BENCHMARK_STATUS phase=hdd-fsync",
            "BENCHMARK_STATUS phase=hdd-cleanup state=complete",
            "BENCHMARK_STATUS phase=complete",
            "REIST OS System Benchmark",
            "Seq\\. Schreiben",
            "Seq\\. Lesen",
        ):
            self.assertIn(marker, source)
        self.assertIn("$promptAfter -ge 0", source)
        self.assertIn("$minimumBenchmarkWriteKiB = 95.0", source)
        self.assertIn("$minimumBenchmarkReadKiB = 415.0", source)
        self.assertIn("$minimumBenchmarkCpuRatio = 0.90", source)
        self.assertIn("$writeRate -lt $minimumBenchmarkWriteKiB", source)
        self.assertIn("$readRate -lt $minimumBenchmarkReadKiB", source)
        self.assertIn("$cpuRatio -lt $minimumBenchmarkCpuRatio", source)
        self.assertIn("Multi/Single", source)
        self.assertIn("REIST_STORAGE RESOURCE_QUARANTINED", source)
        self.assertIn("ATA_FLUSH_FAILED", source)
        self.assertIn("VMWARE BENCHMARK PASS", source)
        self.assertIn("stability=", source)
        self.assertIn("Wait-PostSuccessStability 'benchmark'", source)

    def test_svga_lifecycle_uses_explicit_bounded_render_probe(self):
        source = (ROOT / "scripts/run_vmware_mouse.ps1").read_text(
            encoding="utf-8"
        )
        self.assertIn("[switch]$SvgaLifecycle", source)
        self.assertIn("desktop.prg --render-probe", source)
        self.assertIn("Send-ExplicitShellProbeCommand", source)
        self.assertIn("Built-ins: cd path pwd history help exit", source)
        for marker in (
            "REIST_VIDEO SVGA2D_ACTIVE",
            "REIST_VIDEO SVGA2D_RECT_COPY_OK",
            "REIST_VIDEO SVGA2D_INACTIVE",
            "DESKTOP_METRICS",
            "DESKTOP_EXIT_OK",
        ):
            self.assertIn(marker, source)
        self.assertIn("Wait-PostSuccessStability 'svga2d'", source)
        self.assertIn("VMWARE SVGA2D LIFECYCLE PASS", source)

    def test_hover_mode_uses_real_xhci_motion_and_hard_frame_bounds(self):
        source = (ROOT / "scripts/run_vmware_mouse.ps1").read_text(
            encoding="utf-8"
        )
        dispatch = (ROOT / "scripts/test-reist-runtime.ps1").read_text(
            encoding="utf-8"
        )
        self.assertIn("[switch]$HoverCadence", source)
        command = source[source.index("function Send-ExplicitDesktopCommand"):
                         source.index("function Send-ExplicitShellProbeCommand")]
        self.assertIn("elseif ($HoverCadence)", command)
        self.assertNotIn("desktop.prg --hover-probe", command)
        self.assertIn("'desktop'", command)
        self.assertIn("REIST_GUI COMPOSITOR_READY generation=", source)
        self.assertIn("-CompositorHoverProbe", dispatch)
        self.assertIn("-SourcePackage $sourcePackage", dispatch)
        self.assertIn("OpenHoverSession", source)
        self.assertIn("$script:hoverSession.SendHoverStart()", source)
        self.assertIn("$script:hoverSession.SendHoverItems()", source)
        self.assertIn("$script:hoverSession.SendCommand($command)", source)
        self.assertIn("$script:hoverSession.Dispose()", source)
        self.assertIn("function Refresh-HoverSession", source)
        self.assertIn("hover graphics-session refreshed", source)
        self.assertIn("$hoverSessionRefreshed = Refresh-HoverSession", source)
        self.assertLess(
            source.index("$hoverSessionRefreshed = Refresh-HoverSession"),
            source.index("$hoverInputStarted = $watch.Elapsed"),
        )
        self.assertIn("function Get-DesktopMarkerText", source)
        self.assertIn("$Text.Replace($shellMarker, '')", source)
        self.assertIn("$desktopMarkerText = Get-DesktopMarkerText $text", source)
        self.assertIn("SendHoverStart", source)
        self.assertIn("MovePointerSmooth(0, 0, width - 1, height - 1)", source)
        self.assertIn("MovePointerSmooth(width - 1, height - 1, startX, startY)", source)
        self.assertIn("SendHoverItems", source)
        self.assertIn("DESKTOP_HOVER_MENU_READY", source)
        self.assertIn("Thread.Sleep(16)", source)
        self.assertIn("DESKTOP_HOVER_METRICS", source)
        self.assertIn("DESKTOP_HOVER_OK", source)
        self.assertIn("$maximumHoverFrameMs = 17", source)
        self.assertIn("$maximumPointerGapMs = 34", source)
        self.assertIn("$maximumMouseBatchReports = 4", source)
        self.assertIn("mouse_batch_max_ms", source)
        self.assertIn("mouse_batch_max_reports", source)
        self.assertIn("pointer_latency_max_ms", source)
        self.assertIn("pointer_call_max_ms", source)
        self.assertIn("pointer_failures", source)
        self.assertIn("items -ne 7", source)
        self.assertIn("$fullFrames -ne 0", source)
        self.assertIn("Wait-PostSuccessStability 'hover'", source)
        self.assertIn("VMWARE HOVER CADENCE PASS", source)

    def test_desktop_mode_observes_post_success_stability_interval(self):
        source = (ROOT / "scripts/run_vmware_mouse.ps1").read_text(
            encoding="utf-8"
        )
        self.assertIn("[ValidateRange(10, 60)]", source)
        self.assertIn("Wait-PostSuccessStability 'desktop'", source)
        self.assertIn("$stabilityDeadline =", source)
        self.assertIn("Assert-NoForbiddenMarker $stabilityText", source)

    def test_rename_mode_requires_guest_vfat_proof_and_stability(self):
        source = (ROOT / "scripts/run_vmware_mouse.ps1").read_text(
            encoding="utf-8"
        )
        self.assertIn("[switch]$Rename", source)
        self.assertIn("Send-ExplicitRenameCommand", source)
        self.assertIn("'gtest'", source)
        self.assertIn("GUEST_TEST_BEGIN", source)
        self.assertIn("VFAT_LFN_REPLACE_OK", source)
        self.assertIn("TEST_STAGE VFAT_UTF8_OK", source)
        self.assertIn("TEST_OK", source)
        self.assertIn("$renamePromptAfter -ge 0", source)
        self.assertIn("Wait-PostSuccessStability 'rename'", source)
        self.assertIn("VMWARE RENAME PASS", source)
        self.assertIn("TEST_FAIL", source)
        self.assertIn("VFAT_LFN_FAIL", source)


if __name__ == "__main__":
    unittest.main()
