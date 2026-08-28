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
    $item = Get-Item -LiteralPath $Artifact
    if ($item.Length -le 0 -or $item.Length -gt 2MB) {
        throw "x86_64 bootstrap artifact size is outside the fixed 1..2097152-byte range."
    }
    Write-Host "X86_64_BOOTSTRAP_BUILD_OK path=$Artifact bytes=$($item.Length)"
} finally {
    Pop-Location
}
