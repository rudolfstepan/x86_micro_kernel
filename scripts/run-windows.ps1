[CmdletBinding()]
param(
    [switch]$NoBuild,
    [switch]$Headless,
    [ValidateSet('qemu', 'vmware', 'real_hw')]
    [string]$Target = 'qemu'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$BootImage = Join-Path $RepoRoot 'build\reist-os.img'

if (-not $NoBuild) {
    & (Join-Path $PSScriptRoot 'build-windows.ps1') -Target $Target
    if ($LASTEXITCODE -ne 0) {
        throw "Native Windows build failed with exit code $LASTEXITCODE."
    }
}
if (-not (Test-Path -LiteralPath $BootImage -PathType Leaf)) {
    throw "Boot image not found: $BootImage"
}

$QemuCommand = Get-Command 'qemu-system-i386' -ErrorAction SilentlyContinue
$Qemu = @(
    $(if ($QemuCommand) { $QemuCommand.Source }),
    'C:\tmp\qemu-portable\qemu-system-i386.exe',
    'C:\Program Files\qemu\qemu-system-i386.exe',
    'C:\msys64\mingw64\bin\qemu-system-i386.exe'
) | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) } |
    Select-Object -First 1
if (-not $Qemu) {
    throw 'qemu-system-i386.exe was not found. Install or extract native QEMU first.'
}

$arguments = @(
    '-accel', 'tcg',
    '-machine', 'pc',
    '-m', '512M',
    '-boot', 'c',
    '-drive', "file=$BootImage,format=raw,if=ide,index=0,media=disk",
    '-device', 'rtl8139,netdev=net0',
    '-netdev', 'user,id=net0',
    '-vga', 'std',
    '-no-reboot',
    '-no-shutdown'
)

$dataDisk = Join-Path $RepoRoot 'disk.img'
if (Test-Path -LiteralPath $dataDisk -PathType Leaf) {
    $arguments += @('-drive', "file=$dataDisk,format=raw,if=ide,index=1,media=disk")
}
$floppy = Join-Path $RepoRoot 'floppy.img'
if (Test-Path -LiteralPath $floppy -PathType Leaf) {
    $arguments += @('-drive', "file=$floppy,format=raw,if=floppy")
}

if ($Headless) {
    $arguments += @(
        '-display', 'none',
        '-monitor', 'none',
        '-serial', 'none',
        '-debugcon', 'stdio'
    )
}

Write-Host "Starting native BIOS image with: $Qemu" -ForegroundColor Green
& $Qemu @arguments
exit $LASTEXITCODE
