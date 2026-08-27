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
if (!(Test-Path -LiteralPath $vmx -PathType Leaf)) {
    throw 'VMware mouse package is missing; build target vmware first.'
}

$vmxText = Get-Content -LiteralPath $vmx -Raw
foreach ($setting in @(
    'usb_xhci.present = "TRUE"',
    'mouse.vusb.present = "TRUE"',
    'mouse.vusb.useBasicMouse = "TRUE"',
    'usb.generic.allowHID = "FALSE"'
)) {
    if (!$vmxText.Contains($setting)) {
        throw "VMware mouse package lacks required setting: $setting"
    }
}

$requiredBeforeInput = @(
    'USB: xHCI HID ready',
    'mouse-port=',
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
if (Test-Path -LiteralPath $serial -PathType Leaf) {
    Remove-Item -LiteralPath $serial -Force
}

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class ReistMouseInput {
    [StructLayout(LayoutKind.Sequential)]
    public struct Rect { public int Left, Top, Right, Bottom; }

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")]
    public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")]
    public static extern void mouse_event(uint flags, int dx, int dy,
                                          uint data, UIntPtr extraInfo);
    [DllImport("user32.dll")]
    public static extern void keybd_event(byte key, byte scan, uint flags,
                                          UIntPtr extraInfo);
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

function Find-ReistVmwareWindow {
    $windows = @(Get-Process vmware -ErrorAction SilentlyContinue |
        Where-Object { $_.MainWindowHandle -ne 0 })
    $matches = @($windows |
        Where-Object {
            $_.MainWindowTitle -match '(?i)(reist-os|REIST OS)'
        })
    if ($matches.Count -eq 1) { return $matches[0] }
    if ($windows.Count -eq 1) { return $windows[0] }
    return $null
}

function Send-BoundedMouseInput([System.Diagnostics.Process]$Window, [int]$Attempt) {
    $handle = $Window.MainWindowHandle
    $rect = New-Object ReistMouseInput+Rect
    if ($handle -eq 0 -or
        ![ReistMouseInput]::GetWindowRect($handle, [ref]$rect)) {
        return $false
    }
    [void][ReistMouseInput]::SetForegroundWindow($handle)
    $centerX = [int](($rect.Left + $rect.Right) / 2)
    $centerY = [int](($rect.Top + $rect.Bottom) / 2)
    if (![ReistMouseInput]::SetCursorPos($centerX, $centerY)) { return $false }
    Start-Sleep -Milliseconds 100
    # VMware's documented Ctrl+G host shortcut directs subsequent synthetic
    # pointer input to the focused virtual machine without guest tools.
    [ReistMouseInput]::keybd_event(0x11, 0, 0, [UIntPtr]::Zero)
    [ReistMouseInput]::keybd_event(0x47, 0, 0, [UIntPtr]::Zero)
    [ReistMouseInput]::keybd_event(0x47, 0, 0x0002, [UIntPtr]::Zero)
    [ReistMouseInput]::keybd_event(0x11, 0, 0x0002, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 100
    [ReistMouseInput]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
    [ReistMouseInput]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
    $dx = if (($Attempt % 2) -eq 0) { 24 } else { -24 }
    [ReistMouseInput]::mouse_event(0x0001, $dx, 12, 0, [UIntPtr]::Zero)
    return $true
}

$started = $false
$passed = $false
$watch = [System.Diagnostics.Stopwatch]::StartNew()
try {
    "Starting exact package VM: $vmx" |
        Set-Content -LiteralPath $GateLog -Encoding utf8
    $start = Start-Process -FilePath $vmrun -ArgumentList @(
        '-T', 'ws', 'start', ('"' + $vmx + '"'), 'gui'
    ) -PassThru -WindowStyle Hidden
    $publishDeadline = (Get-Date).AddSeconds(12)
    do {
        $serialPublished = (Test-Path -LiteralPath $serial -PathType Leaf) -and
            (Get-Item -LiteralPath $serial).Length -gt 0
        if ($serialPublished) {
            $started = $true
            break
        }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $publishDeadline)
    if (!$started -and !$start.HasExited) {
        $start.Kill()
        [void]$start.WaitForExit(2000)
    }
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
        $window = Find-ReistVmwareWindow
        if ($null -ne $window) {
            $injected = Send-BoundedMouseInput $window $attempt
            "input attempt=$attempt injected=$injected title=$($window.MainWindowTitle)" |
                Add-Content -LiteralPath $GateLog -Encoding utf8
        } else {
            "input attempt=$attempt injected=False window=missing" |
                Add-Content -LiteralPath $GateLog -Encoding utf8
        }
        Start-Sleep -Milliseconds 500
        $text = Read-SerialText
        $mouse = $text.IndexOf('DESKTOP_MOUSE_OK')
        if ($mouse -ge 0) {
            Assert-NoForbiddenMarker $text.Substring(
                0, $mouse + 'DESKTOP_MOUSE_OK'.Length)
            $hid = $text.IndexOf('USB: xHCI HID ready')
            $ready = $text.IndexOf('REIST_GUI COMPOSITOR_READY')
            $desktop = $text.IndexOf('DESKTOP_OK')
            $explorer = $text.IndexOf('DESKTOP_EXPLORER_OK')
            if (!($hid -lt $ready -and $ready -lt $desktop -and
                $desktop -lt $explorer -and $explorer -lt $mouse)) {
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
        throw 'Injected VMware virtual pointer movement did not reach the desktop.'
    }
}
finally {
    if ($started) {
        $vmxProcesses = @(Get-Process vmware-vmx -ErrorAction SilentlyContinue)
        if ($vmxProcesses.Count -ne 1) {
            throw "Expected the sole REIST VMX process, found $($vmxProcesses.Count)."
        }
        Stop-Process -Id $vmxProcesses[0].Id -Force
        $cleanupDeadline = (Get-Date).AddSeconds(10)
        do {
            if ($null -eq (Get-Process -Id $vmxProcesses[0].Id `
                    -ErrorAction SilentlyContinue)) { break }
            Start-Sleep -Milliseconds 100
        } while ((Get-Date) -lt $cleanupDeadline)
        if ($null -ne (Get-Process -Id $vmxProcesses[0].Id `
                -ErrorAction SilentlyContinue)) {
            throw 'Timed out while stopping the exact VMware mouse VM.'
        }
        if (!$start.HasExited -and !$start.WaitForExit(5000)) {
            $start.Kill()
            [void]$start.WaitForExit(2000)
        }
    }
    $watch.Stop()
}
