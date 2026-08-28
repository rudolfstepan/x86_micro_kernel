[CmdletBinding()]
param(
    [string]$SourcePackage = '',
    [string]$GateLog = '',
    [ValidateRange(20, 120)] [int]$TimeoutSeconds = 75,
    [ValidateRange(1, 20)] [int]$InjectionAttempts = 12
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
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

$requiredBeforeInput = @(
    'USB: xHCI HID ready',
    'mouse-port=',
    'REIST_SMP SCHEDULER_READY cpus=4 probe_mask=0000000E',
    'REIST_GUI COMPOSITOR_READY',
    'DESKTOP_OK',
    'DESKTOP_EXPLORER_OK'
)
$forbidden = @(
    '*** KERNEL PANIC ***',
    'REIST_GUI COMPOSITOR_DEGRADED',
    'REIST_GUI COMPOSITOR_RESTARTED'
)
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

function Assert-NoForbiddenMarker([string]$Text) {
    foreach ($marker in $forbidden) {
        if ($Text.Contains($marker)) {
            throw "VMware mouse log contains forbidden marker: $marker"
        }
    }
}

function Send-BoundedMouseInput([int]$Attempt) {
    return [ReistRfbInput]::SendPointer($vncPort, $Attempt)
}

$started = $false
$launched = $false
$vmxProcessId = 0
$workstationProcess = $null
$passed = $false
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

    $deadline = $watch.Elapsed.Add([TimeSpan]::FromSeconds($TimeoutSeconds))
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
        throw "VMware mouse desktop markers timed out: $($missing -join ', ')"
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
            $ready = $text.IndexOf('REIST_GUI COMPOSITOR_READY')
            $desktop = $text.IndexOf('DESKTOP_OK')
            $explorer = $text.IndexOf('DESKTOP_EXPLORER_OK')
            if (!($hid -lt $scheduler -and $scheduler -lt $ready -and
                $ready -lt $desktop -and $desktop -lt $explorer -and
                $explorer -lt $mouse)) {
                throw 'VMware mouse runtime markers are out of order.'
            }
            $text | Set-Content -LiteralPath $GateLog -Encoding utf8
            $passed = $true
            Write-Output "VMWARE MOUSE PASS elapsed=$([int]$watch.Elapsed.TotalSeconds)s log=$GateLog"
            break
        }
        Assert-NoForbiddenMarker $text
    }
    if (!$passed) {
        throw 'VMware RFB pointer movement did not reach the desktop.'
    }
}
finally {
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
