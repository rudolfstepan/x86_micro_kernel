[CmdletBinding()]
param(
    [ValidatePattern('^build(?:[\\/][A-Za-z0-9_.-]+)*$')]
    [string]$OutputDirectory = 'build'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path

function Resolve-NativeTool {
    param(
        [Parameter(Mandatory)] [string]$Name,
        [Parameter(Mandatory)] [string[]]$Fallbacks
    )
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }
    foreach ($candidate in $Fallbacks) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    throw "Required native Windows tool '$Name' was not found."
}

function To-MakePath([string]$Path) {
    return $Path.Replace('\', '/')
}

function Read-Elf64Layout {
    param(
        [Parameter(Mandatory)] [string]$Path,
        [switch]$RequireExecutable,
        [switch]$RequireLinked
    )
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 64 -or $bytes[0] -ne 0x7F -or
        $bytes[1] -ne 0x45 -or $bytes[2] -ne 0x4C -or
        $bytes[3] -ne 0x46 -or $bytes[4] -ne 2 -or $bytes[5] -ne 1) {
        throw "Artifact is not a little-endian ELFCLASS64 file: $Path"
    }
    if ([BitConverter]::ToUInt16($bytes, 18) -ne 62) {
        throw "Artifact is not for EM_X86_64: $Path"
    }
    if ($RequireExecutable -and [BitConverter]::ToUInt16($bytes, 16) -ne 2) {
        throw "Bootstrap is not an ELF64 ET_EXEC artifact."
    }

    $programOffset = [BitConverter]::ToUInt64($bytes, 32)
    $programEntrySize = [BitConverter]::ToUInt16($bytes, 54)
    $programCount = [BitConverter]::ToUInt16($bytes, 56)
    if ($programCount -gt 32 -or ($programCount -gt 0 -and $programEntrySize -lt 56)) {
        throw "ELF64 program-header table is outside its fixed bounds."
    }
    for ($index = 0; $index -lt $programCount; ++$index) {
        $offset = $programOffset + ($index * $programEntrySize)
        if ($offset + 56 -gt $bytes.Length) {
            throw "ELF64 program-header table exceeds the artifact."
        }
        $type = [BitConverter]::ToUInt32($bytes, [int]$offset)
        $flags = [BitConverter]::ToUInt32($bytes, [int]$offset + 4)
        if ($type -eq 1 -and ($flags -band 3) -eq 3) {
            throw "Bootstrap contains a writable-executable PT_LOAD segment."
        }
    }

    $sectionOffset = [BitConverter]::ToUInt64($bytes, 40)
    $sectionEntrySize = [BitConverter]::ToUInt16($bytes, 58)
    $sectionCount = [BitConverter]::ToUInt16($bytes, 60)
    $stringIndex = [BitConverter]::ToUInt16($bytes, 62)
    if ($sectionCount -eq 0 -or $sectionCount -gt 128 -or
        $sectionEntrySize -lt 64 -or $stringIndex -ge $sectionCount) {
        throw "ELF64 section-header table is outside its fixed bounds."
    }
    if ($sectionOffset + ($sectionCount * $sectionEntrySize) -gt $bytes.Length) {
        throw "ELF64 section-header table exceeds the artifact."
    }
    $stringHeader = $sectionOffset + ($stringIndex * $sectionEntrySize)
    $stringOffset = [BitConverter]::ToUInt64($bytes, [int]$stringHeader + 24)
    $stringSize = [BitConverter]::ToUInt64($bytes, [int]$stringHeader + 32)
    if ($stringOffset + $stringSize -gt $bytes.Length) {
        throw "ELF64 section-name table exceeds the artifact."
    }

    for ($index = 1; $index -lt $sectionCount; ++$index) {
        $offset = $sectionOffset + ($index * $sectionEntrySize)
        $nameOffset = [BitConverter]::ToUInt32($bytes, [int]$offset)
        $type = [BitConverter]::ToUInt32($bytes, [int]$offset + 4)
        $flags = [BitConverter]::ToUInt64($bytes, [int]$offset + 8)
        $size = [BitConverter]::ToUInt64($bytes, [int]$offset + 32)
        if ($nameOffset -ge $stringSize) {
            throw "ELF64 section name is outside the string table."
        }
        $nameStart = [int]($stringOffset + $nameOffset)
        $nameEnd = $nameStart
        $nameLimit = [int]($stringOffset + $stringSize)
        while ($nameEnd -lt $nameLimit -and $bytes[$nameEnd] -ne 0) {
            ++$nameEnd
        }
        if ($nameEnd -eq $nameLimit) {
            throw "ELF64 section name is not terminated."
        }
        $name = [Text.Encoding]::ASCII.GetString($bytes, $nameStart,
                                                 $nameEnd - $nameStart)
        if (($flags -band 7) -eq 7) {
            throw "ELF64 section '$name' is writable and executable."
        }
        if ($RequireLinked -and ($type -eq 4 -or $type -eq 9) -and $size -ne 0) {
            throw "Final ELF64 artifact retains relocation section '$name'."
        }
        if ($RequireLinked -and $type -eq 6 -and $size -ne 0) {
            throw "Final ELF64 artifact contains dynamic-link state."
        }
        if ($name -match '^\.(?:eh_frame|gcc_except_table|init_array|fini_array)') {
            throw "Forbidden C runtime section remains: $name"
        }
        if ($RequireLinked -and $type -eq 2 -and $size -ne 0) {
            $entrySize = [BitConverter]::ToUInt64($bytes, [int]$offset + 56)
            $dataOffset = [BitConverter]::ToUInt64($bytes, [int]$offset + 24)
            if ($entrySize -lt 24 -or $dataOffset + $size -gt $bytes.Length) {
                throw "ELF64 symbol table is malformed."
            }
            for ($symbol = 1; $symbol -lt ($size / $entrySize); ++$symbol) {
                $symbolOffset = $dataOffset + ($symbol * $entrySize)
                if ([BitConverter]::ToUInt16($bytes, [int]$symbolOffset + 6) -eq 0) {
                    throw "Final ELF64 artifact retains an undefined symbol."
                }
            }
        }
    }
    return ,$bytes
}

