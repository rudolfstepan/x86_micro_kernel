[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$logRoot = Join-Path $repoRoot 'build\codex-agent\metadata-stack-targeted'

function Resolve-RequiredTool {
    param([string]$Name, [string[]]$Fallbacks)
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    foreach ($candidate in $Fallbacks) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    throw "Required metadata-stack test tool '$Name' was not found."
}

$python = Resolve-RequiredTool 'python' @(
    'C:\Users\oe3sr\AppData\Local\Programs\Python\Python314\python.exe',
    'C:\Users\oe3sr\AppData\Local\Programs\Python\Python313\python.exe'
)
$gcc = Resolve-RequiredTool 'gcc' @('C:\msys64\mingw64\bin\gcc.exe')
$gccDirectory = Split-Path -Parent $gcc
$islRuntime = Join-Path $gccDirectory 'libisl-23.dll'
if (-not (Test-Path -LiteralPath $islRuntime -PathType Leaf)) {
    throw "MinGW runtime dependency is missing: $islRuntime"
}

$tests = @(
    'test/test_fs_host.py',
    'test/test_fat12_journal.py',
    'test/test_fat12_stack_safety.py',
    'test/test_reist_storage_recovery.py',
    'test/test_reist_undo_journal.py',
    'test/test_stack_evidence.py',
    'test/test_sync_diagnostics_r13.py',
    'test/test_unicode_normalization.py',
    'test/test_userspace_file_syscalls_source.py',
    'test/test_vmware_mouse.py'
)

$oldPath = $env:PATH
$env:PATH = @(
    $gccDirectory,
    'C:\msys64\usr\bin',
    $oldPath
) -join [IO.Path]::PathSeparator

try {
    New-Item -ItemType Directory -Path $logRoot -Force | Out-Null
    Push-Location -LiteralPath $repoRoot
    try {
        foreach ($test in $tests) {
            $logName = ([IO.Path]::GetFileNameWithoutExtension($test)) + '.log'
            $logPath = Join-Path $logRoot $logName
            $output = @(& $python $test 2>&1)
            $exitCode = $LASTEXITCODE
            [IO.File]::WriteAllLines($logPath, [string[]]$output)
            if ($exitCode -ne 0) {
                $output | Select-Object -Last 40
                throw "Focused test '$test' failed with exit code $exitCode."
            }
            Write-Host "$test : PASS"
        }
    }
    finally {
        Pop-Location
    }
}
finally {
    $env:PATH = $oldPath
}
