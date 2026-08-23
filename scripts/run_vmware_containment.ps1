[CmdletBinding()]
param(
    [string]$SourcePackage = '',
    [string]$GateLog = ''
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
    $GateLog = Join-Path $logRoot 'vmware-containment.log'
}

$serial = Join-Path $SourcePackage 'vmware-serial.log'
$vmx = Join-Path $SourcePackage 'reist-os.vmx'
$vmrun = @(
    'C:\Program Files\VMware\VMware Workstation\vmrun.exe',
    'C:\Program Files (x86)\VMware\VMware Workstation\vmrun.exe'
) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
    Select-Object -First 1
if (!$vmrun) { throw 'Required VMware Workstation tool vmrun.exe was not found.' }
if (!(Test-Path -LiteralPath $vmx -PathType Leaf)) {
    throw 'VMware package is missing; build target vmware first.'
}
$required = @(
    'Watchdog: external backend required',
    'REIST_PROBE RECOVERY_SEQUENCE_OK',
    'BOOT_OK',
    'Starting userspace command interpreter from /bin/shell.prg',
    'REIST OS userspace shell'
)
$forbidden = @('*** KERNEL PANIC ***', 'TEST_FAIL', 'FATAL:')
$running = & $vmrun -T ws list 2>$null
if ($running | Where-Object { $_.Trim() -ieq $vmx }) {
    throw 'VMware containment package is already running.'
}
if (Test-Path -LiteralPath $serial -PathType Leaf) {
    Remove-Item -LiteralPath $serial -Force
}
$started = $false
$watch = [System.Diagnostics.Stopwatch]::StartNew()
try {
    $startFailures = @()
    for ($attempt = 1; $attempt -le 3 -and !$started; ++$attempt) {
        $startMode = @('nogui', 'nogui', 'gui')[$attempt - 1]
        $startOutput = & $vmrun -T ws start $vmx $startMode 2>&1
        $startExit = $LASTEXITCODE
        $startOutput | Add-Content -LiteralPath $GateLog
        $publishDeadline = (Get-Date).AddSeconds(5)
        do {
            $runningAfterStart = & $vmrun -T ws list 2>$null
            $serialPublished = (Test-Path -LiteralPath $serial -PathType Leaf) -and
                (Get-Item -LiteralPath $serial).Length -gt 0
            if (($runningAfterStart | Where-Object { $_.Trim() -ieq $vmx }) -or
                $serialPublished) {
                $started = $true
                break
            }
            Start-Sleep -Milliseconds 250
        } while ((Get-Date) -lt $publishDeadline)
        if (!$started) {
            $detail = ($startOutput | ForEach-Object { $_.ToString().Trim() } |
                Where-Object { $_ }) -join ' '
            if (!$detail) { $detail = 'no vmrun diagnostic' }
            $startFailures += "attempt=$attempt mode=$startMode exit=$startExit $detail"
            if ($attempt -lt 3) { Start-Sleep -Milliseconds 500 }
        }
    }
    if (!$started) {
        throw ('VMware start was not published: ' + ($startFailures -join '; '))
    }

    $missing = $required
    $deadline = $watch.Elapsed.Add([TimeSpan]::FromSeconds(60))
    while ($watch.Elapsed -lt $deadline) {
        [string]$text = if (Test-Path -LiteralPath $serial -PathType Leaf) {
            [string](Get-Content -LiteralPath $serial -Raw -ErrorAction SilentlyContinue)
        } else { '' }
        foreach ($marker in $forbidden) {
            if ($text.Contains($marker)) {
                throw "VMware serial log contains forbidden marker: $marker"
            }
        }
        $missing = @($required | Where-Object { !$text.Contains($_) })
        if ($missing.Count -eq 0) {
            "VMWARE CONTAINMENT MONITOR PASS elapsed=$([int]$watch.Elapsed.TotalSeconds)s" |
                Set-Content -LiteralPath $GateLog -Encoding utf8
            Write-Output "VMWARE CONTAINMENT PASS elapsed=$([int]$watch.Elapsed.TotalSeconds)s log=$GateLog"
            return
        }
        Start-Sleep -Milliseconds 250
    }
    throw "VMware containment markers timed out: $($missing -join ', ')"
}
finally {
    if ($started) {
        & $vmrun -T ws stop $vmx hard 2>$null | Out-Null
        $stopDeadline = (Get-Date).AddSeconds(5)
        do {
            $stillRunning = & $vmrun -T ws list 2>$null |
                Where-Object { $_.Trim() -ieq $vmx }
            if (!$stillRunning) { break }
            Start-Sleep -Milliseconds 250
        } while ((Get-Date) -lt $stopDeadline)
        if ($stillRunning) { throw 'VMware containment VM did not stop.' }
    }
    $watch.Stop()
}
