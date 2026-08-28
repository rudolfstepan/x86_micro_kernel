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

$Make = Resolve-NativeTool 'make' @('C:\ProgramData\chocolatey\bin\make.exe')
$Nasm = Resolve-NativeTool 'nasm' @(
    'C:\tmp\nasm-3.02-portable\nasm-3.02\nasm.exe'
)
$Zig = Resolve-NativeTool 'zig' @(
    'C:\tmp\zig-0.16.0-portable\zig-x86_64-windows-0.16.0\zig.exe'
)
$MsysShell = Resolve-NativeTool 'sh' @('C:\msys64\usr\bin\sh.exe')
$Artifact = Join-Path $RepoRoot "$OutputDirectory\x86_64\reist-x86_64-bootstrap.elf"
$UserProbe = Join-Path $RepoRoot "$OutputDirectory\x86_64\reist-x86_64-user-probe.elf"

Push-Location $RepoRoot
try {
    # GNU Make may execute simple recipes directly; make the native MSYS
    # mkdir available just like the production Windows build does.
    $env:Path = "$(Split-Path -Parent $MsysShell);$env:Path"
    & $Make 'x86_64-bootstrap' `
        "OUTPUT_DIR=$($OutputDirectory.Replace('\', '/'))" `
        "SHELL=$(To-MakePath $MsysShell)" `
        "AS=$(To-MakePath $Nasm)" `
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
    $item = Get-Item -LiteralPath $Artifact
    if ($item.Length -le 0 -or $item.Length -gt 2MB) {
        throw "x86_64 bootstrap artifact size is outside the fixed 1..2097152-byte range."
    }
    Write-Host "X86_64_BOOTSTRAP_BUILD_OK path=$Artifact bytes=$($item.Length)"
    Write-Host "X86_64_USER_PROBE_BUILD_OK path=$UserProbe bytes=$($probeItem.Length)"
} finally {
    Pop-Location
}