$Make = Resolve-NativeTool 'make' @('C:\ProgramData\chocolatey\bin\make.exe')
$Nasm = Resolve-NativeTool 'nasm' @(
    'C:\tmp\nasm-3.02-portable\nasm-3.02\nasm.exe'
)
$Zig = Resolve-NativeTool 'zig' @(
    'C:\tmp\zig-0.16.0-portable\zig-x86_64-windows-0.16.0\zig.exe'
)
$Objcopy = Resolve-NativeTool 'objcopy' @('C:\msys64\mingw64\bin\objcopy.exe')
$MsysShell = Resolve-NativeTool 'sh' @('C:\msys64\usr\bin\sh.exe')
$Artifact = Join-Path $RepoRoot "$OutputDirectory\x86_64\reist-x86_64-bootstrap.elf"
$UserProbe = Join-Path $RepoRoot "$OutputDirectory\x86_64\reist-x86_64-user-probe.elf"
$UserShell = Join-Path $RepoRoot "$OutputDirectory\x86_64\reist-x86_64-user-shell.elf"
$UserShellObject = Join-Path $RepoRoot "$OutputDirectory\x86_64\user_shell.o"
$CObject = Join-Path $RepoRoot "$OutputDirectory\x86_64\bootstrap_core.o"
$CElf = Join-Path $RepoRoot "$OutputDirectory\x86_64\reist-x86_64-c-core.elf"
$CText = Join-Path $RepoRoot "$OutputDirectory\x86_64\bootstrap_core_text.bin"
$CRodata = Join-Path $RepoRoot "$OutputDirectory\x86_64\bootstrap_core_rodata.bin"
$CData = Join-Path $RepoRoot "$OutputDirectory\x86_64\bootstrap_core_data.bin"

