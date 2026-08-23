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
$required = @(
    'Watchdog: external backend required',
    'REIST_PROBE RECOVERY_SEQUENCE_OK',
    'BOOT_OK',
    'Starting userspace command interpreter from /bin/shell.prg',
    'REIST OS userspace shell'
)
$forbidden = @('*** KERNEL PANIC ***', 'TEST_FAIL', 'FATAL:')
$missing = $required
$watch = [System.Diagnostics.Stopwatch]::StartNew()
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
        $watch.Stop()
        "VMWARE CONTAINMENT MONITOR PASS elapsed=$([int]$watch.Elapsed.TotalSeconds)s" |
            Set-Content -LiteralPath $GateLog -Encoding utf8
        exit 0
    }
    Start-Sleep -Milliseconds 250
}
$watch.Stop()
throw "VMware containment markers timed out: $($missing -join ', ')"
