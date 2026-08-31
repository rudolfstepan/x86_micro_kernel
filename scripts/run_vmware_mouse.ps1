[CmdletBinding()]
param(
    [string]$SourcePackage = '',
    [string]$GateLog = '',
    [ValidateRange(20, 120)] [int]$TimeoutSeconds = 75,
    [ValidateRange(30, 360)] [int]$BenchmarkTimeoutSeconds = 180,
    [ValidateRange(30, 360)] [int]$RenameTimeoutSeconds = 240,
    [ValidateRange(1, 20)] [int]$InjectionAttempts = 12,
    [ValidateRange(10, 60)] [int]$PostSuccessStabilitySeconds = 10,
    [switch]$ExpectCompositorRestart,
    [switch]$Benchmark,
    [switch]$Rename,
    [switch]$SvgaLifecycle,
    [switch]$HoverCadence
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$minimumBenchmarkWriteKiB = 95.0
$minimumBenchmarkReadKiB = 415.0
$minimumBenchmarkCpuRatio = 0.90
$maximumHoverFrameMs = 17
$maximumPointerGapMs = 34
$maximumMouseBatchReports = 4
if ($Benchmark -and $ExpectCompositorRestart) {
    throw 'Benchmark and compositor-restart modes are exclusive.'
}
if ($Rename -and ($Benchmark -or $ExpectCompositorRestart)) {
    throw 'Rename, benchmark and compositor-restart modes are exclusive.'
}
if ($SvgaLifecycle -and
    ($Benchmark -or $Rename -or $ExpectCompositorRestart)) {
    throw 'SVGA lifecycle, rename, benchmark and compositor-restart modes are exclusive.'
}
if ($HoverCadence -and
    ($Benchmark -or $Rename -or $SvgaLifecycle -or
     $ExpectCompositorRestart)) {
    throw 'Hover cadence and all other specialized modes are exclusive.'
}
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if (!$SourcePackage) {
    $SourcePackage = Join-Path $repoRoot 'build\vmware\reist-os'
}
$SourcePackage = (Resolve-Path -LiteralPath $SourcePackage).Path
if (!$GateLog) {
    $logRoot = Join-Path $repoRoot 'build\codex-agent'
    New-Item -ItemType Directory -Force -Path $logRoot | Out-Null
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $GateLog = Join-Path $logRoot "$stamp-runtime-vmware-mouse.log"
}

$vmx = Join-Path $SourcePackage 'reist-os.vmx'
$serial = Join-Path $SourcePackage 'vmware-serial.log'
$vmrun = @(
    'C:\Program Files\VMware\VMware Workstation\vmrun.exe',
    'C:\Program Files (x86)\VMware\VMware Workstation\vmrun.exe'
) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
    Select-Object -First 1
if (!$vmrun) { throw 'Required VMware Workstation tool vmrun.exe was not found.' }
$workstation = Join-Path (Split-Path -Parent $vmrun) 'vmware.exe'
if (!(Test-Path -LiteralPath $workstation -PathType Leaf)) {
    throw 'Required VMware Workstation executable vmware.exe was not found.'
}
if (!(Test-Path -LiteralPath $vmx -PathType Leaf)) {
    throw 'VMware mouse package is missing; build target vmware first.'
}

$vmxText = Get-Content -LiteralPath $vmx -Raw
foreach ($setting in @(
    'numvcpus = "4"',
    'cpuid.coresPerSocket = "4"',
    'usb_xhci.present = "TRUE"',
    'mouse.vusb.present = "TRUE"',
    'mouse.vusb.useBasicMouse = "TRUE"',
    'usb.generic.allowHID = "FALSE"',
    'RemoteDisplay.vnc.enabled = "TRUE"',
    'RemoteDisplay.vnc.ip = "127.0.0.1"',
    'RemoteDisplay.vnc.port = "5909"'
)) {
    if (!$vmxText.Contains($setting)) {
        throw "VMware mouse package lacks required setting: $setting"
    }
}

# SMP service diagnostics can interleave inside both the kernel launch text
# and the shell banner.  This drive prompt is emitted only from the Ring-3
# command loop after main() has completed its startup diagnostics.
$shellMarker = 'C:\>'
$requiredBeforeInput = @(
    'USB: xHCI HID ready',
    'mouse-port=',
    'REIST_SMP SCHEDULER_READY cpus=4 probe_mask=0000000E',
    $shellMarker
)
if ($SvgaLifecycle) {
    $requiredBeforeInput = @('REIST_VIDEO SVGA2D_READY') +
        $requiredBeforeInput
}
$requiredAfterDesktop = if ($SvgaLifecycle) {
    @('DESKTOP_ACCELERATION_READY caps=', 'DESKTOP_OK')
} elseif ($HoverCadence) {
    @(
        'DESKTOP_ACCELERATION_READY caps=',
        'REIST_GUI COMPOSITOR_READY generation=',
        'DESKTOP_OK',
        'DESKTOP_EXPLORER_OK',
        'DESKTOP_HOVER_PROBE_READY'
    )
} else {
    @(
        'DESKTOP_ACCELERATION_READY caps=',
        'REIST_GUI COMPOSITOR_READY generation=',
        'DESKTOP_OK',
        'DESKTOP_EXPLORER_OK'
    )
}
$forbidden = @(
    '*** KERNEL PANIC ***',
    'KERNEL PANIC',
    'REIST_FATAL',
    'REIST_RUNTIME_DEGRADATION',
    'DRIVER_DEGRADED',
    'SERVICE_DEGRADED',
    'DRIVER_RESTARTED',
    'SERVICE_RESTARTED',
    'REIST_GUI COMPOSITOR_DEGRADED',
    'REIST_STORAGE RESOURCE_QUARANTINED',
    'REIST_STORAGE RECOVERY_WAIT_',
    'ATA_FLUSH_FAILED'
    'desktop: DISPLAY_SOFTWARE_FALLBACK'
    'desktop: SVGA2D-Transaktion status='
    'DESKTOP_HOVER_FAIL'
)
if (!$ExpectCompositorRestart) {
    $forbidden += 'REIST_GUI COMPOSITOR_RESTARTED'
}
if ($Benchmark) {
    $forbidden += @(
        'BENCHMARK_STATUS phase=hdd-failed',
        'BENCHMARK FAILED'
    )
}
if ($Rename) {
    $forbidden += @(
        'TEST_FAIL',
        'VFAT_LFN_FAIL'
    )
}
$running = & $vmrun -T ws list 2>$null
$runningVms = @($running | Where-Object {
    $_.Trim() -and $_.Trim() -notmatch '^Total running VMs:'
})
if ($runningVms.Count -ne 0) {
    throw 'VMware Workstation already has a running VM.'
}
$unexpectedVmx = @(Get-Process vmware-vmx -ErrorAction SilentlyContinue)
if ($unexpectedVmx.Count -ne 0) {
    throw 'vmrun reports no VM but a VMware VMX process is still running.'
}
$unexpectedWorkstation = @(Get-Process vmware -ErrorAction SilentlyContinue)
if ($unexpectedWorkstation.Count -ne 0) {
    throw 'VMware Workstation UI is already running; close it before the gate.'
}
$packagePrefix = $SourcePackage.TrimEnd(
    [IO.Path]::DirectorySeparatorChar,
    [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
$staleLocks = @(Get-ChildItem -LiteralPath $SourcePackage -Force |
    Where-Object { $_.Name.EndsWith('.lck',
        [StringComparison]::OrdinalIgnoreCase) })
foreach ($lock in $staleLocks) {
    $lockPath = [IO.Path]::GetFullPath($lock.FullName)
    if (!$lockPath.StartsWith(
            $packagePrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove VMware lock outside package: $lockPath"
    }
    Remove-Item -LiteralPath $lockPath -Recurse -Force
}
if (Test-Path -LiteralPath $serial -PathType Leaf) {
    Remove-Item -LiteralPath $serial -Force
}
$vncPort = 5909
$portProbe = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, $vncPort)
try {
    $portProbe.Start()
}
catch {
    throw "Required loopback RFB port $vncPort is already in use."
}
finally {
    $portProbe.Stop()
}

Add-Type -TypeDefinition @'
using System;
using System.Net.Sockets;
using System.Text;
using System.Threading;

public static class ReistRfbInput {
    private static byte[] ReadExact(NetworkStream stream, int count) {
        byte[] data = new byte[count];
        int offset = 0;
        while (offset < count) {
            int got = stream.Read(data, offset, count - offset);
            if (got <= 0) throw new InvalidOperationException("RFB stream closed");
            offset += got;
        }
        return data;
    }

    private static uint ReadU32(NetworkStream stream) {
        byte[] b = ReadExact(stream, 4);
        return ((uint)b[0] << 24) | ((uint)b[1] << 16) |
               ((uint)b[2] << 8) | b[3];
    }

    private static void SendPointer(NetworkStream stream, int x, int y,
                                    byte buttons) {
        byte[] message = new byte[] {
            5, buttons, (byte)(x >> 8), (byte)x,
            (byte)(y >> 8), (byte)y
        };
        stream.Write(message, 0, message.Length);
        stream.Flush();
    }

    private static void SendKey(NetworkStream stream, uint key,
                                bool pressed) {
        byte[] message = new byte[] {
            4, pressed ? (byte)1 : (byte)0, 0, 0,
            (byte)(key >> 24), (byte)(key >> 16),
            (byte)(key >> 8), (byte)key
        };
        stream.Write(message, 0, message.Length);
        stream.Flush();
    }

    public sealed class HoverSession : IDisposable {
        private readonly TcpClient client;
        private readonly NetworkStream stream;
        private readonly int width;
        private readonly int height;

        internal HoverSession(TcpClient client, NetworkStream stream,
                              int width, int height) {
            this.client = client;
            this.stream = stream;
            this.width = width;
            this.height = height;
        }

        private void MovePointerSmooth(int fromX, int fromY,
                                       int toX, int toY) {
            int distance = Math.Max(Math.Abs(toX - fromX),
                                    Math.Abs(toY - fromY));
            int steps = Math.Max(1, (distance + 1) / 2);
            for (int step = 1; step <= steps; ++step) {
                int x = fromX + (toX - fromX) * step / steps;
                int y = fromY + (toY - fromY) * step / steps;
                SendPointer(stream, x, y, 0);
                // Keep the reference path at 60 Hz. Faster RFB injection can
                // burst replaceable relative reports ahead of VMware's virtual
                // xHCI path without representing a smoother visible cursor.
                Thread.Sleep(16);
            }
        }

        public bool SendCommand(string command) {
            if (String.IsNullOrEmpty(command) || command.Length > 32)
                return false;
            try {
                foreach (char character in command) {
                    if (character < 0x20 || character > 0x7E) return false;
                    SendKey(stream, character, true);
                    SendKey(stream, character, false);
                    Thread.Sleep(20);
                }
                SendKey(stream, 0xFF0D, true);
                SendKey(stream, 0xFF0D, false);
                return true;
            }
            catch {
                return false;
            }
        }

        public bool SendHoverStart() {
            try {
                int centerX = width / 2;
                int centerY = height / 2;
                int startX = Math.Min(width - 1, 40);
                int startY = Math.Max(0, height - 15);
                SendPointer(stream, centerX, centerY, 0);
                MovePointerSmooth(centerX, centerY, startX, startY);
                SendPointer(stream, startX, startY, 1);
                Thread.Sleep(24);
                SendPointer(stream, startX, startY, 0);
                Thread.Sleep(100);
                return true;
            }
            catch {
                return false;
            }
        }

        public bool SendHoverItems() {
            try {
                int startX = Math.Min(width - 1, 40);
                int startY = Math.Max(0, height - 15);
                int itemX = Math.Min(width - 1, 100);
                int previousX = startX;
                int previousY = startY;
                for (int item = 0; item < 6; ++item) {
                    int itemY = Math.Max(0, height - 41 - item * 24);
                    MovePointerSmooth(previousX, previousY, itemX, itemY);
                    Thread.Sleep(100);
                    previousX = itemX;
                    previousY = itemY;
                }
                return true;
            }
            catch {
                return false;
            }
        }

        public void Dispose() {
            try {
                stream.Dispose();
            }
            finally {
                client.Dispose();
            }
        }
    }

    public static HoverSession OpenHoverSession(int port) {
        TcpClient client = null;
        try {
            client = new TcpClient();
            IAsyncResult pending = client.BeginConnect("127.0.0.1", port,
                                                        null, null);
            bool connected = pending.AsyncWaitHandle.WaitOne(2000);
            pending.AsyncWaitHandle.Close();
            if (!connected) {
                client.Dispose();
                return null;
            }
            client.EndConnect(pending);
            client.ReceiveTimeout = 2000;
            client.SendTimeout = 2000;
            NetworkStream stream = client.GetStream();
            string serverVersion = Encoding.ASCII.GetString(
                ReadExact(stream, 12));
            if (!serverVersion.StartsWith("RFB 003.008"))
                throw new InvalidOperationException("RFB version");
            byte[] version = Encoding.ASCII.GetBytes("RFB 003.008\n");
            stream.Write(version, 0, version.Length);
            int securityCount = stream.ReadByte();
            if (securityCount <= 0)
                throw new InvalidOperationException("RFB security");
            byte[] security = ReadExact(stream, securityCount);
            bool supportsNone = false;
            foreach (byte kind in security) {
                if (kind == 1) supportsNone = true;
            }
            if (!supportsNone)
                throw new InvalidOperationException("RFB authentication");
            stream.WriteByte(1);
            if (ReadU32(stream) != 0)
                throw new InvalidOperationException("RFB handshake");
            stream.WriteByte(1);
            byte[] geometry = ReadExact(stream, 4);
            int width = (geometry[0] << 8) | geometry[1];
            int height = (geometry[2] << 8) | geometry[3];
            ReadExact(stream, 16);
            uint nameLength = ReadU32(stream);
            if (width < 320 || height < 240 || nameLength > 1048576)
                throw new InvalidOperationException("RFB geometry");
            ReadExact(stream, (int)nameLength);
            return new HoverSession(client, stream, width, height);
        }
        catch {
            if (client != null) client.Dispose();
            return null;
        }
    }

    public static bool SendCommand(int port, string command) {
        if (String.IsNullOrEmpty(command) || command.Length > 32) return false;
        try {
            using (TcpClient client = new TcpClient()) {
                IAsyncResult pending = client.BeginConnect("127.0.0.1", port,
                                                            null, null);
                bool connected = pending.AsyncWaitHandle.WaitOne(2000);
                pending.AsyncWaitHandle.Close();
                if (!connected) return false;
                client.EndConnect(pending);
                client.ReceiveTimeout = 2000;
                client.SendTimeout = 2000;
                using (NetworkStream stream = client.GetStream()) {
                    string serverVersion = Encoding.ASCII.GetString(
                        ReadExact(stream, 12));
                    if (!serverVersion.StartsWith("RFB 003.008")) return false;
                    byte[] version = Encoding.ASCII.GetBytes("RFB 003.008\n");
                    stream.Write(version, 0, version.Length);

                    int securityCount = stream.ReadByte();
                    if (securityCount <= 0) return false;
                    byte[] security = ReadExact(stream, securityCount);
                    bool supportsNone = false;
                    foreach (byte kind in security) {
                        if (kind == 1) supportsNone = true;
                    }
                    if (!supportsNone) return false;
                    stream.WriteByte(1);
                    if (ReadU32(stream) != 0) return false;

                    stream.WriteByte(1);
                    byte[] geometry = ReadExact(stream, 4);
                    int width = (geometry[0] << 8) | geometry[1];
                    int height = (geometry[2] << 8) | geometry[3];
                    ReadExact(stream, 16);
                    uint nameLength = ReadU32(stream);
                    if (width <= 0 || height <= 0 || nameLength > 1048576)
                        return false;
                    ReadExact(stream, (int)nameLength);

                    foreach (char character in command) {
                        if (character < 0x20 || character > 0x7E) return false;
                        SendKey(stream, character, true);
                        SendKey(stream, character, false);
                        Thread.Sleep(20);
                    }
                    SendKey(stream, 0xFF0D, true);
                    SendKey(stream, 0xFF0D, false);
                    return true;
                }
            }
        }
        catch {
            return false;
        }
    }

    public static bool SendPointer(int port, int attempt) {
        try {
            using (TcpClient client = new TcpClient()) {
                IAsyncResult pending = client.BeginConnect("127.0.0.1", port,
                                                            null, null);
                bool connected = pending.AsyncWaitHandle.WaitOne(2000);
                pending.AsyncWaitHandle.Close();
                if (!connected) return false;
                client.EndConnect(pending);
                client.ReceiveTimeout = 2000;
                client.SendTimeout = 2000;
                using (NetworkStream stream = client.GetStream()) {
                    string serverVersion = Encoding.ASCII.GetString(
                        ReadExact(stream, 12));
                    if (!serverVersion.StartsWith("RFB 003.008")) return false;
                    byte[] version = Encoding.ASCII.GetBytes("RFB 003.008\n");
                    stream.Write(version, 0, version.Length);

                    int securityCount = stream.ReadByte();
                    if (securityCount <= 0) return false;
                    byte[] security = ReadExact(stream, securityCount);
                    bool supportsNone = false;
                    foreach (byte kind in security) {
                        if (kind == 1) supportsNone = true;
                    }
                    if (!supportsNone) return false;
                    stream.WriteByte(1);
                    if (ReadU32(stream) != 0) return false;

                    stream.WriteByte(1);
                    byte[] geometry = ReadExact(stream, 4);
                    int width = (geometry[0] << 8) | geometry[1];
                    int height = (geometry[2] << 8) | geometry[3];
                    ReadExact(stream, 16);
                    uint nameLength = ReadU32(stream);
                    if (width <= 0 || height <= 0 || nameLength > 1048576)
                        return false;
                    ReadExact(stream, (int)nameLength);

                    int direction = (attempt & 1) == 0 ? 1 : -1;
                    int x = width / 2;
                    int y = height / 2;
                    int movedX = Math.Max(0, Math.Min(width - 1,
                                                     x + direction * 32));
                    int movedY = Math.Max(0, Math.Min(height - 1, y + 16));
                    SendPointer(stream, x, y, 0);
                    SendPointer(stream, movedX, movedY, 1);
                    SendPointer(stream, movedX, movedY, 0);
                    return true;
                }
            }
        }
        catch {
            return false;
        }
    }

}
'@

function Read-SerialText {
    if (Test-Path -LiteralPath $serial -PathType Leaf) {
        return [string](Get-Content -LiteralPath $serial -Raw `
            -ErrorAction SilentlyContinue)
    }
    return ''
}

function Get-DesktopMarkerText([string]$Text) {
    # The Ring-3 shell is independent from the desktop process and can publish
    # its prompt between two serial writes of one desktop marker. Remove only
    # that known prompt for presence checks; raw evidence and ordering remain
    # untouched everywhere else.
    return $Text.Replace($shellMarker, '')
}

function Assert-NoForbiddenMarker([string]$Text) {
    foreach ($marker in $forbidden) {
        if ($Text.Contains($marker)) {
            throw "VMware mouse log contains forbidden marker: $marker"
        }
    }
}

function Wait-PostSuccessStability([string]$Mode, [int]$BaselineBootCount,
                                   [int]$BaselineLoaderCount) {
    $stabilityDeadline = (Get-Date).AddSeconds($PostSuccessStabilitySeconds)
    $stabilityText = Read-SerialText
    while ((Get-Date) -lt $stabilityDeadline) {
        Start-Sleep -Milliseconds 250
        $stabilityText = Read-SerialText
        Assert-NoForbiddenMarker $stabilityText
        $bootCount = ([regex]::Matches($stabilityText, 'BOOT_OK')).Count
        if ($bootCount -gt $BaselineBootCount) {
            throw 'Boot marker repeated during post-success stability.'
        }
        $loaderCount = ([regex]::Matches(
            $stabilityText, 'x86 native BIOS loader')).Count
        if ($loaderCount -gt $BaselineLoaderCount) {
            throw 'BIOS loader marker repeated during post-success stability.'
        }
    }
    "$Mode post-success stability=$PostSuccessStabilitySeconds seconds boot_count=$BaselineBootCount" |
        Add-Content -LiteralPath $GateLog -Encoding utf8
    return $stabilityText
}

function Send-BoundedMouseInput([int]$Attempt) {
    return [ReistRfbInput]::SendPointer($vncPort, $Attempt)
}

function Send-HoverCadenceInput {
    if ($null -ne $script:hoverSession) {
        return $script:hoverSession.SendHoverItems()
    }
    return $false
}

function Send-HoverStartInput {
    if ($null -ne $script:hoverSession) {
        return $script:hoverSession.SendHoverStart()
    }
    return $false
}

function Refresh-HoverSession {
    # The desktop command is injected while the guest still presents its
    # text-mode framebuffer.  VMware can publish a different RFB geometry
    # once the compositor selects the graphics mode, so never reuse that
    # pre-graphics connection for absolute pointer coordinates.
    if ($null -ne $script:hoverSession) {
        try {
            $script:hoverSession.Dispose()
        }
        catch {
            # Socket teardown is best effort. A dead pre-graphics connection
            # must not prevent the graphics-mode session from being opened.
        }
        finally {
            $script:hoverSession = $null
        }
    }
    $script:hoverSession = [ReistRfbInput]::OpenHoverSession($vncPort)
    if ($null -eq $script:hoverSession) {
        return $false
    }
    'hover graphics-session refreshed transport=rfb-loopback' |
        Add-Content -LiteralPath $GateLog -Encoding utf8
    return $true
}

function Send-ExplicitDesktopCommand {
    $command = if ($SvgaLifecycle) {
        'desktop.prg --render-probe'
    } elseif ($HoverCadence) {
        'desktop'
    } else {
        'desktop'
    }
    if ($HoverCadence) {
        if ($null -eq $script:hoverSession) {
            $script:hoverSession = [ReistRfbInput]::OpenHoverSession($vncPort)
        }
        return $null -ne $script:hoverSession -and
            $script:hoverSession.SendCommand($command)
    }
    return [ReistRfbInput]::SendCommand($vncPort, $command)
}

function Send-ExplicitShellProbeCommand {
    if ($HoverCadence -and $null -ne $script:hoverSession) {
        return $script:hoverSession.SendCommand('help')
    }
    return [ReistRfbInput]::SendCommand($vncPort, 'help')
}

function Send-ExplicitBenchmarkCommand {
    return [ReistRfbInput]::SendCommand($vncPort, 'benchmark')
}

function Send-ExplicitRenameCommand {
    return [ReistRfbInput]::SendCommand($vncPort, 'gtest')
}

$started = $false
$launched = $false
$vmxProcessId = 0
$workstationProcess = $null
$passed = $false
$script:hoverSession = $null
$watch = [System.Diagnostics.Stopwatch]::StartNew()
try {
    "Starting exact package VM: $vmx" |
        Set-Content -LiteralPath $GateLog -Encoding utf8
    # Start through Workstation itself: the runtime proof needs its visible
    # input window, and -x powers on the exact package VM in that window.
    $workstationProcess = Start-Process -FilePath $workstation -ArgumentList @(
        '-x', ('"' + $vmx + '"')
    ) -PassThru -WindowStyle Normal
    $publishDeadline = (Get-Date).AddSeconds(30)
    do {
        $vmxProcesses = @(Get-Process vmware-vmx -ErrorAction SilentlyContinue)
        if ($vmxProcesses.Count -gt 1) {
            throw "Expected at most one REIST VMX process, found $($vmxProcesses.Count)."
        }
        if ($vmxProcesses.Count -eq 1) {
            $launched = $true
            $vmxProcessId = $vmxProcesses[0].Id
        }
        $serialPublished = (Test-Path -LiteralPath $serial -PathType Leaf) -and
            (Get-Item -LiteralPath $serial).Length -gt 0
        if ($serialPublished) {
            $started = $true
            break
        }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $publishDeadline)
    if (!$started) { throw 'VMware mouse VM failed to start.' }

    $modeTimeout = if ($Benchmark) {
        $BenchmarkTimeoutSeconds
    } elseif ($Rename) {
        $RenameTimeoutSeconds
    } else {
        $TimeoutSeconds
    }
    $deadline = $watch.Elapsed.Add([TimeSpan]::FromSeconds($modeTimeout))
    $missing = $requiredBeforeInput
    while ($watch.Elapsed -lt $deadline) {
        $text = Read-SerialText
        Assert-NoForbiddenMarker $text
        $missing = @($requiredBeforeInput |
            Where-Object { !$text.Contains($_) })
        if ($missing.Count -eq 0) { break }
        Start-Sleep -Milliseconds 250
    }
    if ($missing.Count -ne 0) {
        throw "VMware mouse shell markers timed out: $($missing -join ', ')"
    }
    $text = Read-SerialText
    $shellPosition = $text.IndexOf($shellMarker)
    if ($shellPosition -lt 0) {
        throw 'VMware userspace shell marker disappeared before validation.'
    }
    if ($SvgaLifecycle) {
        $initialReady = $text.IndexOf('REIST_VIDEO SVGA2D_READY')
        if ($initialReady -lt 0 -or $initialReady -ge $shellPosition) {
            throw 'VMware SVGA2D readiness did not precede the Ring-3 shell.'
        }
    }
    $preShell = $text.Substring(0, $shellPosition)
    foreach ($unexpected in @('REIST_GUI COMPOSITOR_READY', 'DESKTOP_OK',
                               'DESKTOP_EXPLORER_OK', 'DESKTOP_MOUSE_OK')) {
        if ($preShell.Contains($unexpected)) {
            throw "Desktop marker appeared before explicit shell command: $unexpected"
        }
    }

    if ($Benchmark) {
        $commandSent = $false
        for ($attempt = 1; $attempt -le 3; ++$attempt) {
            $commandSent = Send-ExplicitBenchmarkCommand
            "benchmark command attempt=$attempt injected=$commandSent transport=rfb-loopback" |
                Add-Content -LiteralPath $GateLog -Encoding utf8
            if ($commandSent) { break }
            Start-Sleep -Milliseconds 250
        }
        if (!$commandSent) {
            throw 'VMware RFB benchmark command could not be injected.'
        }

        $benchmarkMarkers = @(
            'REIST Benchmark: begrenzte Diagnose laeuft ...',
            'BENCHMARK_STATUS phase=cpu',
            'BENCHMARK_STATUS phase=ram-write',
            'BENCHMARK_STATUS phase=ram-read',
            'BENCHMARK_STATUS phase=hdd-create',
            'BENCHMARK_STATUS phase=hdd-write progress_kib=0 total_kib=256',
            'BENCHMARK_STATUS phase=hdd-write progress_kib=64 total_kib=256',
            'BENCHMARK_STATUS phase=hdd-write progress_kib=128 total_kib=256',
            'BENCHMARK_STATUS phase=hdd-write progress_kib=192 total_kib=256',
            'BENCHMARK_STATUS phase=hdd-write progress_kib=256 total_kib=256',
            'BENCHMARK_STATUS phase=hdd-fsync',
            'BENCHMARK_STATUS phase=hdd-read progress_kib=0 total_kib=256',
            'BENCHMARK_STATUS phase=hdd-read progress_kib=64 total_kib=256',
            'BENCHMARK_STATUS phase=hdd-read progress_kib=128 total_kib=256',
            'BENCHMARK_STATUS phase=hdd-read progress_kib=192 total_kib=256',
            'BENCHMARK_STATUS phase=hdd-read progress_kib=256 total_kib=256',
            'BENCHMARK_STATUS phase=hdd-cleanup state=begin',
            'BENCHMARK_STATUS phase=hdd-cleanup state=complete',
            'BENCHMARK_STATUS phase=vga',
            'REIST OS System Benchmark',
            'BENCHMARK_STATUS phase=complete'
        )
        $benchmarkMissing = $benchmarkMarkers
        while ($watch.Elapsed -lt $deadline) {
            $text = Read-SerialText
            Assert-NoForbiddenMarker $text
            $position = $shellPosition
            $benchmarkMissing = @()
            foreach ($marker in $benchmarkMarkers) {
                $found = $text.IndexOf($marker, $position + 1,
                    [StringComparison]::Ordinal)
                if ($found -lt 0) {
                    $benchmarkMissing += $marker
                    break
                }
                $position = $found + $marker.Length - 1
            }
            if ($benchmarkMissing.Count -eq 0) {
                $promptAfter = $text.IndexOf($shellMarker, $position + 1,
                    [StringComparison]::Ordinal)
                $writeMatch = [regex]::Match($text,
                    '\|\s*HDD\s*\|\s*Seq\. Schreiben\s*\|\s*(?<rate>[0-9]+(?:[.,][0-9]+)?)\s+KiB/s\s*\|\s*OK\s*\|')
                $readMatch = [regex]::Match($text,
                    '\|\s*HDD\s*\|\s*Seq\. Lesen\s*\|\s*(?<rate>[0-9]+(?:[.,][0-9]+)?)\s+KiB/s\s*\|\s*OK\s*\|')
                $cpuRatioMatch = [regex]::Match($text,
                    '\|\s*CPU\s*\|\s*Multi/Single\s*\|\s*(?<ratio>[0-9]+(?:[.,][0-9]+)?)\s+x\s*\|\s*OK\s*\|')
                if ($promptAfter -ge 0 -and $writeMatch.Success -and
                    $readMatch.Success -and $cpuRatioMatch.Success) {
                    $culture = [Globalization.CultureInfo]::InvariantCulture
                    $writeText = $writeMatch.Groups['rate'].Value.Replace(',', '.')
                    $readText = $readMatch.Groups['rate'].Value.Replace(',', '.')
                    $cpuRatioText = $cpuRatioMatch.Groups['ratio'].Value.Replace(',', '.')
                    $writeRate = [double]::Parse($writeText, $culture)
                    $readRate = [double]::Parse($readText, $culture)
                    $cpuRatio = [double]::Parse($cpuRatioText, $culture)
                    if ($cpuRatio -lt $minimumBenchmarkCpuRatio) {
                        throw "VMware CPU scaling missed the frozen minimum: ratio=$cpuRatio/$minimumBenchmarkCpuRatio."
                    }
                    if ($writeRate -lt $minimumBenchmarkWriteKiB -or
                        $readRate -lt $minimumBenchmarkReadKiB) {
                        throw "VMware HDD rates missed the frozen minimum: write=$writeRate/$minimumBenchmarkWriteKiB read=$readRate/$minimumBenchmarkReadKiB KiB/s."
                    }
                    $bootCount = ([regex]::Matches($text, 'BOOT_OK')).Count
                    $loaderCount = ([regex]::Matches(
                        $text, 'x86 native BIOS loader')).Count
                    $text = Wait-PostSuccessStability 'benchmark' $bootCount `
                        $loaderCount
                    $text | Set-Content -LiteralPath $GateLog -Encoding utf8
                    $passed = $true
                    Write-Output ("VMWARE BENCHMARK PASS elapsed={0}s cpu_ratio={1}x write={2}KiB/s read={3}KiB/s cleanup=ok stability={4}s log={5}" -f
                        [int]$watch.Elapsed.TotalSeconds, $cpuRatioText,
                        $writeText, $readText, $PostSuccessStabilitySeconds,
                        $GateLog)
                    break
                }
            }
            Start-Sleep -Milliseconds 250
        }
        if (!$passed) {
            $lastMarker = if ($benchmarkMissing.Count -ne 0) {
                $benchmarkMissing[0]
            } else {
                'HDD result rows or shell return'
            }
            throw "VMware benchmark timed out waiting for: $lastMarker"
        }
    }
    elseif ($Rename) {
        $commandSent = $false
        for ($attempt = 1; $attempt -le 3; ++$attempt) {
            $commandSent = Send-ExplicitRenameCommand
            "rename command attempt=$attempt injected=$commandSent transport=rfb-loopback" |
                Add-Content -LiteralPath $GateLog -Encoding utf8
            if ($commandSent) { break }
            Start-Sleep -Milliseconds 250
        }
        if (!$commandSent) {
            throw 'VMware RFB rename-test command could not be injected.'
        }

        $renameMarkers = @(
            'GUEST_TEST_BEGIN',
            'VFAT_LFN_REPLACE_OK',
            'TEST_STAGE VFAT_UTF8_OK',
            'TEST_OK'
        )
        $renameMissing = $renameMarkers
        while ($watch.Elapsed -lt $deadline) {
            $text = Read-SerialText
            Assert-NoForbiddenMarker $text
            $position = $shellPosition
            $renameMissing = @()
            foreach ($marker in $renameMarkers) {
                $found = $text.IndexOf($marker, $position + 1,
                    [StringComparison]::Ordinal)
                if ($found -lt 0) {
                    $renameMissing += $marker
                    break
                }
                $position = $found + $marker.Length - 1
            }
            if ($renameMissing.Count -eq 0) {
                $renamePromptAfter = $text.IndexOf(
                    $shellMarker, $position + 1,
                    [StringComparison]::Ordinal)
                if ($renamePromptAfter -ge 0) {
                    $bootCount = ([regex]::Matches($text, 'BOOT_OK')).Count
                    $loaderCount = ([regex]::Matches(
                        $text, 'x86 native BIOS loader')).Count
                    $text = Wait-PostSuccessStability 'rename' $bootCount `
                        $loaderCount
                    $text | Set-Content -LiteralPath $GateLog -Encoding utf8
                    $passed = $true
                    Write-Output "VMWARE RENAME PASS elapsed=$([int]$watch.Elapsed.TotalSeconds)s cleanup=ok stability=$($PostSuccessStabilitySeconds)s log=$GateLog"
                    break
                }
            }
            Start-Sleep -Milliseconds 250
        }
        if (!$passed) {
            $lastMarker = if ($renameMissing.Count -ne 0) {
                $renameMissing[0]
            } else {
                'fresh Ring-3 shell prompt'
            }
            throw "VMware rename test timed out waiting for: $lastMarker"
        }
    }
    else {
    $commandSent = $false
    for ($attempt = 1; $attempt -le 3; ++$attempt) {
        $commandSent = Send-ExplicitDesktopCommand
        "desktop command attempt=$attempt injected=$commandSent transport=rfb-loopback" |
            Add-Content -LiteralPath $GateLog -Encoding utf8
        if ($commandSent) { break }
        Start-Sleep -Milliseconds 250
    }
    if (!$commandSent) {
        throw 'VMware RFB desktop command could not be injected.'
    }

    $desktopMissing = $requiredAfterDesktop
    while ($watch.Elapsed -lt $deadline) {
        $text = Read-SerialText
        Assert-NoForbiddenMarker $text
        $desktopMarkerText = Get-DesktopMarkerText $text
        $desktopMissing = @($requiredAfterDesktop |
            Where-Object { !$desktopMarkerText.Contains($_) })
        if ($desktopMissing.Count -eq 0) { break }
        Start-Sleep -Milliseconds 250
    }
    if ($desktopMissing.Count -ne 0) {
        throw "VMware explicit desktop markers timed out: $($desktopMissing -join ', ')"
    }
    if ($HoverCadence) {
        # Re-negotiate RFB only after the compositor has selected its final
        # graphics mode.  The hover trajectory uses absolute coordinates.
        $hoverSessionRefreshed = Refresh-HoverSession
        if (!$hoverSessionRefreshed) {
            throw 'VMware RFB graphics-mode hover session could not be opened.'
        }
    }

    if ($SvgaLifecycle) {
        $lifecycleMissing = @(
            'REIST_VIDEO SVGA2D_ACTIVE',
            'DESKTOP_ACCELERATION_READY caps=',
            'REIST_VIDEO SVGA2D_RECT_COPY_OK',
            'REIST_VIDEO SVGA2D_INACTIVE',
            'DESKTOP_METRICS',
            'DESKTOP_EXIT_OK'
        )
        $lifecycleComplete = $false
        $desktopExit = -1
        while ($watch.Elapsed -lt $deadline) {
            $text = Read-SerialText
            Assert-NoForbiddenMarker $text
            $active = $text.IndexOf(
                'REIST_VIDEO SVGA2D_ACTIVE', $shellPosition + 1,
                [StringComparison]::Ordinal)
            $accelerated = if ($active -ge 0) {
                $text.IndexOf(
                    'DESKTOP_ACCELERATION_READY caps=', $active + 1,
                    [StringComparison]::Ordinal)
            } else { -1 }
            $copy = if ($accelerated -ge 0) {
                $text.IndexOf(
                    'REIST_VIDEO SVGA2D_RECT_COPY_OK', $accelerated + 1,
                    [StringComparison]::Ordinal)
            } else { -1 }
            $inactive = if ($copy -ge 0) {
                $text.IndexOf(
                    'REIST_VIDEO SVGA2D_INACTIVE', $copy + 1,
                    [StringComparison]::Ordinal)
            } else { -1 }
            $desktopExit = if ($inactive -ge 0) {
                $text.IndexOf(
                    'DESKTOP_EXIT_OK', $inactive + 1,
                    [StringComparison]::Ordinal)
            } else { -1 }
            $metrics = if ($active -ge 0) {
                $text.IndexOf(
                    'DESKTOP_METRICS', $active + 1,
                    [StringComparison]::Ordinal)
            } else { -1 }
            $lifecycleMissing = @()
            if ($active -lt 0) {
                $lifecycleMissing += 'REIST_VIDEO SVGA2D_ACTIVE'
            } elseif ($accelerated -lt 0) {
                $lifecycleMissing += 'DESKTOP_ACCELERATION_READY caps='
            } elseif ($copy -lt 0) {
                $lifecycleMissing += 'REIST_VIDEO SVGA2D_RECT_COPY_OK'
            } elseif ($inactive -lt 0) {
                $lifecycleMissing += 'REIST_VIDEO SVGA2D_INACTIVE'
            } elseif ($desktopExit -lt 0) {
                $lifecycleMissing += 'DESKTOP_EXIT_OK'
            } elseif ($metrics -lt 0 -or $metrics -gt $desktopExit) {
                $lifecycleMissing += 'DESKTOP_METRICS'
            }
            if ($lifecycleMissing.Count -eq 0) {
                $lifecycleComplete = $true
                break
            }
            Start-Sleep -Milliseconds 250
        }
        if (!$lifecycleComplete) {
            throw "VMware SVGA2D lifecycle timed out waiting for: $($lifecycleMissing[0])"
        }

        $shellProbeSent = $false
        for ($attempt = 1; $attempt -le 3; ++$attempt) {
            $shellProbeSent = Send-ExplicitShellProbeCommand
            "shell probe attempt=$attempt injected=$shellProbeSent transport=rfb-loopback" |
                Add-Content -LiteralPath $GateLog -Encoding utf8
            if ($shellProbeSent) { break }
            Start-Sleep -Milliseconds 250
        }
        if (!$shellProbeSent) {
            throw 'VMware RFB post-lifecycle shell probe could not be injected.'
        }
        while ($watch.Elapsed -lt $deadline) {
            $text = Read-SerialText
            Assert-NoForbiddenMarker $text
            $helpAfter = $text.IndexOf(
                'Built-ins: cd path pwd history help exit', $desktopExit + 1,
                [StringComparison]::Ordinal)
            $promptAfter = if ($helpAfter -ge 0) {
                $text.IndexOf(
                    $shellMarker, $helpAfter + 1,
                    [StringComparison]::Ordinal)
            } else { -1 }
            if ($promptAfter -ge 0) {
                $bootCount = ([regex]::Matches($text, 'BOOT_OK')).Count
                $loaderCount = ([regex]::Matches(
                    $text, 'x86 native BIOS loader')).Count
                $text = Wait-PostSuccessStability 'svga2d' $bootCount `
                    $loaderCount
                $text | Set-Content -LiteralPath $GateLog -Encoding utf8
                $passed = $true
                Write-Output "VMWARE SVGA2D LIFECYCLE PASS elapsed=$([int]$watch.Elapsed.TotalSeconds)s stability=$($PostSuccessStabilitySeconds)s log=$GateLog"
                break
            }
            Start-Sleep -Milliseconds 250
        }
        if (!$passed) {
            throw 'VMware SVGA2D lifecycle did not return a responsive Ring-3 shell.'
        }
    }
    else {
    if ($HoverCadence) {
        $hoverInputStarted = $watch.Elapsed
        $hoverStartInjected = Send-HoverStartInput
        "hover Start-menu press injected=$hoverStartInjected transport=rfb-loopback" |
            Add-Content -LiteralPath $GateLog -Encoding utf8
        if (!$hoverStartInjected) {
            throw 'VMware RFB Start-menu press could not be injected.'
        }
        $hoverMenuReady = $false
        while ($watch.Elapsed -lt $deadline) {
            $text = Read-SerialText
            Assert-NoForbiddenMarker $text
            $hoverMenuReady = $text.IndexOf(
                'DESKTOP_HOVER_MENU_READY', $shellPosition + 1,
                [StringComparison]::Ordinal) -ge 0
            if ($hoverMenuReady) { break }
            Start-Sleep -Milliseconds 50
        }
        if (!$hoverMenuReady) {
            throw 'VMware Start-menu did not acknowledge the hover trajectory.'
        }
        $hoverInjected = Send-HoverCadenceInput
        "hover row trajectory injected=$hoverInjected transport=rfb-loopback" |
            Add-Content -LiteralPath $GateLog -Encoding utf8
        if (!$hoverInjected) {
            throw 'VMware RFB hover row trajectory could not be injected.'
        }
        $hoverPattern =
            'DESKTOP_HOVER_METRICS\s+version=(?<version>[0-9]+)\s+items=(?<items>[0-9]+)\s+frames=(?<frames>[0-9]+)\s+full_frames=(?<full_frames>[0-9]+)\s+total_ms=(?<total_ms>[0-9]+)\s+max_ms=(?<max_ms>[0-9]+)\s+damage_max=(?<damage_max>[0-9]+)\s+mouse_reports=(?<mouse_reports>[0-9]+)\s+mouse_batch_max_ms=(?<mouse_batch_max_ms>[0-9]+)\s+mouse_batch_max_reports=(?<mouse_batch_max_reports>[0-9]+)\s+pointer_frames=(?<pointer_frames>[0-9]+)\s+pointer_max_gap_ms=(?<pointer_max_gap_ms>[0-9]+)\s+pointer_latency_max_ms=(?<pointer_latency_max_ms>[0-9]+)\s+pointer_call_max_ms=(?<pointer_call_max_ms>[0-9]+)\s+pointer_failures=(?<pointer_failures>[0-9]+)\s+order_errors=(?<order_errors>[0-9]+)\s+clock_errors=(?<clock_errors>[0-9]+)'
        $hoverShellProbeSent = $false
        while ($watch.Elapsed -lt $deadline) {
            $text = Read-SerialText
            Assert-NoForbiddenMarker $text
            $hoverMatch = [regex]::Match($text, $hoverPattern)
            if ($hoverMatch.Success) {
                $hoverOk = $text.IndexOf(
                    'DESKTOP_HOVER_OK', $hoverMatch.Index,
                    [StringComparison]::Ordinal)
                $desktopExit = if ($hoverOk -ge 0) {
                    $text.IndexOf(
                        'DESKTOP_EXIT_OK', $hoverOk + 1,
                        [StringComparison]::Ordinal)
                } else { -1 }
                if ($desktopExit -ge 0 -and !$hoverShellProbeSent) {
                    $hoverShellProbeSent = Send-ExplicitShellProbeCommand
                    "hover shell probe injected=$hoverShellProbeSent transport=rfb-loopback" |
                        Add-Content -LiteralPath $GateLog -Encoding utf8
                    if (!$hoverShellProbeSent) {
                        throw 'VMware RFB post-hover shell probe could not be injected.'
                    }
                }
                $helpAfter = if ($desktopExit -ge 0) {
                    $text.IndexOf(
                        'Built-ins: cd path pwd history help exit',
                        $desktopExit + 1, [StringComparison]::Ordinal)
                } else { -1 }
                $promptAfter = if ($helpAfter -ge 0) {
                    $text.IndexOf(
                        $shellMarker, $helpAfter + 1,
                        [StringComparison]::Ordinal)
                } else { -1 }
                if ($hoverOk -ge 0 -and $promptAfter -ge 0) {
                    $version = [int]$hoverMatch.Groups['version'].Value
                    $items = [int]$hoverMatch.Groups['items'].Value
                    $frames = [int]$hoverMatch.Groups['frames'].Value
                    $fullFrames = [int]$hoverMatch.Groups['full_frames'].Value
                    $totalMs = [int]$hoverMatch.Groups['total_ms'].Value
                    $maxMs = [int]$hoverMatch.Groups['max_ms'].Value
                    $damageMax = [int]$hoverMatch.Groups['damage_max'].Value
                    $mouseReports = [int]$hoverMatch.Groups['mouse_reports'].Value
                    $mouseBatchMaxMs = [int]$hoverMatch.Groups['mouse_batch_max_ms'].Value
                    $mouseBatchMaxReports = [int]$hoverMatch.Groups['mouse_batch_max_reports'].Value
                    $pointerFrames = [int]$hoverMatch.Groups['pointer_frames'].Value
                    $pointerMaxGapMs = [int]$hoverMatch.Groups['pointer_max_gap_ms'].Value
                    $pointerLatencyMaxMs = [int]$hoverMatch.Groups['pointer_latency_max_ms'].Value
                    $pointerCallMaxMs = [int]$hoverMatch.Groups['pointer_call_max_ms'].Value
                    $pointerFailures = [int]$hoverMatch.Groups['pointer_failures'].Value
                    $orderErrors = [int]$hoverMatch.Groups['order_errors'].Value
                    $clockErrors = [int]$hoverMatch.Groups['clock_errors'].Value
                    if ($version -ne 1 -or $items -ne 6 -or $frames -ne 6) {
                        throw "VMware hover coverage is invalid: version=$version items=$items frames=$frames."
                    }
                    if ($fullFrames -ne 0 -or $damageMax -gt 2) {
                        throw "VMware hover used excessive damage: full_frames=$fullFrames damage_max=$damageMax."
                    }
                    if ($maxMs -gt $maximumHoverFrameMs) {
                        throw "VMware hover frame missed the frozen maximum: max_ms=$maxMs/$maximumHoverFrameMs."
                    }
                    if ($mouseBatchMaxReports -lt 1 -or
                        $mouseBatchMaxReports -gt $maximumMouseBatchReports -or
                        $mouseBatchMaxMs -gt $maximumPointerGapMs) {
                        throw "VMware mouse batch missed the frozen bound: reports=$mouseBatchMaxReports/$maximumMouseBatchReports elapsed_ms=$mouseBatchMaxMs/$maximumPointerGapMs."
                    }
                    if ($pointerMaxGapMs -gt $maximumPointerGapMs) {
                        throw "VMware pointer cadence missed the frozen maximum: gap_ms=$pointerMaxGapMs/$maximumPointerGapMs."
                    }
                    if ($pointerLatencyMaxMs -gt $maximumPointerGapMs -or
                        $pointerCallMaxMs -gt $maximumPointerGapMs) {
                        throw "VMware pointer service missed the frozen maximum: latency_ms=$pointerLatencyMaxMs call_ms=$pointerCallMaxMs/$maximumPointerGapMs."
                    }
                    if ($mouseReports -lt 6 -or $pointerFrames -lt 2 -or
                        $pointerFailures -ne 0 -or
                        $orderErrors -ne 0 -or $clockErrors -ne 0) {
                        throw "VMware hover input proof is invalid: reports=$mouseReports pointer_frames=$pointerFrames pointer_failures=$pointerFailures order_errors=$orderErrors clock_errors=$clockErrors."
                    }
                    $explorer = $text.IndexOf(
                        'DESKTOP_EXPLORER_OK', $shellPosition + 1,
                        [StringComparison]::Ordinal)
                    $ready = $text.IndexOf(
                        'REIST_GUI COMPOSITOR_READY generation=',
                        $shellPosition + 1, [StringComparison]::Ordinal)
                    $desktop = $text.IndexOf(
                        'DESKTOP_OK', $shellPosition + 1,
                        [StringComparison]::Ordinal)
                    $hoverReady = $text.IndexOf(
                        'DESKTOP_HOVER_PROBE_READY', $shellPosition + 1,
                        [StringComparison]::Ordinal)
                    $menuReady = $text.IndexOf(
                        'DESKTOP_HOVER_MENU_READY', $hoverReady + 1,
                        [StringComparison]::Ordinal)
                    $mouse = $text.IndexOf(
                        'DESKTOP_MOUSE_OK', $shellPosition + 1,
                        [StringComparison]::Ordinal)
                    if (!($shellPosition -lt $explorer -and
                        $explorer -lt $ready -and $ready -lt $desktop -and
                        $desktop -lt $hoverReady -and $hoverReady -lt $mouse -and
                        $mouse -lt $menuReady -and
                        $mouse -lt $hoverMatch.Index)) {
                        throw 'VMware supervised hover markers or xHCI input are out of order.'
                    }
                    $bootCount = ([regex]::Matches($text, 'BOOT_OK')).Count
                    $loaderCount = ([regex]::Matches(
                        $text, 'x86 native BIOS loader')).Count
                    $text = Wait-PostSuccessStability 'hover' $bootCount `
                        $loaderCount
                    $text | Set-Content -LiteralPath $GateLog -Encoding utf8
                    $passed = $true
                    $inputElapsedMs = [int](
                        ($watch.Elapsed - $hoverInputStarted).TotalMilliseconds)
                    Write-Output "VMWARE HOVER CADENCE PASS elapsed=$([int]$watch.Elapsed.TotalSeconds)s input_elapsed_ms=$inputElapsedMs hover_total_ms=$totalMs hover_max_ms=$maxMs pointer_gap_max_ms=$pointerMaxGapMs pointer_latency_max_ms=$pointerLatencyMaxMs pointer_call_max_ms=$pointerCallMaxMs stability=$($PostSuccessStabilitySeconds)s log=$GateLog"
                    break
                }
            }
            Start-Sleep -Milliseconds 50
        }
        if (!$passed) {
            throw 'VMware physical Start-menu hover cadence timed out.'
        }
    }
    if (!$HoverCadence) {
    if ($ExpectCompositorRestart) {
        $restartReady = $false
        while ($watch.Elapsed -lt $deadline) {
            $text = Read-SerialText
            Assert-NoForbiddenMarker $text
            $readyCount = ([regex]::Matches(
                $text, 'REIST_GUI COMPOSITOR_READY generation=')).Count
            $apCount = ([regex]::Matches(
                $text, 'REIST_GUI COMPOSITOR_AP_EXEC cpu=')).Count
            if ($text.Contains('REIST_GUI COMPOSITOR_TIMEOUT_ARMED epoch=1') -and
                $text.Contains('REIST_GUI COMPOSITOR_RESTARTED epoch=2') -and
                $readyCount -ge 2 -and $apCount -ge 2) {
                $restartReady = $true
                break
            }
            Start-Sleep -Milliseconds 250
        }
        if (!$restartReady) {
            throw 'VMware compositor replacement did not become healthy and AP-affine.'
        }
    }

    for ($attempt = 1; $attempt -le $InjectionAttempts; ++$attempt) {
        $injected = Send-BoundedMouseInput $attempt
        "input attempt=$attempt injected=$injected transport=rfb-loopback" |
            Add-Content -LiteralPath $GateLog -Encoding utf8
        Start-Sleep -Milliseconds 500
        $text = Read-SerialText
        $mouse = $text.IndexOf('DESKTOP_MOUSE_OK')
        if ($mouse -ge 0) {
            Assert-NoForbiddenMarker $text.Substring(
                0, $mouse + 'DESKTOP_MOUSE_OK'.Length)
            $hid = $text.IndexOf('USB: xHCI HID ready')
            $scheduler = $text.IndexOf(
                'REIST_SMP SCHEDULER_READY cpus=4 probe_mask=0000000E')
            $shell = $text.IndexOf($shellMarker)
            $desktop = $text.IndexOf('DESKTOP_OK')
            $explorer = $text.IndexOf('DESKTOP_EXPLORER_OK')
            $ready = $text.IndexOf('REIST_GUI COMPOSITOR_READY generation=')
            if ($ExpectCompositorRestart) {
                $armed = $text.IndexOf(
                    'REIST_GUI COMPOSITOR_TIMEOUT_ARMED epoch=1')
                $restart = $text.IndexOf(
                    'REIST_GUI COMPOSITOR_RESTARTED epoch=2')
                $replacementReady = $text.IndexOf(
                    'REIST_GUI COMPOSITOR_READY generation=', $restart)
                $replacementAp = $text.IndexOf(
                    'REIST_GUI COMPOSITOR_AP_EXEC cpu=', $replacementReady)
                $replacementDesktop = $text.IndexOf('DESKTOP_OK', $restart)
                $replacementExplorer = $text.IndexOf(
                    'DESKTOP_EXPLORER_OK', $restart)
                if (!($hid -lt $scheduler -and $scheduler -lt $shell -and
                    $shell -lt $armed -and $armed -lt $restart -and
                    $restart -lt $replacementExplorer -and
                    $replacementExplorer -lt $replacementReady -and
                    $replacementReady -lt $replacementAp -and
                    $replacementReady -lt $replacementDesktop -and
                    $replacementAp -lt $mouse -and
                    $replacementDesktop -lt $mouse)) {
                    throw 'VMware compositor restart markers are out of order.'
                }
            } elseif (!($hid -lt $scheduler -and $scheduler -lt $shell -and
                $shell -lt $explorer -and $explorer -lt $ready -and
                $ready -lt $desktop -and $desktop -lt $mouse)) {
                throw 'VMware mouse runtime markers are out of order.'
            }
            $bootCount = ([regex]::Matches($text, 'BOOT_OK')).Count
            $loaderCount = ([regex]::Matches(
                $text, 'x86 native BIOS loader')).Count
            $text = Wait-PostSuccessStability 'desktop' $bootCount `
                $loaderCount
            $text | Set-Content -LiteralPath $GateLog -Encoding utf8
            $passed = $true
            Write-Output "VMWARE MOUSE PASS elapsed=$([int]$watch.Elapsed.TotalSeconds)s stability=$($PostSuccessStabilitySeconds)s log=$GateLog"
            break
        }
        Assert-NoForbiddenMarker $text
    }
    if (!$passed) {
        throw 'VMware RFB pointer movement did not reach the desktop.'
    }
    }
    }
    }
}
finally {
    if ($null -ne $script:hoverSession) {
        try {
            $script:hoverSession.Dispose()
        }
        catch {
            # Continue with the process-scoped VMware teardown below.
        }
        finally {
            $script:hoverSession = $null
        }
    }
    if ($launched) {
        # A hard runtime teardown is intentionally process-scoped. vmrun stop
        # can block after the guest is already gone; the captured PID belongs
        # to the sole VMX process created after the empty-state precondition.
        Stop-Process -Id $vmxProcessId -Force -ErrorAction SilentlyContinue
        $cleanupDeadline = (Get-Date).AddSeconds(10)
        do {
            if ($null -eq (Get-Process -Id $vmxProcessId `
                    -ErrorAction SilentlyContinue)) { break }
            Start-Sleep -Milliseconds 100
        } while ((Get-Date) -lt $cleanupDeadline)
        if ($null -ne (Get-Process -Id $vmxProcessId `
                -ErrorAction SilentlyContinue)) {
            throw 'Timed out while stopping the exact VMware mouse VM.'
        }
    }
    if ($null -ne $workstationProcess) {
        $workstationProcessId = $workstationProcess.Id
        Stop-Process -Id $workstationProcessId -Force `
            -ErrorAction SilentlyContinue
        $workstationDeadline = (Get-Date).AddSeconds(10)
        do {
            if ($null -eq (Get-Process -Id $workstationProcessId `
                    -ErrorAction SilentlyContinue)) { break }
            Start-Sleep -Milliseconds 100
        } while ((Get-Date) -lt $workstationDeadline)
        if ($null -ne (Get-Process -Id $workstationProcessId `
                -ErrorAction SilentlyContinue)) {
            throw 'Timed out while stopping the gate-owned VMware UI.'
        }
    }
    $watch.Stop()
}
