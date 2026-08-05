param(
    [Parameter(Mandatory = $true)]
    [string]$ImagePath,

    [string]$Drive = "A:"
)

$ErrorActionPreference = "Stop"

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

public static class FloppyNative
{
    public const uint GENERIC_READ = 0x80000000;
    public const uint GENERIC_WRITE = 0x40000000;
    public const uint FILE_SHARE_READ = 0x00000001;
    public const uint FILE_SHARE_WRITE = 0x00000002;
    public const uint OPEN_EXISTING = 3;
    public const uint FILE_FLAG_WRITE_THROUGH = 0x80000000;
    public const uint FSCTL_LOCK_VOLUME = 0x00090018;
    public const uint FSCTL_UNLOCK_VOLUME = 0x0009001c;
    public const uint FSCTL_DISMOUNT_VOLUME = 0x00090020;

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern SafeFileHandle CreateFile(
        string name, uint access, uint share, IntPtr security,
        uint creation, uint flags, IntPtr template);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool DeviceIoControl(
        SafeFileHandle device, uint controlCode,
        IntPtr input, uint inputSize, IntPtr output, uint outputSize,
        out uint bytesReturned, IntPtr overlapped);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool FlushFileBuffers(SafeFileHandle handle);
}
"@

function Invoke-VolumeControl([Microsoft.Win32.SafeHandles.SafeFileHandle]$Handle, [uint32]$Code, [string]$Action) {
    [uint32]$returned = 0
    if (-not [FloppyNative]::DeviceIoControl(
        $Handle, $Code, [IntPtr]::Zero, 0, [IntPtr]::Zero, 0,
        [ref]$returned, [IntPtr]::Zero)) {
        $errorCode = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        throw "$Action failed (Windows error $errorCode). Close all programs using ${Drive}."
    }
}

$expectedSize = 1440KB
$sectorSize = 512
$sectorsPerTrack = 18
$heads = 2
$cylinderSize = $sectorSize * $sectorsPerTrack * $heads
$image = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $ImagePath))
if ($image.Length -ne $expectedSize) {
    throw "The image is $($image.Length) bytes instead of $expectedSize bytes."
}

$devicePath = "\\.\$Drive"
# Windows requires read/write sharing when a volume handle is opened. The
# subsequent FSCTL_LOCK_VOLUME call, rather than CreateFile's share flags,
# provides exclusive access while sectors are changed.
$handle = [FloppyNative]::CreateFile(
    $devicePath,
    [FloppyNative]::GENERIC_READ -bor [FloppyNative]::GENERIC_WRITE,
    [FloppyNative]::FILE_SHARE_READ -bor [FloppyNative]::FILE_SHARE_WRITE,
    [IntPtr]::Zero,
    [FloppyNative]::OPEN_EXISTING,
    [FloppyNative]::FILE_FLAG_WRITE_THROUGH,
    [IntPtr]::Zero)

if ($handle.IsInvalid) {
    $errorCode = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
    throw "Could not open $devicePath exclusively (Windows error $errorCode)."
}

$locked = $false
$stream = $null
try {
    # Lock and dismount before writing so neither Explorer nor the filesystem
    # cache can keep using an obsolete FAT directory while sectors change.
    Invoke-VolumeControl $handle ([FloppyNative]::FSCTL_LOCK_VOLUME) "Locking the volume"
    $locked = $true
    Invoke-VolumeControl $handle ([FloppyNative]::FSCTL_DISMOUNT_VOLUME) "Dismounting the volume"

    $stream = [IO.FileStream]::new(
        $handle, [IO.FileAccess]::ReadWrite, $cylinderSize, $false)
    Write-Host "Writing 1,474,560 bytes directly to $devicePath ..."
    $stream.Position = 0
    # A 1.44-MB disk has 18 sectors per track and two heads. Sending one
    # complete cylinder per request lets the Windows floppy driver perform
    # efficient multi-sector transfers instead of 2,880 synchronous writes.
    for ($offset = 0; $offset -lt $image.Length; $offset += $cylinderSize) {
        $count = [Math]::Min($cylinderSize, $image.Length - $offset)
        $stream.Write($image, $offset, $count)
        $completed = $offset + $count
        Write-Progress -Activity "Writing floppy" -Status "$completed of $($image.Length) bytes" -PercentComplete (($completed * 100) / $image.Length)
    }
    # FILE_FLAG_WRITE_THROUGH plus FlushFileBuffers forces the controller to
    # receive the data; verifying an ordinary cached write would be misleading.
    $stream.Flush()
    if (-not [FloppyNative]::FlushFileBuffers($handle)) {
        $errorCode = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        throw "The physical write could not be flushed (Windows error $errorCode)."
    }
    Write-Progress -Activity "Writing floppy" -Completed

    Write-Host "Reading the physical medium back for verification ..."
    $stream.Position = 0
    $readback = New-Object byte[] $image.Length
    $total = 0
    while ($total -lt $readback.Length) {
        $count = $stream.Read($readback, $total, $readback.Length - $total)
        if ($count -eq 0) { throw "Unexpected end of the floppy at byte $total." }
        $total += $count
    }

    for ($index = 0; $index -lt $image.Length; $index++) {
        if ($image[$index] -ne $readback[$index]) {
            throw "Verification failed at byte $index. The disk may be defective."
        }
    }
    Write-Host "The physical floppy was written and verified successfully."
}
finally {
    if ($null -ne $stream) {
        $stream.Dispose()
    }
    if ($locked) {
        [uint32]$returned = 0
        [void][FloppyNative]::DeviceIoControl(
            $handle, [FloppyNative]::FSCTL_UNLOCK_VOLUME,
            [IntPtr]::Zero, 0, [IntPtr]::Zero, 0,
            [ref]$returned, [IntPtr]::Zero)
    }
    $handle.Dispose()
}
