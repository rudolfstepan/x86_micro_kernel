[CmdletBinding()]
param(
    [ValidateRange(1, 6)]
    [int]$MaxPackages = 6,
    [ValidateRange(1, 14400)]
    [int]$PackageTimeoutSeconds = 600,
    [switch]$DryRun
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$Python = (Get-Command python -ErrorAction Stop).Source
$arguments = @(
    (Join-Path $PSScriptRoot 'run_reist_autonomous.py'),
    '--repo', $RepoRoot,
    '--max-packages', [string]$MaxPackages,
    '--package-timeout-seconds', [string]$PackageTimeoutSeconds
)
if ($DryRun) { $arguments += '--dry-run' }

& $Python @arguments
exit $LASTEXITCODE
