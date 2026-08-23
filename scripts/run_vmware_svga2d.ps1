[CmdletBinding()]
param(
    [string]$SourcePackage = '',
    [string]$GateLog = '',
    [ValidateRange(10, 120)] [int]$TimeoutSeconds = 60
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
    $GateLog = Join-Path $logRoot 'vmware-svga2d.log'
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
    throw 'VMware SVGA2D package is missing; build target vmware first.'
}

$required = @(
    'REIST_VIDEO SVGA2D_PROFILE pci=15AD:0405',
    'REIST_VIDEO SVGA2D_ACTIVE',
    'REIST_VIDEO SVGA2D_RECT_COPY_OK',
    'REIST_VIDEO SVGA2D_READY',
    'BOOT_OK'
)
$forbidden = @(
    '*** KERNEL PANIC ***', 'REIST_VIDEO DRIVER_DEGRADED',
    'REIST_VIDEO SVGA2D_REJECTED', 'REIST_VIDEO DRIVER_RESTARTED'
)
$running = & $vmrun -T ws list 2>$null
if ($running | Where-Object { $_.Trim() -ieq $vmx }) {
    throw 'VMware SVGA2D package is already running.'
}
if (Test-Path -LiteralPath $serial -PathType Leaf) {
    Remove-Item -LiteralPath $serial -Force
}

$started = $false
$watch = [System.Diagnostics.Stopwatch]::StartNew()
try {
    $startFailures = @()
    foreach ($mode in @('nogui', 'nogui', 'gui')) {
        $output = & $vmrun -T ws start $vmx $mode 2>&1
        $exit = $LASTEXITCODE
        $output | Add-Content -LiteralPath $GateLog -Encoding utf8
        $publishDeadline = (Get-Date).AddSeconds(5)
        do {
            $runningNow = & $vmrun -T ws list 2>$null
            $serialPublished = (Test-Path -LiteralPath $serial -PathType Leaf) `
                -and (Get-Item -LiteralPath $serial).Length -gt 0
            if (($runningNow | Where-Object { $_.Trim() -ieq $vmx }) -or
                $serialPublished) {
                $started = $true
                break
            }
            Start-Sleep -Milliseconds 250
        } while ((Get-Date) -lt $publishDeadline)
        if ($started) { break }
        $startFailures += "mode=$mode exit=$exit $($output -join ' ')"
    }
    if (!$started) {
        throw "VMware SVGA2D VM failed to start: $($startFailures -join '; ')"
    }
    $deadline = $watch.Elapsed.Add([TimeSpan]::FromSeconds($TimeoutSeconds))
    $missing = $required
    while ($watch.Elapsed -lt $deadline) {
        [string]$text = if (Test-Path -LiteralPath $serial -PathType Leaf) {
            [string](Get-Content -LiteralPath $serial -Raw `
                -ErrorAction SilentlyContinue)
        } else { '' }
        foreach ($marker in $forbidden) {
            if ($text.Contains($marker)) {
                throw "VMware SVGA2D log contains forbidden marker: $marker"
            }
        }
        $missing = @($required | Where-Object { !$text.Contains($_) })
        if ($missing.Count -eq 0) {
            $text | Set-Content -LiteralPath $GateLog -Encoding utf8
            Write-Output "VMWARE SVGA2D PASS elapsed=$([int]$watch.Elapsed.TotalSeconds)s log=$GateLog"
            return
        }
        Start-Sleep -Milliseconds 250
    }
    throw "VMware SVGA2D markers timed out: $($missing -join ', ')"
}
finally {
    if ($started) {
        & $vmrun -T ws stop $vmx hard 2>$null | Out-Null
    }
    $watch.Stop()
}
