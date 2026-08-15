[CmdletBinding()]
param(
    [ValidateSet('normal', 'pit', 'watchdog', 'memory', 'arp-reply', 'arp-resolution', 'icmp-echo', 'udp-echo', 'udp-bindings', 'dhcp-config', 'dhcp-expiry', 'dhcp-renewal', 'network-frame', 'network-ipv4-parser', 'network-udp-parser', 'network-dhcp-parser', 'network-udp-ingress', 'storage-recovery', 'storage-io-failure', 'handover')]
    [string]$Mode = 'normal'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$Image = Join-Path $RepoRoot 'build\reist-os.img'
$Runner = Join-Path $RepoRoot 'scripts\run_qemu_smoke.py'
$LogRoot = Join-Path $RepoRoot 'build\codex-agent'

function Resolve-NativeTool {
    param([string]$Name, [string[]]$Fallbacks)
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    foreach ($candidate in $Fallbacks) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    throw "Required tool '$Name' was not found."
}

$Python = Resolve-NativeTool 'python' @(
    'C:\Python314\python.exe',
    'C:\Python313\python.exe'
)
$Qemu = Resolve-NativeTool 'qemu-system-i386' @(
    'C:\tmp\qemu-portable\qemu-system-i386.exe',
    'C:\Program Files\qemu\qemu-system-i386.exe',
    'C:\msys64\mingw64\bin\qemu-system-i386.exe'
)

if (!(Test-Path -LiteralPath $Image -PathType Leaf)) {
    throw 'build\reist-os.img is missing; run build-windows.ps1 first.'
}

function Invoke-Smoke(
    [string]$LogName,
    [string[]]$Extra,
    [bool]$ExpectProbe = $true
) {
    New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $gateLog = Join-Path $LogRoot "$stamp-runtime-$LogName"
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    $arguments = @(
        $Runner,
        '--qemu', $Qemu,
        '--image', $Image,
        '--log', (Join-Path $RepoRoot "build\$LogName")
    )
    if ($ExpectProbe) { $arguments += '--expect-reist-probe' }
    $arguments += $Extra
    $exitCode = 0
    try {
        $LASTEXITCODE = 0
        & $Python @arguments *> $gateLog
        $exitCode = if ($null -eq $LASTEXITCODE) { 1 } else { $LASTEXITCODE }
    }
    catch {
        $exitCode = 1
        $_ | Out-String | Add-Content -LiteralPath $gateLog
    }
    finally {
        $watch.Stop()
    }
    if ($exitCode -eq 0 -and
        (Select-String -LiteralPath $gateLog -SimpleMatch 'guest-smoke: FAIL' `
            -Quiet)) {
        $exitCode = 1
    }
    if ($exitCode -ne 0) {
        Write-Output "RUNTIME FAIL exit=$exitCode elapsed=$([int]$watch.Elapsed.TotalSeconds)s log=$gateLog"
        Get-Content -LiteralPath $gateLog -Tail 40
        throw "REIST runtime smoke '$LogName' failed with exit $exitCode."
    }
    Write-Output "RUNTIME PASS elapsed=$([int]$watch.Elapsed.TotalSeconds)s log=$gateLog"
}

switch ($Mode) {
    'normal' {
        Invoke-Smoke 'guest-smoke.log' @()
    }
    'pit' {
        Invoke-Smoke 'guest-smoke-pit.log' @('--no-apic')
    }
    'watchdog' {
        Invoke-Smoke 'guest-smoke-watchdog.log' @('--watchdog')
    }
    'memory' {
        foreach ($memory in @('32M', '64M', '256M', '1024M')) {
            Invoke-Smoke "guest-smoke-memory-$($memory.ToLower()).log" @(
                '--memory', $memory
            )
        }
    }
    'arp-reply' {
        Invoke-Smoke 'guest-smoke-arp-reply.log' @(
            '--nic', 'rtl8139', '--inject-arp-request'
        )
    }
    'arp-resolution' {
        Invoke-Smoke 'guest-smoke-arp-resolution.log' @(
            '--nic', 'rtl8139', '--expect-arp-resolution'
        )
    }
    'icmp-echo' {
        Invoke-Smoke 'guest-smoke-icmp-echo.log' @(
            '--nic', 'rtl8139', '--inject-icmp-echo'
        )
    }
    'udp-echo' {
        Invoke-Smoke 'guest-smoke-udp-echo.log' @(
            '--nic', 'rtl8139', '--inject-udp-echo'
        )
    }
    'udp-bindings' {
        Invoke-Smoke 'guest-smoke-udp-bindings.log' @(
            '--nic', 'rtl8139', '--inject-udp-echo', '--udp-port', '9001'
        )
    }
    'dhcp-config' {
        Invoke-Smoke 'guest-smoke-dhcp-config.log' @(
            '--nic', 'rtl8139', '--expect-dhcp-config'
        )
    }
    'dhcp-expiry' {
        Invoke-Smoke 'guest-smoke-dhcp-expiry.log' @(
            '--nic', 'rtl8139', '--expect-dhcp-expiry'
        ) $false
    }
    'dhcp-renewal' {
        Invoke-Smoke 'guest-smoke-dhcp-renewal.log' @(
            '--nic', 'rtl8139', '--expect-dhcp-renewal'
        ) $false
    }
    'network-frame' {
        Invoke-Smoke 'guest-smoke-network-frame.log' @(
            '--nic', 'rtl8139', '--expect-network-frame'
        )
    }
    'network-ipv4-parser' {
        Invoke-Smoke 'guest-smoke-network-ipv4-parser.log' @(
            '--nic', 'rtl8139', '--expect-network-frame',
            '--expect-network-ipv4'
        )
    }
    'network-udp-parser' {
        Invoke-Smoke 'guest-smoke-network-udp-parser.log' @(
            '--nic', 'rtl8139', '--expect-network-frame',
            '--expect-network-ipv4', '--expect-network-udp',
            '--inject-udp-echo'
        )
    }
    'network-dhcp-parser' {
        Invoke-Smoke 'guest-smoke-network-dhcp-parser.log' @(
            '--nic', 'rtl8139', '--expect-dhcp-config',
            '--expect-network-frame', '--expect-network-ipv4',
            '--expect-network-dhcp'
        )
    }
    'network-udp-ingress' {
        Invoke-Smoke 'guest-smoke-network-udp-ingress.log' @(
            '--nic', 'rtl8139', '--expect-network-frame',
            '--expect-network-ipv4', '--expect-network-udp',
            '--expect-network-udp-ingress', '--inject-udp-echo'
        )
    }
    'storage-recovery' {
        Invoke-Smoke 'guest-smoke-storage-recovery.log' @(
            '--expect-storage-recovery'
        )
    }
    'storage-io-failure' {
        Invoke-Smoke 'guest-smoke-storage-io-failure.log' @(
            '--expect-storage-io-failure'
        )
    }
    'handover' {
        Invoke-Smoke 'guest-smoke-handover.log' @(
            '--expect-handover'
        )
    }
}
