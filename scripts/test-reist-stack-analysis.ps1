[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path

function Resolve-RequiredTool {
    param([string]$Name, [string[]]$Fallbacks)
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    foreach ($candidate in $Fallbacks) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    throw "Required stack-analysis tool '$Name' was not found."
}

$python = Resolve-RequiredTool 'python' @(
    'C:\Users\oe3sr\AppData\Local\Programs\Python\Python314\python.exe',
    'C:\Users\oe3sr\AppData\Local\Programs\Python\Python313\python.exe'
)
$make = Resolve-RequiredTool 'make' @('C:\msys64\usr\bin\make.exe')
$gcc = Resolve-RequiredTool 'gcc' @('C:\msys64\mingw64\bin\gcc.exe')
$gccDirectory = Split-Path -Parent $gcc
$islRuntime = Join-Path $gccDirectory 'libisl-23.dll'
if (-not (Test-Path -LiteralPath $islRuntime -PathType Leaf)) {
    throw "MinGW runtime dependency is missing: $islRuntime"
}
$oldPath = $env:PATH
$env:PATH = @(
    (Split-Path -Parent $make),
    $gccDirectory,
    $oldPath
) -join [IO.Path]::PathSeparator

try {
    Push-Location -LiteralPath $repoRoot
    try {
        $gccArgument = 'STACK_ANALYSIS_CC=' + $gcc.Replace('\', '/')
        $pythonArgument = 'PYTHON=' + $python.Replace('\', '/')
        & $make check-kernel-stack-analysis $gccArgument $pythonArgument `
            'STACK_ANALYSIS_OUTPUT_DIR=build/stack-analysis'
        if ($LASTEXITCODE -ne 0) {
            throw "Kernel stack analysis failed with exit code $LASTEXITCODE."
        }
    }
    finally {
        Pop-Location
    }
}
finally {
    $env:PATH = $oldPath
}
