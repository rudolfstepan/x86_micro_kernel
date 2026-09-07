[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$Evidence,
    [ValidateRange(20, 120)] [int]$TimeoutSeconds = 120
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$evidenceRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot 'build/codex-agent')) + [IO.Path]::DirectorySeparatorChar
$destination = [IO.Path]::GetFullPath((Join-Path $repoRoot $Evidence))
if (!$destination.StartsWith($evidenceRoot, [StringComparison]::OrdinalIgnoreCase) -or
    (Test-Path -LiteralPath $destination)) {
    throw 'Display evidence must be a fresh directory under build/codex-agent.'
}
# Do not overwrite the user package, its NVRAM, serial evidence or disk.
# Copy only the generated portable VMX and its explicit flat-disk extent.
$source = Join-Path $repoRoot 'build/vmware/reist-os'
$files = @('reist-os.vmx', 'reist-os.vmdk', 'reist-os-flat.vmdk')
foreach ($file in $files) {
    if (!(Test-Path -LiteralPath (Join-Path $source $file) -PathType Leaf)) {
        throw "Missing VMware package artifact: $file"
    }
}
$descriptor = Get-Content -LiteralPath (Join-Path $source 'reist-os.vmdk') -Raw
$vmxText = Get-Content -LiteralPath (Join-Path $source 'reist-os.vmx') -Raw
if ($descriptor -notmatch '(?m)^RW \d+ FLAT "reist-os-flat.vmdk" 0\r?$' -or
    $vmxText -notmatch 'sata0:0.fileName = "reist-os.vmdk"' -or
    $vmxText -notmatch 'serial0.fileName = "vmware-serial.log"') {
    throw 'Unexpected disk or serial layout; refusing to touch another VM resource.'
}
New-Item -ItemType Directory -Path $destination | Out-Null
foreach ($file in $files) {
    Copy-Item -LiteralPath (Join-Path $source $file) -Destination (Join-Path $destination $file)
}
try {
    & (Join-Path $PSScriptRoot 'run_vmware_mouse.ps1') -SourcePackage $destination `
        -GateLog (Join-Path $destination 'gate.log') -TimeoutSeconds $TimeoutSeconds -DisplayModes
} catch {
    "VMWARE DISPLAY MODES FAIL: $($_.Exception.Message)" |
        Add-Content -LiteralPath (Join-Path $destination 'gate.log') -Encoding utf8
    throw
}
