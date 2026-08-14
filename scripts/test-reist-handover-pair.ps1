param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$BuildScript = Join-Path $PSScriptRoot 'build-windows.ps1'
$Runner = Join-Path $PSScriptRoot 'run_qemu_handover_pair.py'
$TemporaryActive = Join-Path $RepoRoot 'reist-pair-active.tmp.img'
$PairDir = Join-Path $RepoRoot 'build\handover-pair'

function Resolve-NativeTool {
    param([string]$Name, [string[]]$Fallbacks)
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    foreach ($candidate in $Fallbacks) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    throw "Required tool '$Name' was not found."
}

$Python = Resolve-NativeTool 'python' @(
    'C:\Python314\python.exe', 'C:\Python313\python.exe'
)
$Qemu = Resolve-NativeTool 'qemu-system-i386' @(
    'C:\tmp\qemu-portable\qemu-system-i386.exe',
    'C:\Program Files\qemu\qemu-system-i386.exe',
    'C:\msys64\mingw64\bin\qemu-system-i386.exe'
)

Push-Location $RepoRoot
try {
    & $BuildScript -Target qemu -Video vga -HandoverFaultInjection `
        -HandoverNodeId 1
    Copy-Item -LiteralPath 'build\reist-os.img' -Destination $TemporaryActive
    & $BuildScript -Target qemu -Video vga -HandoverFaultInjection `
        -HandoverNodeId 2
    New-Item -ItemType Directory -Force -Path $PairDir | Out-Null
    Copy-Item -LiteralPath $TemporaryActive `
        -Destination (Join-Path $PairDir 'active.img')
    Copy-Item -LiteralPath 'build\reist-os.img' `
        -Destination (Join-Path $PairDir 'standby.img')
    & $Python $Runner --qemu $Qemu `
        --active-image (Join-Path $PairDir 'active.img') `
        --standby-image (Join-Path $PairDir 'standby.img') --timeout 120 `
        --log 'build\guest-smoke-handover-pair.log'
    if ($LASTEXITCODE -ne 0) {
        throw "REIST two-channel handover failed with exit $LASTEXITCODE."
    }
}
finally {
    if (Test-Path -LiteralPath $TemporaryActive -PathType Leaf) {
        Remove-Item -LiteralPath $TemporaryActive -Force
    }
    Pop-Location
}
