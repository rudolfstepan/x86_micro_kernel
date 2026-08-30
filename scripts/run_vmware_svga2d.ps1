[CmdletBinding()]
param(
    [string]$SourcePackage = '',
    [string]$GateLog = '',
    [ValidateRange(20, 120)] [int]$TimeoutSeconds = 60
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$mouseRunner = Join-Path $PSScriptRoot 'run_vmware_mouse.ps1'
if (!(Test-Path -LiteralPath $mouseRunner -PathType Leaf)) {
    throw 'The bounded VMware interaction runner is missing.'
}

& $mouseRunner -SourcePackage $SourcePackage -GateLog $GateLog `
    -TimeoutSeconds $TimeoutSeconds -SvgaLifecycle
