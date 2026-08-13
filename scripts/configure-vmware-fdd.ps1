[CmdletBinding()]
param(
    [ValidateSet('Physical', 'Image')]
    [string]$Mode = 'Physical',

    [ValidatePattern('^[A-Za-z]:$')]
    [string]$Drive = 'A:',

    [string]$VmxPath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if (-not $VmxPath) {
    $VmxPath = Join-Path $repoRoot 'build\vmware\reist-os\reist-os.vmx'
}
if (-not (Test-Path -LiteralPath $VmxPath -PathType Leaf)) {
    throw "VMware configuration not found: $VmxPath"
}
$VmxPath = (Resolve-Path -LiteralPath $VmxPath).Path

$vmrunCandidates = @(
    'C:\Program Files\VMware\VMware Workstation\vmrun.exe',
    'C:\Program Files (x86)\VMware\VMware Workstation\vmrun.exe'
)
$vmrun = $vmrunCandidates | Where-Object {
    Test-Path -LiteralPath $_ -PathType Leaf
} | Select-Object -First 1
if ($vmrun) {
    $runningVms = & $vmrun -T ws list 2>$null
    if ($runningVms | Where-Object {
        $_ -and [IO.Path]::GetFullPath($_) -eq $VmxPath
    }) {
        throw 'Power off the VMware virtual machine before changing its floppy backing.'
    }
}

if ($Mode -eq 'Physical') {
    $logicalDrive = Get-CimInstance Win32_LogicalDisk -Filter "DeviceID='$Drive'"
    if (-not $logicalDrive -or $logicalDrive.DriveType -ne 2) {
        throw "No removable floppy drive is available as $Drive"
    }
}

$configuration = [IO.File]::ReadAllText($VmxPath)
# Remove every old floppy0 property, including values VMware may have appended
# after opening the VM, so no image and physical device backing coexist.
$configuration = [Text.RegularExpressions.Regex]::Replace(
    $configuration,
    '(?m)^floppy0\.[^\r\n]*(?:\r?\n)?',
    '')
$configuration = [Text.RegularExpressions.Regex]::Replace(
    $configuration,
    '(?m)^bios\.bootOrder\s*=.*$',
    'bios.bootOrder = "floppy,hdd"')

if ($Mode -eq 'Physical') {
    $floppy = @"
floppy0.present = "TRUE"
floppy0.fileType = "device"
floppy0.fileName = "$Drive"
floppy0.startConnected = "TRUE"
floppy0.autodetect = "FALSE"
floppy0.clientDevice = "FALSE"
"@
} else {
    $image = Join-Path (Split-Path -Parent $VmxPath) 'reist-os-floppy.img'
    if (-not (Test-Path -LiteralPath $image -PathType Leaf)) {
        throw "Packaged floppy image not found: $image"
    }
    $floppy = @"
floppy0.present = "TRUE"
floppy0.fileType = "file"
floppy0.fileName = "reist-os-floppy.img"
floppy0.startConnected = "TRUE"
floppy0.autodetect = "FALSE"
"@
}

$configuration = $configuration.TrimEnd("`r", "`n") + "`r`n" + $floppy.Trim() + "`r`n"
[IO.File]::WriteAllText($VmxPath, $configuration, [Text.Encoding]::ASCII)

Write-Host "VMware floppy backing changed to $Mode."
if ($Mode -eq 'Physical') {
    Write-Host "Host $Drive is exposed to the guest as its legacy floppy drive A:."
} else {
    Write-Host 'The packaged reist-os-floppy.img is active again.'
}
Write-Host "VMX: $VmxPath"
