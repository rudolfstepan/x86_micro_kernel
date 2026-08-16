[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateRange(0, 255)]
    [int]$DiskNumber,

    [Parameter(Mandatory = $true)]
    [string]$ExpectedSerial,

    [Parameter(Mandatory = $true)]
    [UInt64]$ExpectedSize,

    [Parameter(Mandatory = $true)]
    [string]$ImagePath,

    [Parameter(Mandatory = $true)]
    [switch]$ConfirmDestructive
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $ConfirmDestructive) {
    throw 'The destructive confirmation switch is required.'
}

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Administrator privileges are required for a physical disk write.'
}

$image = Get-Item -LiteralPath $ImagePath -ErrorAction Stop
if ($image.Length -le 512 -or ($image.Length % 512) -ne 0) {
    throw "Image size $($image.Length) is not a positive 512-byte sector image."
}
$signature = [IO.File]::ReadAllBytes($image.FullName)[510..511]
if ($signature[0] -ne 0x55 -or $signature[1] -ne 0xAA) {
    throw 'Image has no valid BIOS boot signature.'
}

$disk = Get-Disk -Number $DiskNumber -ErrorAction Stop
$serial = ([string]$disk.SerialNumber).Trim()
if ($serial -ne $ExpectedSerial.Trim() -or
    [UInt64]$disk.Size -ne $ExpectedSize -or
    $disk.IsBoot -or $disk.IsSystem -or $disk.IsReadOnly -or
    $disk.LogicalSectorSize -ne 512 -or
    [UInt64]$image.Length -ge [UInt64]$disk.Size) {
    throw "Target identity or safety check failed for disk $DiskNumber."
}

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

public static class PhysicalDiskNative
{
    public const uint GENERIC_READ = 0x80000000;
    public const uint GENERIC_WRITE = 0x40000000;
    public const uint FILE_SHARE_READ = 0x00000001;
    public const uint FILE_SHARE_WRITE = 0x00000002;
    public const uint OPEN_EXISTING = 3;
    public const uint FILE_FLAG_WRITE_THROUGH = 0x80000000;

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern SafeFileHandle CreateFile(
        string name, uint access, uint share, IntPtr security,
        uint creation, uint flags, IntPtr template);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool FlushFileBuffers(SafeFileHandle handle);
}
'@

$devicePath = "\\.\PhysicalDrive$DiskNumber"
$wasOffline = [bool]$disk.IsOffline
if (-not $wasOffline) {
    Set-Disk -Number $DiskNumber -IsOffline $true
}

$handle = $null
$device = $null
$source = $null
try {
    $handle = [PhysicalDiskNative]::CreateFile(
        $devicePath,
        [PhysicalDiskNative]::GENERIC_READ -bor
            [PhysicalDiskNative]::GENERIC_WRITE,
        [PhysicalDiskNative]::FILE_SHARE_READ -bor
            [PhysicalDiskNative]::FILE_SHARE_WRITE,
        [IntPtr]::Zero,
        [PhysicalDiskNative]::OPEN_EXISTING,
        [PhysicalDiskNative]::FILE_FLAG_WRITE_THROUGH,
        [IntPtr]::Zero)
    if ($handle.IsInvalid) {
        $code = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        throw "Cannot open $devicePath (Windows error $code)."
    }

    $device = [IO.FileStream]::new(
        $handle, [IO.FileAccess]::ReadWrite, 1MB, $false)
    $source = [IO.File]::OpenRead($image.FullName)
    $buffer = New-Object byte[] 1MB
    [Int64]$written = 0
    while (($count = $source.Read($buffer, 0, $buffer.Length)) -gt 0) {
        $device.Write($buffer, 0, $count)
        $written += $count
        Write-Progress -Activity 'Writing REIST OS' -Status "$written bytes" `
            -PercentComplete (($written * 100) / $image.Length)
    }

    # Remove the stale secondary GPT header and entry array from the old disk.
    $tailBytes = 34 * 512
    $zeros = New-Object byte[] $tailBytes
    $device.Position = [Int64]$disk.Size - $tailBytes
    $device.Write($zeros, 0, $zeros.Length)
    $device.Flush()
    if (-not [PhysicalDiskNative]::FlushFileBuffers($handle)) {
        $code = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        throw "Physical flush failed (Windows error $code)."
    }
    Write-Progress -Activity 'Writing REIST OS' -Completed

    $source.Position = 0
    $device.Position = 0
    [Int64]$verified = 0
    $expected = New-Object byte[] 1MB
    $actual = New-Object byte[] 1MB
    while ($verified -lt $image.Length) {
        $wanted = [Math]::Min($expected.Length, $image.Length - $verified)
        $expectedCount = $source.Read($expected, 0, $wanted)
        $actualCount = $device.Read($actual, 0, $wanted)
        if ($expectedCount -ne $wanted -or $actualCount -ne $wanted) {
            throw "Short verification read at byte $verified."
        }
        for ($index = 0; $index -lt $wanted; ++$index) {
            if ($expected[$index] -ne $actual[$index]) {
                throw "Verification mismatch at byte $($verified + $index)."
            }
        }
        $verified += $wanted
    }
    Write-Host "Verified $verified image bytes on $devicePath."
    Write-Host 'The disk remains offline. Disconnect it before booting the target PC.'
}
finally {
    if ($null -ne $source) { $source.Dispose() }
    if ($null -ne $device) { $device.Dispose() }
    elseif ($null -ne $handle) { $handle.Dispose() }
    if ($wasOffline) {
        # Preserve an already-offline state. A newly offlined target deliberately
        # remains offline so Windows cannot mount the freshly written filesystem.
    }
}
