[CmdletBinding()]
param(
    [string]$Qemu = '',
    [string]$Image = '',
    [ValidateRange(30, 600)]
    [int]$TimeoutSeconds = 180,
    [switch]$SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if (-not $Image) {
    $Image = Join-Path $RepoRoot 'build\reist-os.img'
}
if (-not $Qemu) {
    $QemuCommand = Get-Command qemu-system-i386 -ErrorAction SilentlyContinue
    if ($QemuCommand) {
        $Qemu = $QemuCommand.Source
    } else {
        $Qemu = 'C:\tmp\qemu-portable\qemu-system-i386.exe'
    }
}

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build-windows.ps1') -Target qemu -Video vga
    if ($LASTEXITCODE -ne 0) {
        throw "QEMU VGA build failed with exit code $LASTEXITCODE."
    }
}

if (-not (Test-Path -LiteralPath $Qemu -PathType Leaf)) {
    throw "QEMU executable not found: $Qemu"
}
if (-not (Test-Path -LiteralPath $Image -PathType Leaf)) {
    throw "REIST image not found: $Image"
}

$PythonCommand = Get-Command python -ErrorAction Stop
$Runner = Join-Path $PSScriptRoot 'run_qemu_runtime_desktop.py'
$OutputDirectory = Join-Path $RepoRoot 'docs\assets\screenshots'
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

$Captures = @(
    @{
        Name = 'reist-desktop.png'
        Probe = @()
    },
    @{
        Name = 'reist-desktop-apps.png'
        Probe = @('--surface-probe')
    },
    @{
        Name = 'reist-notepad.png'
        Probe = @('--notepad-probe')
    }
)

foreach ($Capture in $Captures) {
    $Screenshot = Join-Path $OutputDirectory $Capture.Name
    $Arguments = @(
        $Runner,
        '--qemu', $Qemu,
        '--image', $Image,
        '--screenshot', $Screenshot,
        '--timeout', $TimeoutSeconds
    ) + $Capture.Probe
    & $PythonCommand.Source @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "QEMU documentation capture failed for $($Capture.Name)."
    }
}

Write-Host "Documentation screenshots updated: $OutputDirectory" -ForegroundColor Green
