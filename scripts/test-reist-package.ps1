[CmdletBinding()]
param(
    [ValidateSet('qemu', 'vmware', 'real_hw')]
    [string]$Target = 'qemu',
    [ValidateSet('vga', 'framebuffer')]
    [string]$Video = 'vga',
    [string]$BuildScript = '',
    [string]$LogRoot = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($BuildScript)) {
    $BuildScript = Join-Path $PSScriptRoot 'build-windows.ps1'
}
if ([string]::IsNullOrWhiteSpace($LogRoot)) {
    $LogRoot = Join-Path $RepoRoot 'build\codex-agent'
}
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$log = Join-Path $LogRoot "$stamp-package-$Target-$Video.log"
New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null

$watch = [System.Diagnostics.Stopwatch]::StartNew()
$exitCode = 0
try {
    $LASTEXITCODE = 0
    & $BuildScript -Target $Target -Video $Video -RunTests *> $log
    $exitCode = $LASTEXITCODE
}
catch {
    $exitCode = 1
    $_ | Out-String | Add-Content -LiteralPath $log
}

if ($exitCode -eq 0) {
    $requiredArtifacts = @(
        (Join-Path $RepoRoot 'build\kernel.bin'),
        (Join-Path $RepoRoot 'build\reist-os.img'),
        (Join-Path $RepoRoot 'build\programs\GTEST.PRG')
    )
    $artifactDeadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        $missingArtifacts = @(
            $requiredArtifacts | Where-Object {
                !(Test-Path -LiteralPath $_ -PathType Leaf)
            }
        )
        if ($missingArtifacts.Count -eq 0) { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $artifactDeadline)
    if ($missingArtifacts.Count -ne 0) {
        $exitCode = 1
        "Reference build returned without artifacts: $($missingArtifacts -join ', ')" |
            Add-Content -LiteralPath $log
    }
}
$watch.Stop()

if ($exitCode -ne 0) {
    Write-Output "PACKAGE FAIL exit=$exitCode elapsed=$([int]$watch.Elapsed.TotalSeconds)s log=$log"
    Get-Content -LiteralPath $log -Tail 40
    exit $exitCode
}

Write-Output "PACKAGE PASS elapsed=$([int]$watch.Elapsed.TotalSeconds)s log=$log"