Push-Location $RepoRoot
try {
    # GNU Make may execute simple recipes directly; make the native MSYS
    # mkdir available just like the production Windows build does.
    $env:Path = "$(Split-Path -Parent $MsysShell);$env:Path"
    & $Make 'x86_64-bootstrap' `
        "OUTPUT_DIR=$($OutputDirectory.Replace('\', '/'))" `
        "SHELL=$(To-MakePath $MsysShell)" `
        "AS=$(To-MakePath $Nasm)" `
        "OBJCOPY=$(To-MakePath $Objcopy)" `
        "X86_64_CC=$(To-MakePath $Zig) cc" `
        "LD=$(To-MakePath $Zig) ld.lld"
    if ($LASTEXITCODE -ne 0) {
        throw "x86_64 bootstrap build failed with exit code $LASTEXITCODE."
    }
    if (-not (Test-Path -LiteralPath $Artifact -PathType Leaf)) {
        throw "x86_64 bootstrap artifact was not produced: $Artifact"
    }
    if (-not (Test-Path -LiteralPath $UserProbe -PathType Leaf)) {
        throw "x86_64 ELF64 user probe was not produced: $UserProbe"
    }
    if (-not (Test-Path -LiteralPath $UserShell -PathType Leaf) -or
        -not (Test-Path -LiteralPath $UserShellObject -PathType Leaf)) {
        throw "x86_64 ELF64 Ring-3 shell artifacts were not produced."
    }
    if (-not (Test-Path -LiteralPath $CObject -PathType Leaf)) {
        throw "x86_64 freestanding C object was not produced: $CObject"
    }
    if (-not (Test-Path -LiteralPath $CElf -PathType Leaf)) {
        throw "x86_64 linked C payload was not produced: $CElf"
    }
    foreach ($payload in @($CText, $CRodata, $CData)) {
        if (-not (Test-Path -LiteralPath $payload -PathType Leaf)) {
            throw "x86_64 C payload section was not produced: $payload"
        }
    }
    $probeItem = Get-Item -LiteralPath $UserProbe
    if ($probeItem.Length -lt 64 -or $probeItem.Length -gt 64KB) {
        throw "x86_64 ELF64 user probe size is outside the fixed 64..65536-byte range."
    }
    $probeMagic = ([System.IO.File]::ReadAllBytes($UserProbe))[0..4]
    if ($probeMagic[0] -ne 0x7F -or $probeMagic[1] -ne 0x45 -or
        $probeMagic[2] -ne 0x4C -or $probeMagic[3] -ne 0x46 -or
        $probeMagic[4] -ne 0x02) {
        throw "x86_64 user probe is not an ELFCLASS64 artifact."
    }
    $shellItem = Get-Item -LiteralPath $UserShell
    if ($shellItem.Length -lt 64 -or $shellItem.Length -gt 4096) {
        throw "x86_64 ELF64 Ring-3 shell exceeds its fixed compact page."
    }
    $item = Get-Item -LiteralPath $Artifact
    if ($item.Length -le 0 -or $item.Length -gt 2MB) {
        throw "x86_64 bootstrap artifact size is outside the fixed 1..2097152-byte range."
    }
    $artifactBytes = [System.IO.File]::ReadAllBytes($Artifact)
    if ($artifactBytes.Length -lt 52 -or $artifactBytes[4] -ne 1 -or
        [BitConverter]::ToUInt16($artifactBytes, 16) -ne 2 -or
        [BitConverter]::ToUInt16($artifactBytes, 18) -ne 3) {
        throw "Multiboot bootstrap is not an ELF32 EM_386 ET_EXEC container."
    }
    $cElfBytes = Read-Elf64Layout -Path $CElf -RequireExecutable -RequireLinked
    $objectBytes = Read-Elf64Layout -Path $CObject
    $shellBytes = Read-Elf64Layout -Path $UserShell -RequireExecutable -RequireLinked
    $shellObjectBytes = Read-Elf64Layout -Path $UserShellObject
    $objectText = [Text.Encoding]::ASCII.GetString($objectBytes)
    $shellObjectText = [Text.Encoding]::ASCII.GetString($shellObjectBytes)
    foreach ($forbidden in @('__stack_chk', 'memcpy', 'memset', 'memmove',
                              '_Unwind', '__cxa_', 'malloc', 'free')) {
        if ($objectText.Contains($forbidden)) {
            throw "x86_64 C object references forbidden runtime symbol '$forbidden'."
        }
        if ($shellObjectText.Contains($forbidden)) {
            throw "x86_64 shell object references forbidden runtime symbol '$forbidden'."
        }
    }
    $cTextLength = (Get-Item -LiteralPath $CText).Length
    $cRodataLength = (Get-Item -LiteralPath $CRodata).Length
    $cDataLength = (Get-Item -LiteralPath $CData).Length
    if ($cElfBytes.Length -gt 64KB -or $cTextLength -le 0 -or
        $cTextLength -gt 4096 -or $cRodataLength -le 0 -or
        $cRodataLength -gt 4096 -or $cDataLength -ne 32) {
        throw "x86_64 C payload sections exceed their fixed page or ABI bounds."
    }
    Write-Host "X86_64_BOOTSTRAP_BUILD_OK path=$Artifact bytes=$($item.Length)"
    Write-Host "X86_64_USER_PROBE_BUILD_OK path=$UserProbe bytes=$($probeItem.Length)"
    Write-Host "X86_64_USER_SHELL_BUILD_OK path=$UserShell bytes=$($shellBytes.Length)"
    Write-Host "X86_64_C_CORE_BUILD_OK path=$CObject bytes=$($objectBytes.Length)"
    Write-Host "X86_64_C_PAYLOAD_BUILD_OK path=$CElf bytes=$($cElfBytes.Length)"
} finally {
    Pop-Location
}
