[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)]
    [string[]]$Source,
    [Parameter(Mandatory)]
    [string]$Output,
    [string]$ElfOutput
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path

$Python = (Get-Command python -ErrorAction Stop).Source
$ZigCommand = Get-Command zig -ErrorAction SilentlyContinue
$Zig = if ($ZigCommand) {
    $ZigCommand.Source
} else {
    'C:\tmp\zig-0.16.0-portable\zig-x86_64-windows-0.16.0\zig.exe'
}
if (-not (Test-Path -LiteralPath $Zig -PathType Leaf)) {
    throw 'Zig was not found. Install Zig or add zig.exe to PATH.'
}

$Arguments = @(
    (Join-Path $RepoRoot 'scripts\build_user_program.py')
) + $Source + @('--output', $Output, '--zig', $Zig)
if ($ElfOutput) { $Arguments += @('--elf-output', $ElfOutput) }

& $Python @Arguments
if ($LASTEXITCODE -ne 0) {
    throw "User program build failed with exit code $LASTEXITCODE."
}
