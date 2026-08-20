[CmdletBinding()]
param(
    [ValidateSet('normal', 'pit', 'watchdog', 'memory', 'arp-reply', 'arp-resolution', 'icmp-echo', 'udp-echo', 'udp-bindings', 'dhcp-config', 'dhcp-expiry', 'dhcp-renewal', 'network-frame', 'network-ipv4-parser', 'network-icmp-parser', 'network-udp-parser', 'network-dhcp-parser', 'network-udp-ingress', 'storage-recovery', 'storage-io-failure', 'fdd-hotplug', 'sata-hotplug', 'admin-maintenance', 'component-control', 'driver-domain', 'system-layout', 'partition-provisioning', 'partition-full-format', 'handover', 'runtime-desktop', 'runtime-desktop-metrics', 'runtime-desktop-vbe', 'runtime-desktop-vbe-failure')]
    [string]$Mode = 'normal'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$Image = Join-Path $RepoRoot 'build\reist-os.img'
$Runner = Join-Path $RepoRoot 'scripts\run_qemu_smoke.py'
$RuntimeDesktopRunner = Join-Path $RepoRoot 'scripts\run_qemu_runtime_desktop.py'
$BuildScript = Join-Path $RepoRoot 'scripts\build-windows.ps1'
$FddHotplugRunner = Join-Path $RepoRoot 'scripts\run_qemu_fdd_hotplug.py'
$SataHotplugRunner = Join-Path $RepoRoot 'scripts\run_qemu_sata_hotplug.py'
$AdminMaintenanceRunner = Join-Path $RepoRoot 'scripts\run_qemu_admin_maintenance.py'
$ComponentControlRunner = Join-Path $RepoRoot 'scripts\run_qemu_component_control.py'
$DriverDomainRunner = Join-Path $RepoRoot 'scripts\run_qemu_driver_domain.py'
$SystemLayoutRunner = Join-Path $RepoRoot 'scripts\run_qemu_system_layout.py'
$PartitionProvisioningRunner = Join-Path $RepoRoot 'scripts\run_qemu_partition_provisioning.py'
$LogRoot = Join-Path $RepoRoot 'build\codex-agent'

function Resolve-NativeTool {
    param([string]$Name, [string[]]$Fallbacks)
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($Name -eq 'qemu-system-i386' -and $command -and
        $command.Source -match '(?i)[\\/]android-sdk[\\/]emulator[\\/]qemu[\\/]') {
        $command = $null
    }
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
$Make = if ($Mode -eq 'driver-domain') {
    Resolve-NativeTool 'make' @(
        'C:\msys64\usr\bin\make.exe',
        'C:\msys64\mingw64\bin\mingw32-make.exe'
    )
} else { $null }

if ($Mode -ne 'driver-domain' -and
    !(Test-Path -LiteralPath $Image -PathType Leaf)) {
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

function Invoke-RuntimeDesktop(
    [bool]$ExpectFailure = $false,
    [bool]$RenderProbe = $false
) {
    if ($ExpectFailure -and $RenderProbe) {
        throw 'Runtime desktop failure and render probe modes are exclusive.'
    }
    $screenshot = Join-Path $RepoRoot 'build\runtime-desktop.ppm'
    $arguments = @('--qemu', $Qemu, '--image', $Image,
        '--screenshot', $screenshot)
    if ($ExpectFailure) { $arguments += '--expect-failure' }
    if ($RenderProbe) {
        New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null
        $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
        $metricsLog = Join-Path $LogRoot "$stamp-runtime-desktop-metrics.log"
        $arguments += @('--render-probe', '--metrics-log', $metricsLog)
    }
    & $Python $RuntimeDesktopRunner @arguments
    if ($ExpectFailure) {
        if ($LASTEXITCODE -eq 0) { throw 'Runtime graphics failure was not rejected.' }
    } elseif ($LASTEXITCODE -ne 0) {
        throw 'Runtime desktop activation failed.'
    }
}

function Invoke-FddHotplug {
    New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $gateLog = Join-Path $LogRoot "$stamp-runtime-fdd-hotplug"
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    $exitCode = 0
    try {
        $LASTEXITCODE = 0
        & $Python $FddHotplugRunner `
            --qemu $Qemu `
            --image $Image `
            --floppy (Join-Path $RepoRoot 'build\fdd-hotplug.img') `
            --log (Join-Path $RepoRoot 'build\guest-smoke-fdd-hotplug.log') `
            *> $gateLog
        $exitCode = if ($null -eq $LASTEXITCODE) { 1 } else { $LASTEXITCODE }
    }
    catch {
        $exitCode = 1
        $_ | Out-String | Add-Content -LiteralPath $gateLog
    }
    finally {
        $watch.Stop()
    }
    if ($exitCode -ne 0) {
        Write-Output "RUNTIME FAIL exit=$exitCode elapsed=$([int]$watch.Elapsed.TotalSeconds)s log=$gateLog"
        Get-Content -LiteralPath $gateLog -Tail 40
        throw "REIST runtime smoke 'fdd-hotplug' failed with exit $exitCode."
    }
    Write-Output "RUNTIME PASS elapsed=$([int]$watch.Elapsed.TotalSeconds)s log=$gateLog"
}

function Invoke-SataHotplug {
    New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $gateLog = Join-Path $LogRoot "$stamp-runtime-sata-hotplug"
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    $exitCode = 0
    try {
        $LASTEXITCODE = 0
        & $Python $SataHotplugRunner `
            --qemu $Qemu `
            --image $Image `
            --disk (Join-Path $RepoRoot 'build\sata-hotplug.img') `
            --log (Join-Path $RepoRoot 'build\guest-smoke-sata-hotplug.log') `
            *> $gateLog
        $exitCode = if ($null -eq $LASTEXITCODE) { 1 } else { $LASTEXITCODE }
    }
    catch {
        $exitCode = 1
        $_ | Out-String | Add-Content -LiteralPath $gateLog
    }
    finally {
        $watch.Stop()
    }
    if ($exitCode -ne 0) {
        Write-Output "RUNTIME FAIL exit=$exitCode elapsed=$([int]$watch.Elapsed.TotalSeconds)s log=$gateLog"
        Get-Content -LiteralPath $gateLog -Tail 40
        throw "REIST runtime smoke 'sata-hotplug' failed with exit $exitCode."
    }
    Write-Output "RUNTIME PASS elapsed=$([int]$watch.Elapsed.TotalSeconds)s log=$gateLog"
}

function Invoke-AdminMaintenance {
    New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $gateLog = Join-Path $LogRoot "$stamp-runtime-admin-maintenance"
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    $exitCode = 0
    try {
        $LASTEXITCODE = 0
        & $Python $AdminMaintenanceRunner `
            --qemu $Qemu `
            --image $Image `
            --disk (Join-Path $RepoRoot 'build\admin-maintenance.img') `
            --floppy (Join-Path $RepoRoot 'build\admin-maintenance-fdd.img') `
            --log (Join-Path $RepoRoot 'build\guest-admin-maintenance.log') `
            *> $gateLog
        $exitCode = if ($null -eq $LASTEXITCODE) { 1 } else { $LASTEXITCODE }
    }
    catch {
        $exitCode = 1
        $_ | Out-String | Add-Content -LiteralPath $gateLog
    }
    finally {
        $watch.Stop()
    }
    if ($exitCode -ne 0) {
        Write-Output "RUNTIME FAIL exit=$exitCode elapsed=$([int]$watch.Elapsed.TotalSeconds)s log=$gateLog"
        Get-Content -LiteralPath $gateLog -Tail 40
        throw "REIST runtime smoke 'admin-maintenance' failed with exit $exitCode."
    }
    Write-Output "RUNTIME PASS elapsed=$([int]$watch.Elapsed.TotalSeconds)s log=$gateLog"
}

function Invoke-ComponentControl {
    New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $gateLog = Join-Path $LogRoot "$stamp-runtime-component-control"
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    $exitCode = 0
    try {
        $LASTEXITCODE = 0
        & $Python $ComponentControlRunner `
            --qemu $Qemu `
            --image $Image `
            --log (Join-Path $RepoRoot 'build\guest-component-control.log') `
            *> $gateLog
        $exitCode = if ($null -eq $LASTEXITCODE) { 1 } else { $LASTEXITCODE }
    }
    catch {
        $exitCode = 1
        $_ | Out-String | Add-Content -LiteralPath $gateLog
    }
    finally {
        $watch.Stop()
    }
    if ($exitCode -ne 0) {
        Write-Output "RUNTIME FAIL exit=$exitCode elapsed=$([int]$watch.Elapsed.TotalSeconds)s log=$gateLog"
        Get-Content -LiteralPath $gateLog -Tail 40
        throw "REIST runtime smoke 'component-control' failed with exit $exitCode."
    }
    Write-Output "RUNTIME PASS elapsed=$([int]$watch.Elapsed.TotalSeconds)s log=$gateLog"
}

function Invoke-DriverDomain {
    New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $gateLog = Join-Path $LogRoot "$stamp-runtime-driver-domain"
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    $exitCode = 0
    try {
        $LASTEXITCODE = 0
        & $Make native-image TARGET=qemu VIDEO=vga `
            DRIVER_DOMAIN_FAULT_INJECTION=1 *> $gateLog
        $exitCode = if ($null -eq $LASTEXITCODE) { 1 } else { $LASTEXITCODE }
        if ($exitCode -eq 0) {
            & $Python $DriverDomainRunner `
                --qemu $Qemu `
                --image $Image `
                --log (Join-Path $RepoRoot 'build\guest-driver-domain.log') `
                *>> $gateLog
            $exitCode = if ($null -eq $LASTEXITCODE) { 1 } else { $LASTEXITCODE }
        }
    }
    catch {
        $exitCode = 1
        $_ | Out-String | Add-Content -LiteralPath $gateLog
    }
    finally {
        $watch.Stop()
    }
    if ($exitCode -ne 0) {
        Write-Output "RUNTIME FAIL exit=$exitCode elapsed=$([int]$watch.Elapsed.TotalSeconds)s log=$gateLog"
        Get-Content -LiteralPath $gateLog -Tail 40
        throw "REIST runtime smoke 'driver-domain' failed with exit $exitCode."
    }
    Write-Output "RUNTIME PASS elapsed=$([int]$watch.Elapsed.TotalSeconds)s log=$gateLog"
}

function Invoke-SystemLayout {
    New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $gateLog = Join-Path $LogRoot "$stamp-runtime-system-layout"
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    $exitCode = 0
    try {
        $LASTEXITCODE = 0
        & $Python $SystemLayoutRunner `
            --qemu $Qemu `
            --image $Image `
            --log (Join-Path $RepoRoot 'build\guest-system-layout.log') `
            *> $gateLog
        $exitCode = if ($null -eq $LASTEXITCODE) { 1 } else { $LASTEXITCODE }
    }
    catch {
        $exitCode = 1
        $_ | Out-String | Add-Content -LiteralPath $gateLog
    }
    finally {
        $watch.Stop()
    }
    if ($exitCode -ne 0) {
        Write-Output "RUNTIME FAIL exit=$exitCode elapsed=$([int]$watch.Elapsed.TotalSeconds)s log=$gateLog"
        Get-Content -LiteralPath $gateLog -Tail 40
        throw "REIST runtime smoke 'system-layout' failed with exit $exitCode."
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
    'network-icmp-parser' {
        Invoke-Smoke 'guest-smoke-network-icmp-parser.log' @(
            '--nic', 'rtl8139', '--expect-network-frame',
            '--expect-network-ipv4', '--expect-network-icmp',
            '--inject-icmp-echo'
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
    'fdd-hotplug' {
        Invoke-FddHotplug
    }
    'sata-hotplug' {
        Invoke-SataHotplug
    }
    'admin-maintenance' {
        Invoke-AdminMaintenance
    }
    'component-control' {
        Invoke-ComponentControl
    }
    'driver-domain' {
        Invoke-DriverDomain
    }
    'system-layout' {
        Invoke-SystemLayout
    }
    'partition-provisioning' {
        $disk = Join-Path $RepoRoot 'build\partition-provisioning.img'
        & $Python $PartitionProvisioningRunner --qemu $Qemu --image $Image --disk $disk --log (Join-Path $RepoRoot 'build\guest-partition-provisioning.log')
        if ($LASTEXITCODE -ne 0) { throw "REIST partition provisioning runtime failed." }
    }
    'partition-full-format' {
        $disk = Join-Path $RepoRoot 'build\partition-full-format.img'
        & $Python $PartitionProvisioningRunner --qemu $Qemu --image $Image --disk $disk --log (Join-Path $RepoRoot 'build\guest-partition-full-format.log') --format-mode full --timeout 600
        if ($LASTEXITCODE -ne 0) { throw "REIST partition full-format runtime failed." }
    }
    'handover' {
        Invoke-Smoke 'guest-smoke-handover.log' @(
            '--expect-handover'
        )
    }
    'runtime-desktop' {
        Invoke-RuntimeDesktop
    }
    'runtime-desktop-metrics' {
        Invoke-RuntimeDesktop $false $true
    }
    'runtime-desktop-vbe' {
        & $BuildScript -Target qemu -Video vga -VbeRuntimeTest
        if ($LASTEXITCODE -ne 0) { throw 'VBE runtime test build failed.' }
        Invoke-RuntimeDesktop
    }
    'runtime-desktop-vbe-failure' {
        Invoke-RuntimeDesktop $true
    }
}
