[CmdletBinding()]
param(
    [ValidateSet('normal', 'boot-integrity', 'boot-control', 'boot-success', 'pit', 'watchdog', 'memory', 'memory-resilience', 'arp-reply', 'arp-resolution', 'icmp-echo', 'udp-echo', 'udp-bindings', 'dhcp-config', 'dhcp-expiry', 'dhcp-renewal', 'network-frame', 'network-ipv4-parser', 'network-icmp-parser', 'network-udp-parser', 'network-dhcp-parser', 'network-udp-ingress', 'curl-client', 'curl-https-client', 'curl-https-public-client', 'http-server', 'storage-recovery', 'storage-service-restart', 'storage-io-failure', 'storage-maintenance', 'storage-reconnect', 'wcet-baseline', 'fdd-hotplug', 'ext2-stat', 'vfs-symbolic-links', 'sata-hotplug', 'admin-maintenance', 'component-control', 'driver-domain', 'system-layout', 'editor-load', 'chkdsk-readonly', 'chkdsk-fat12', 'pci-audio', 'partition-provisioning', 'partition-full-format', 'handover', 'vmware-svga2d', 'vmware-svga2d-lifecycle', 'vmware-mouse', 'vmware-hover-cadence', 'vmware-compositor-restart', 'vmware-benchmark', 'vmware-rename', 'runtime-desktop', 'runtime-desktop-notepad', 'runtime-desktop-notepad-fonts', 'runtime-desktop-control', 'runtime-desktop-trash-restore', 'runtime-desktop-explorer-scroll', 'runtime-desktop-explorer-views', 'runtime-desktop-shortcuts', 'runtime-desktop-metrics', 'runtime-desktop-surface', 'runtime-desktop-hover-cadence', 'runtime-desktop-audio', 'runtime-desktop-guidemo-click', 'runtime-desktop-vbe', 'runtime-desktop-vbe-failure')]
    [string]$Mode = 'normal',
    [ValidateSet('qemu', 'vmware')]
    [string]$Target = 'qemu',
    [ValidateSet('vga', 'framebuffer')]
    [string]$Video = 'vga'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$Image = Join-Path $RepoRoot 'build\reist-os.img'
$Runner = Join-Path $RepoRoot 'scripts\run_qemu_smoke.py'
$BootIntegrityRunner = Join-Path $RepoRoot 'scripts\run_qemu_boot_integrity.py'
$BootControlRunner = Join-Path $RepoRoot 'scripts\run_qemu_boot_control.py'
$BootSuccessRunner = Join-Path $RepoRoot 'scripts\run_qemu_boot_success.py'
$RuntimeDesktopRunner = Join-Path $RepoRoot 'scripts\run_qemu_runtime_desktop.py'
$BuildScript = Join-Path $RepoRoot 'scripts\build-windows.ps1'
$FddHotplugRunner = Join-Path $RepoRoot 'scripts\run_qemu_fdd_hotplug.py'
$Ext2StatRunner = Join-Path $RepoRoot 'scripts\run_qemu_ext2_stat.py'
$Ext2SymlinkRunner = Join-Path $RepoRoot 'scripts\run_qemu_ext2_symlink.py'
$SataHotplugRunner = Join-Path $RepoRoot 'scripts\run_qemu_sata_hotplug.py'
$AdminMaintenanceRunner = Join-Path $RepoRoot 'scripts\run_qemu_admin_maintenance.py'
$ComponentControlRunner = Join-Path $RepoRoot 'scripts\run_qemu_component_control.py'
$DriverDomainRunner = Join-Path $RepoRoot 'scripts\run_qemu_driver_domain.py'
$SystemLayoutRunner = Join-Path $RepoRoot 'scripts\run_qemu_system_layout.py'
$PciAudioRunner = Join-Path $RepoRoot 'scripts\run_qemu_pci_audio.py'
$VmwareSvga2dRunner = Join-Path $RepoRoot 'scripts\run_vmware_svga2d.ps1'
$VmwareMouseRunner = Join-Path $RepoRoot 'scripts\run_vmware_mouse.ps1'
$PartitionProvisioningRunner = Join-Path $RepoRoot 'scripts\run_qemu_partition_provisioning.py'
$LogRoot = Join-Path $RepoRoot 'build\codex-agent'
$WcetBudget = Join-Path $RepoRoot 'safety\wcet_budgets.json'
$KernelImage = Join-Path $RepoRoot 'build\kernel.bin'
$KernelSignature = Join-Path $RepoRoot 'build\kernel.bin.sig'
$BootTrustPolicy = Join-Path $RepoRoot 'safety\boot_trust_policy.json'

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
$OpenSsl = if ($Mode -eq 'boot-control' -or $Mode -eq 'boot-success') {
    Resolve-NativeTool 'openssl' @(
        'C:\msys64\mingw64\bin\openssl.exe'
    )
} else { $null }
$Qemu = if (($Target -eq 'qemu' -and $Mode -ne 'vmware-mouse' -and
    $Mode -ne 'vmware-hover-cadence' -and
    $Mode -ne 'vmware-compositor-restart') -or
    $Mode -eq 'pci-audio') {
    Resolve-NativeTool 'qemu-system-i386' @(
        'C:\tmp\qemu-portable\qemu-system-i386.exe',
        'C:\Program Files\qemu\qemu-system-i386.exe',
        'C:\msys64\mingw64\bin\qemu-system-i386.exe'
    )
} else { $null }
$Make = if ($Mode -eq 'driver-domain') {
    Resolve-NativeTool 'make' @(
        'C:\msys64\usr\bin\make.exe',
        'C:\msys64\mingw64\bin\mingw32-make.exe'
    )
} else { $null }

if ($Target -eq 'qemu' -and $Mode -ne 'driver-domain' -and
    $Mode -ne 'vmware-mouse' -and
    $Mode -ne 'vmware-hover-cadence' -and
    $Mode -ne 'vmware-compositor-restart' -and
    $Mode -ne 'runtime-desktop-hover-cadence' -and
    $Mode -ne 'curl-https-client' -and
    $Mode -ne 'curl-https-public-client' -and
    !(Test-Path -LiteralPath $Image -PathType Leaf)) {
    throw 'build\reist-os.img is missing; run build-windows.ps1 first.'
}

function Invoke-Smoke(
    [string]$LogName,
    [string[]]$Extra,
    [bool]$ExpectProbe = $true,
    [string]$ImagePath = $Image
) {
    New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $gateLog = Join-Path $LogRoot "$stamp-runtime-$LogName"
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    $arguments = @(
        $Runner,
        '--qemu', $Qemu,
        '--image', $ImagePath,
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
    $reportedFailure = Select-String -LiteralPath $gateLog `
        -SimpleMatch 'guest-smoke: FAIL' -Quiet
    $reportedPass = Select-String -LiteralPath $gateLog `
        -SimpleMatch 'guest-smoke: PASS' -Quiet
    if ($exitCode -eq 0 -and ($reportedFailure -or !$reportedPass)) {
        $exitCode = 1
    }
    if ($exitCode -ne 0) {
        Write-Output "RUNTIME FAIL exit=$exitCode elapsed=$([int]$watch.Elapsed.TotalSeconds)s log=$gateLog"
        Get-Content -LiteralPath $gateLog -Tail 40
        throw "REIST runtime smoke '$LogName' failed with exit $exitCode."
    }
    Write-Output "RUNTIME PASS elapsed=$([int]$watch.Elapsed.TotalSeconds)s log=$gateLog"
}

function Invoke-StorageRecoverySmoke {
    $relativeOutput = 'build/storage-injection'
    $faultImage = Join-Path $RepoRoot "$relativeOutput/reist-os.img"
    New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $buildLog = Join-Path $LogRoot "$stamp-runtime-storage-recovery-build.log"
    $LASTEXITCODE = 0
    & $BuildScript -Target qemu -Video $Video -StorageFaultInjection `
        -OutputDirectory $relativeOutput -SkipReleaseSbom *> $buildLog
    if ($LASTEXITCODE -ne 0 -or
        !(Test-Path -LiteralPath $faultImage -PathType Leaf)) {
        Get-Content -LiteralPath $buildLog -Tail 40
        throw 'Isolated storage-recovery image build failed.'
    }
    Invoke-Smoke 'guest-smoke-storage-recovery.log' @(
        '--expect-storage-recovery'
    ) $true $faultImage
}

function Invoke-FramebufferSmoke {
    $relativeOutput = 'build/framebuffer-runtime'
    $framebufferImage = Join-Path $RepoRoot "$relativeOutput/reist-os.img"
    New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $buildLog = Join-Path $LogRoot "$stamp-runtime-framebuffer-build.log"
    $LASTEXITCODE = 0
    & $BuildScript -Target qemu -Video framebuffer `
        -OutputDirectory $relativeOutput -SkipReleaseSbom *> $buildLog
    if ($LASTEXITCODE -ne 0 -or
        !(Test-Path -LiteralPath $framebufferImage -PathType Leaf)) {
        Get-Content -LiteralPath $buildLog -Tail 40
        throw 'Isolated framebuffer image build failed.'
    }
    Invoke-Smoke 'guest-smoke-framebuffer.log' @() $true $framebufferImage
}

function Invoke-RuntimeDesktop(
    [bool]$ExpectFailure = $false,
    [bool]$RenderProbe = $false,
    [bool]$SurfaceProbe = $false,
    [bool]$ControlProbe = $false,
    [bool]$VmwareSvga2d = $false,
    [bool]$SoundProbe = $false,
    [bool]$GuidemoClickProbe = $false,
    [bool]$HoverProbe = $false,
    [bool]$SupervisedProbe = $false,
    [string]$ImagePath = $Image,
    [int]$Smp = 1,
    [bool]$NotepadProbe = $false,
    [bool]$NotepadFontProbe = $false,
    [bool]$TrashRestoreProbe = $false,
    [bool]$ExplorerScrollProbe = $false,
    [bool]$ExplorerViewsProbe = $false,
    [bool]$ShortcutProbe = $false
) {
    if (([int]$ExpectFailure + [int]$RenderProbe + [int]$SurfaceProbe +
            [int]$ControlProbe + [int]$NotepadProbe +
            [int]$NotepadFontProbe + [int]$SoundProbe +
            [int]$GuidemoClickProbe + [int]$HoverProbe +
            [int]$TrashRestoreProbe + [int]$ExplorerScrollProbe +
            [int]$ExplorerViewsProbe + [int]$ShortcutProbe) -gt 1) {
        throw 'Runtime desktop probe modes are exclusive.'
    }
    if ($SupervisedProbe -and !$HoverProbe) {
        throw 'A supervised runtime probe requires hover mode.'
    }
    $screenshot = Join-Path $RepoRoot 'build\runtime-desktop.ppm'
    $arguments = @('--qemu', $Qemu, '--image', $ImagePath,
        '--screenshot', $screenshot, '--smp', [string]$Smp)
    if ($ExpectFailure) { $arguments += '--expect-failure' }
    if ($RenderProbe) {
        New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null
        $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
        $metricsLog = Join-Path $LogRoot "$stamp-runtime-desktop-metrics.log"
        $arguments += @('--render-probe', '--metrics-log', $metricsLog)
    }
    if ($SurfaceProbe) { $arguments += '--surface-probe' }
    if ($ControlProbe) { $arguments += '--control-probe' }
    if ($NotepadProbe) { $arguments += '--notepad-probe' }
    if ($NotepadFontProbe) { $arguments += '--notepad-font-probe' }
    if ($TrashRestoreProbe) { $arguments += '--trash-restore-probe' }
    if ($ExplorerScrollProbe) { $arguments += '--explorer-scroll-probe' }
    if ($ExplorerViewsProbe) { $arguments += '--explorer-views-probe' }
    if ($ShortcutProbe) { $arguments += '--shortcut-probe' }
    if ($SoundProbe) { $arguments += '--sound-probe' }
    if ($GuidemoClickProbe) { $arguments += '--guidemo-click-probe' }
    if ($HoverProbe) {
        New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null
        $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
        $metricsLog = Join-Path $LogRoot "$stamp-runtime-desktop-hover.log"
        $arguments += @('--hover-probe', '--metrics-log', $metricsLog)
    }
    if ($SupervisedProbe) { $arguments += '--supervised-probe' }
    if ($VmwareSvga2d) { $arguments += '--vmware-vga' }
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
    $runnerPassed = Select-String -LiteralPath $gateLog -SimpleMatch `
        'FDD HOTPLUG PASS' -Quiet
    $runnerFailed = Select-String -LiteralPath $gateLog -SimpleMatch `
        'FDD HOTPLUG FAIL' -Quiet
    if ($exitCode -eq 0 -and (!$runnerPassed -or $runnerFailed)) {
        $exitCode = 1
    }
    if ($exitCode -ne 0) {
        Write-Output "RUNTIME FAIL exit=$exitCode elapsed=$([int]$watch.Elapsed.TotalSeconds)s log=$gateLog"
        Get-Content -LiteralPath $gateLog -Tail 40
        throw "REIST runtime smoke 'fdd-hotplug' failed with exit $exitCode."
    }
    Write-Output "RUNTIME PASS elapsed=$([int]$watch.Elapsed.TotalSeconds)s log=$gateLog"
}

function Invoke-Ext2Stat {
    New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $gateLog = Join-Path $LogRoot "$stamp-runtime-ext2-stat"
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    $exitCode = 0
    try {
        $LASTEXITCODE = 0
        & $Python $Ext2StatRunner `
            --qemu $Qemu `
            --image $Image `
            --disk (Join-Path $RepoRoot 'build\ext2-stat.img') `
            --log (Join-Path $RepoRoot 'build\guest-ext2-stat.log') `
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
    $runnerPassed = Select-String -LiteralPath $gateLog -SimpleMatch `
        'EXT2 STAT PASS' -Quiet
    $runnerFailed = Select-String -LiteralPath $gateLog -SimpleMatch `
        'EXT2 STAT FAIL' -Quiet
    if ($exitCode -eq 0 -and (!$runnerPassed -or $runnerFailed)) {
        $exitCode = 1
    }
    if ($exitCode -ne 0) {
        Write-Output "RUNTIME FAIL exit=$exitCode elapsed=$([int]$watch.Elapsed.TotalSeconds)s log=$gateLog"
        Get-Content -LiteralPath $gateLog -Tail 40
        throw "REIST runtime smoke 'ext2-stat' failed with exit $exitCode."
    }
    Write-Output "RUNTIME PASS elapsed=$([int]$watch.Elapsed.TotalSeconds)s log=$gateLog"
}

function Invoke-Ext2Symlink {
    New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $gateLog = Join-Path $LogRoot "$stamp-runtime-ext2-symlink"
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    $exitCode = 0
    try {
        $LASTEXITCODE = 0
        & $Python $Ext2SymlinkRunner `
            --qemu $Qemu `
            --image $Image `
            --disk (Join-Path $RepoRoot 'build\ext2-symlink.img') `
            --log (Join-Path $RepoRoot 'build\guest-ext2-symlink.log') `
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
    $runnerPassed = Select-String -LiteralPath $gateLog -SimpleMatch `
        'EXT2 SYMLINK PASS' -Quiet
    $runnerFailed = Select-String -LiteralPath $gateLog -SimpleMatch `
        'EXT2 SYMLINK FAIL' -Quiet
    if ($exitCode -eq 0 -and (!$runnerPassed -or $runnerFailed)) {
        $exitCode = 1
    }
    if ($exitCode -ne 0) {
        Write-Output "RUNTIME FAIL exit=$exitCode elapsed=$([int]$watch.Elapsed.TotalSeconds)s log=$gateLog"
        Get-Content -LiteralPath $gateLog -Tail 40
        throw "REIST runtime smoke 'vfs-symbolic-links' failed with exit $exitCode."
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
    param([switch]$ChkdskOnly)
    New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $gateName = if ($ChkdskOnly) { 'chkdsk-fat12' } else { 'admin-maintenance' }
    $gateLog = Join-Path $LogRoot "$stamp-runtime-$gateName"
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    $exitCode = 0
    try {
        $LASTEXITCODE = 0
        $runnerArgs = @(
            '--qemu', $Qemu,
            '--image', $Image,
            '--disk', (Join-Path $RepoRoot 'build\admin-maintenance.img'),
            '--floppy', (Join-Path $RepoRoot 'build\admin-maintenance-fdd.img'),
            '--log', (Join-Path $RepoRoot "build\guest-$gateName.log")
        )
        if ($ChkdskOnly) {
            $runnerArgs += '--chkdsk-only'
        }
        & $Python $AdminMaintenanceRunner @runnerArgs *> $gateLog
        $exitCode = if ($null -eq $LASTEXITCODE) { 1 } else { $LASTEXITCODE }
    }
    catch {
        $exitCode = 1
        $_ | Out-String | Add-Content -LiteralPath $gateLog
    }
    finally {
        $watch.Stop()
    }
    $passMarker = if ($ChkdskOnly) { 'CHKDSK FAT12 PASS' } else { 'ADMIN MAINTENANCE PASS' }
    $reportedFailure = Select-String -LiteralPath $gateLog `
        -SimpleMatch 'FAIL' -Quiet
    $reportedPass = Select-String -LiteralPath $gateLog `
        -SimpleMatch $passMarker -Quiet
    if ($exitCode -eq 0 -and ($reportedFailure -or !$reportedPass)) {
        $exitCode = 1
    }
    if ($exitCode -ne 0) {
        Write-Output "RUNTIME FAIL exit=$exitCode elapsed=$([int]$watch.Elapsed.TotalSeconds)s log=$gateLog"
        Get-Content -LiteralPath $gateLog -Tail 40
        throw "REIST runtime smoke '$gateName' failed with exit $exitCode."
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
    $runnerOut = "$gateLog.runner.out"
    $runnerErr = "$gateLog.runner.err"
    try {
        & $BuildScript -Target qemu -Video vga `
            -DriverDomainFaultInjection -SkipReleaseSbom *> $gateLog
        $runner = Start-Process -FilePath $Python -Wait -PassThru `
            -ArgumentList @(
                $DriverDomainRunner, '--qemu', "`"$Qemu`"", '--image', $Image,
                '--log', (Join-Path $RepoRoot 'build\guest-driver-domain.log')
            ) -RedirectStandardOutput $runnerOut `
              -RedirectStandardError $runnerErr
        Get-Content -LiteralPath $runnerOut, $runnerErr |
            Add-Content -LiteralPath $gateLog
        $exitCode = $runner.ExitCode
    }
    catch {
        $exitCode = 1
        $_ | Out-String | Add-Content -LiteralPath $gateLog
    }
    finally {
        $watch.Stop()
        Remove-Item -LiteralPath $runnerOut, $runnerErr -Force `
            -ErrorAction SilentlyContinue
    }
    if ($exitCode -ne 0) {
        Write-Output "RUNTIME FAIL exit=$exitCode elapsed=$([int]$watch.Elapsed.TotalSeconds)s log=$gateLog"
        Get-Content -LiteralPath $gateLog -Tail 40
        throw "REIST runtime smoke 'driver-domain' failed with exit $exitCode."
    }
    Write-Output "RUNTIME PASS elapsed=$([int]$watch.Elapsed.TotalSeconds)s log=$gateLog"
}

function Invoke-SystemLayout([bool]$ChkdskOnly = $false,
                             [bool]$EditorOnly = $false) {
    New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $gateLog = Join-Path $LogRoot "$stamp-runtime-system-layout"
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    $exitCode = 0
    try {
        $LASTEXITCODE = 0
        $arguments = @(
            $SystemLayoutRunner,
            '--qemu', $Qemu,
            '--image', $Image,
            '--log', (Join-Path $RepoRoot 'build\guest-system-layout.log')
        )
        if ($ChkdskOnly) { $arguments += '--chkdsk-only' }
        if ($EditorOnly) { $arguments += '--editor-only' }
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
    $reportedFailure = Select-String -LiteralPath $gateLog `
        -SimpleMatch 'SYSTEM LAYOUT FAIL' -Quiet
    $reportedPass = Select-String -LiteralPath $gateLog `
        -SimpleMatch 'SYSTEM LAYOUT PASS' -Quiet
    if ($exitCode -eq 0 -and ($reportedFailure -or !$reportedPass)) {
        $exitCode = 1
    }
    if ($exitCode -ne 0) {
        Write-Output "RUNTIME FAIL exit=$exitCode elapsed=$([int]$watch.Elapsed.TotalSeconds)s log=$gateLog"
        Get-Content -LiteralPath $gateLog -Tail 40
        throw "REIST runtime smoke 'system-layout' failed with exit $exitCode."
    }
    Write-Output "RUNTIME PASS elapsed=$([int]$watch.Elapsed.TotalSeconds)s log=$gateLog"
}

function Invoke-StorageReconnect {
    $vmrun = $null
    foreach ($candidate in @(
        'C:\Program Files\VMware\VMware Workstation\vmrun.exe',
        'C:\Program Files (x86)\VMware\VMware Workstation\vmrun.exe'
    )) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            $vmrun = $candidate
            break
        }
    }
    if ($null -eq $vmrun) { throw 'VMware vmrun is required for storage-reconnect.' }

    New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $gateLog = Join-Path $LogRoot "$stamp-runtime-storage-reconnect.log"
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    $vmx = Join-Path $RepoRoot 'build\vmware\reist-os\reist-os.vmx'
    $serial = Join-Path $RepoRoot 'build\vmware\reist-os\vmware-serial.log'
    $started = $false
    try {
        $LASTEXITCODE = 0
        & $BuildScript -Target vmware -Video $Video *> $gateLog
        if ($LASTEXITCODE -ne 0 || !(Test-Path -LiteralPath $vmx -PathType Leaf)) {
            throw 'VMware reference package build failed.'
        }
        $running = & $vmrun -T ws list 2>$null
        if ($running | Where-Object { $_.Trim() -ieq $vmx }) {
            throw 'VMware package VM is already running.'
        }

        for ($stage = 1; $stage -le 2; ++$stage) {
            # Workstation prompts instead of starting headless when a serial
            # file already exists. A fresh file also makes each stage's
            # readiness evidence independent of stale marker counts.
            if (Test-Path -LiteralPath $serial) {
                Remove-Item -LiteralPath $serial -Force
            }
            # This Workstation host rejects VIX headless startup although the
            # generated VM is valid. Use the package launcher's supported GUI
            # mode; serial evidence and bounded hard-stop remain automated.
            $startOutput = & $vmrun -T ws start $vmx gui 2>&1
            $startExit = $LASTEXITCODE
            $startOutput | Add-Content -LiteralPath $gateLog
            $published = $false
            $publishDeadline = (Get-Date).AddSeconds(5)
            do {
                $runningAfterStart = & $vmrun -T ws list 2>$null
                if ($runningAfterStart |
                        Where-Object { $_.Trim() -ieq $vmx }) {
                    $published = $true
                    break
                }
                Start-Sleep -Milliseconds 250
            } while ((Get-Date) -lt $publishDeadline)
            if (!$published) {
                throw "VMware start stage $stage was not published (exit $startExit)."
            }
            $started = $true
            $deadline = (Get-Date).AddSeconds(45)
            $passed = $false
            do {
                $ready = @(Select-String -LiteralPath $serial -SimpleMatch `
                    'REIST_STORAGE SERVICE_READY' `
                    -ErrorAction SilentlyContinue)
                $boot = @(Select-String -LiteralPath $serial -SimpleMatch `
                    'BOOT_OK' -ErrorAction SilentlyContinue)
                if ($ready.Count -gt 0 -and $boot.Count -gt 0 -and
                    $ready[0].LineNumber -lt $boot[0].LineNumber) {
                    $passed = $true
                    break
                }
                Start-Sleep -Milliseconds 250
            } while ((Get-Date) -lt $deadline)
            if (!$passed) { throw "VMware storage reconnect stage $stage timed out." }
            $stageSerial = Join-Path $LogRoot `
                "$stamp-runtime-storage-reconnect-stage-$stage-serial.log"
            Copy-Item -LiteralPath $serial -Destination $stageSerial -Force
            "STAGE $stage PASS ready_line=$($ready[0].LineNumber) " +
                "boot_line=$($boot[0].LineNumber) serial=$stageSerial" |
                Add-Content -LiteralPath $gateLog

            # A hard stop deliberately models power loss. Stage two must then
            # publish fresh service/boot markers from the same package. GTEST
            # is interactive and therefore not a reachable headless marker.
            $stopOutput = & $vmrun -T ws stop $vmx hard 2>&1
            $stopExit = $LASTEXITCODE
            $stopOutput | Add-Content -LiteralPath $gateLog
            if ($stopExit -ne 0) {
                throw "VMware hard stop stage $stage failed with exit $stopExit."
            }
            $started = $false
        }
    }
    catch {
        $_ | Out-String | Add-Content -LiteralPath $gateLog
        Get-Content -LiteralPath $gateLog -Tail 40
        throw
    }
    finally {
        if ($started) { & $vmrun -T ws stop $vmx hard 2>$null | Out-Null }
        $watch.Stop()
    }
    Write-Output "VMWARE STORAGE RECONNECT PASS elapsed=$([int]$watch.Elapsed.TotalSeconds)s log=$gateLog"
}

function Test-WcetBaseline {
    param(
        [string]$Transcript,
        [ValidateSet('qemu', 'vmware')]
        [string]$Platform
    )
    $pattern = '(?m)^REIST_WCET BASELINE version=(\d+) frequency_hz=(\d+) scheduler_samples=(\d+) scheduler_total_cycles=(\d+) scheduler_max_cycles=(\d+) int80_samples=(\d+) int80_total_cycles=(\d+) int80_max_cycles=(\d+) clock_anomalies=(\d+)\s*$'
    $match = [regex]::Match($Transcript, $pattern)
    if (!$match.Success) { throw 'Missing bounded REIST WCET baseline marker.' }

    $values = @()
    for ($index = 1; $index -le 9; ++$index) {
        $values += [uint64]::Parse($match.Groups[$index].Value)
    }
    if ($values[0] -ne 1 -or $values[1] -eq 0) {
        throw 'Invalid REIST WCET baseline version or frequency.'
    }
    $document = Get-Content -LiteralPath $WcetBudget -Raw | ConvertFrom-Json
    $platformBudget = $document.platforms.PSObject.Properties[$Platform].Value
    if ($values[2] -lt [uint64]$document.minimum_samples -or
        $values[5] -lt [uint64]$document.minimum_samples) {
        throw 'Insufficient REIST WCET baseline samples.'
    }
    if ($values[8] -gt [uint64]$document.maximum_clock_anomalies) {
        throw 'REIST WCET clock anomaly detected.'
    }
    if ($values[3] -lt $values[4] -or $values[6] -lt $values[7]) {
        throw 'Invalid REIST WCET baseline totals.'
    }
    $schedulerNs = [uint64][Math]::Ceiling(
        ([double]$values[4] * 1000000000.0) / [double]$values[1])
    $int80Ns = [uint64][Math]::Ceiling(
        ([double]$values[7] * 1000000000.0) / [double]$values[1])
    if ($schedulerNs -gt [uint64]$platformBudget.scheduler_decision_max_ns) {
        throw "Scheduler decision baseline exceeded: $schedulerNs ns."
    }
    if ($int80Ns -gt [uint64]$platformBudget.int80_probe_max_ns) {
        throw "INT-80 probe baseline exceeded: $int80Ns ns."
    }
    return "platform=$Platform scheduler_max_ns=$schedulerNs int80_max_ns=$int80Ns samples=$($values[2])/$($values[5])"
}

function Invoke-VmwareWcetBaseline {
    $vmrun = $null
    foreach ($candidate in @(
        'C:\Program Files\VMware\VMware Workstation\vmrun.exe',
        'C:\Program Files (x86)\VMware\VMware Workstation\vmrun.exe'
    )) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            $vmrun = $candidate
            break
        }
    }
    if ($null -eq $vmrun) { throw 'VMware vmrun is required for wcet-baseline.' }

    New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $gateLog = Join-Path $LogRoot "$stamp-runtime-wcet-vmware.log"
    $evidence = Join-Path $LogRoot "$stamp-runtime-wcet-vmware-serial.log"
    $vmx = Join-Path $RepoRoot 'build\vmware\reist-os\reist-os.vmx'
    $serial = Join-Path $RepoRoot 'build\vmware\reist-os\vmware-serial.log'
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    $started = $false
    try {
        $LASTEXITCODE = 0
        & $BuildScript -Target vmware -Video $Video *> $gateLog
        if ($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath $vmx -PathType Leaf)) {
            throw 'VMware reference package build failed.'
        }
        $running = & $vmrun -T ws list 2>$null
        if ($running | Where-Object { $_.Trim() -ieq $vmx }) {
            throw 'VMware package VM is already running.'
        }
        if (Test-Path -LiteralPath $serial) {
            Remove-Item -LiteralPath $serial -Force
        }
        $startFailures = @()
        for ($attempt = 1; $attempt -le 3 -and !$started; ++$attempt) {
            $startOutput = & $vmrun -T ws start $vmx nogui 2>&1
            $startExit = $LASTEXITCODE
            $startOutput | Add-Content -LiteralPath $gateLog
            $publishDeadline = (Get-Date).AddSeconds(5)
            do {
                $runningAfterStart = & $vmrun -T ws list 2>$null
                $serialPublished = Test-Path -LiteralPath $serial -PathType Leaf
                if (($runningAfterStart |
                        Where-Object { $_.Trim() -ieq $vmx }) -or
                    ($serialPublished -and
                     (Get-Item -LiteralPath $serial).Length -gt 0)) {
                    $started = $true
                    break
                }
                Start-Sleep -Milliseconds 250
            } while ((Get-Date) -lt $publishDeadline)
            if (!$started) {
                $detail = ($startOutput | ForEach-Object {
                    $_.ToString().Trim()
                } | Where-Object { $_ }) -join ' '
                if (!$detail) { $detail = 'no vmrun diagnostic' }
                $startFailures += "attempt=$attempt exit=$startExit $detail"
                if ($attempt -lt 3) { Start-Sleep -Milliseconds 500 }
            }
        }
        if (!$started) {
            throw ('VMware start was not published: ' +
                ($startFailures -join '; '))
        }

        $deadline = (Get-Date).AddSeconds(45)
        $summary = $null
        do {
            if (Test-Path -LiteralPath $serial -PathType Leaf) {
                $serialContent = Get-Content -LiteralPath $serial -Raw
                $transcript = if ($null -eq $serialContent) {
                    ''
                } else {
                    [string]$serialContent
                }
                if ($transcript.Contains('BOOT_OK') -and
                    $transcript.Contains('REIST_WCET BASELINE')) {
                    $summary = Test-WcetBaseline $transcript 'vmware'
                    break
                }
            }
            Start-Sleep -Milliseconds 250
        } while ((Get-Date) -lt $deadline)
        if ($null -eq $summary) { throw 'VMware WCET baseline timed out.' }
        Copy-Item -LiteralPath $serial -Destination $evidence -Force
        "$summary evidence=$evidence" | Add-Content -LiteralPath $gateLog
    }
    catch {
        $_ | Out-String | Add-Content -LiteralPath $gateLog
        Get-Content -LiteralPath $gateLog -Tail 40
        throw
    }
    finally {
        if ($started) { & $vmrun -T ws stop $vmx hard 2>$null | Out-Null }
        $watch.Stop()
    }
    Write-Output "VMWARE WCET PASS elapsed=$([int]$watch.Elapsed.TotalSeconds)s log=$gateLog"
}

function Invoke-VmwareAudioService {
    $vmrun = $null
    foreach ($candidate in @(
        'C:\Program Files\VMware\VMware Workstation\vmrun.exe',
        'C:\Program Files (x86)\VMware\VMware Workstation\vmrun.exe'
    )) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            $vmrun = $candidate
            break
        }
    }
    if ($null -eq $vmrun) {
        Write-Output 'VMWARE AUDIO SKIP vmrun unavailable'
        return
    }

    $vmx = Join-Path $RepoRoot 'build\vmware\reist-os\reist-os.vmx'
    $serial = Join-Path $RepoRoot 'build\vmware\reist-os\vmware-serial.log'
    if (!(Test-Path -LiteralPath $vmx -PathType Leaf)) {
        throw 'VMware package is missing; build target vmware first.'
    }
    $configuration = Get-Content -LiteralPath $vmx -Raw
    foreach ($required in @(
        'sound.virtualDev = "hdaudio"',
        'sound.pciSlotNumber = "34"',
        'usb.generic.allowHID = "FALSE"',
        'usb.generic.allowLastHID = "FALSE"'
    )) {
        if (!$configuration.Contains($required)) {
            throw "VMware audio safety configuration missing: $required"
        }
    }
    $running = & $vmrun -T ws list 2>$null
    if ($running | Where-Object { $_.Trim() -ieq $vmx }) {
        throw 'VMware package VM is already running.'
    }

    $beforeFallback = @(Select-String -LiteralPath $serial -SimpleMatch `
        'DEVICE_DOMAIN: legacy INTx PIC fallback' `
        -ErrorAction SilentlyContinue).Count
    $beforeProfile = @(Select-String -LiteralPath $serial -SimpleMatch `
        'REIST_AUDIO HDA_PROFILE pci=15AD:1977' `
        -ErrorAction SilentlyContinue).Count
    $beforeReady = @(Select-String -LiteralPath $serial -SimpleMatch `
        'REIST_AUDIO SERVICE_READY' -ErrorAction SilentlyContinue).Count
    $beforeRejected = @(Select-String -LiteralPath $serial -SimpleMatch `
        'REIST_AUDIO HDA_REJECTED' -ErrorAction SilentlyContinue).Count
    $started = $false
    $passed = $false
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    try {
        $startFailures = @()
        for ($attempt = 1; $attempt -le 2; ++$attempt) {
            $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
            $startInfo.FileName = $vmrun
            $startInfo.Arguments = "-T ws start `"$vmx`" nogui"
            $startInfo.UseShellExecute = $false
            $startInfo.CreateNoWindow = $true
            $startInfo.RedirectStandardOutput = $true
            $startInfo.RedirectStandardError = $true
            $startProcess = [System.Diagnostics.Process]::new()
            $startProcess.StartInfo = $startInfo
            try {
                [void]$startProcess.Start()
            }
            catch {
                $startFailures += "attempt=$attempt launch=$($_.Exception.Message)"
                if ($attempt -lt 2) { Start-Sleep -Milliseconds 500 }
                continue
            }
            $stdout = $startProcess.StandardOutput.ReadToEndAsync()
            $stderr = $startProcess.StandardError.ReadToEndAsync()
            if (!$startProcess.WaitForExit(15000)) {
                $startProcess.Kill()
                [void]$startProcess.WaitForExit(2000)
                $startFailures += "attempt=$attempt exit=timeout"
                if ($attempt -lt 2) { Start-Sleep -Milliseconds 500 }
                continue
            }
            $startExit = $startProcess.ExitCode
            $startOutput = @(
                $stdout.GetAwaiter().GetResult(),
                $stderr.GetAwaiter().GetResult()
            )
            if ($startExit -eq 0) {
                $started = $true
                break
            }

            # vmrun can return before the Workstation service has published
            # the VM. Accept only a positively listed VM, otherwise make one
            # bounded retry and retain the native diagnostic for the gate log.
            Start-Sleep -Milliseconds 500
            $runningAfterStart = & $vmrun -T ws list 2>$null
            if ($runningAfterStart |
                    Where-Object { $_.Trim() -ieq $vmx }) {
                $started = $true
                break
            }
            $detail = ($startOutput | ForEach-Object {
                $_.ToString().Trim()
            } | Where-Object { $_ }) -join ' '
            if (!$detail) { $detail = 'no vmrun diagnostic' }
            $startFailures += "attempt=$attempt exit=$startExit $detail"
            if ($attempt -lt 2) { Start-Sleep -Milliseconds 500 }
        }
        if (!$started) {
            throw ('Unable to start VMware audio smoke: ' +
                ($startFailures -join '; '))
        }
        $deadline = (Get-Date).AddSeconds(45)
        do {
            $rejected = @(Select-String -LiteralPath $serial -SimpleMatch `
                'REIST_AUDIO HDA_REJECTED' -ErrorAction SilentlyContinue).Count
            if ($rejected -gt $beforeRejected) {
                throw 'VMware rejected the HDA safety profile.'
            }
            $fallback = @(Select-String -LiteralPath $serial -SimpleMatch `
                'DEVICE_DOMAIN: legacy INTx PIC fallback' `
                -ErrorAction SilentlyContinue).Count
            $profile = @(Select-String -LiteralPath $serial -SimpleMatch `
                'REIST_AUDIO HDA_PROFILE pci=15AD:1977' `
                -ErrorAction SilentlyContinue).Count
            $ready = @(Select-String -LiteralPath $serial -SimpleMatch `
                'REIST_AUDIO SERVICE_READY' -ErrorAction SilentlyContinue).Count
            if ($fallback -gt $beforeFallback -and
                $profile -gt $beforeProfile -and $ready -gt $beforeReady) {
                $passed = $true
                break
            }
            Start-Sleep -Milliseconds 250
        } while ((Get-Date) -lt $deadline)
        if (!$passed) { throw 'VMware audio service readiness timed out.' }
    }
    finally {
        if ($started) {
            & $vmrun -T ws stop $vmx hard 2>$null | Out-Null
        }
        $watch.Stop()
    }
    Write-Output "VMWARE AUDIO PASS elapsed=$([int]$watch.Elapsed.TotalSeconds)s"
}

if ($Mode -eq 'storage-maintenance' -and $Target -ne 'qemu') {
    throw 'storage-maintenance requires -Target qemu.'
}
if ($Mode -eq 'storage-reconnect' -and $Target -ne 'vmware') {
    throw 'storage-reconnect requires -Target vmware.'
}

switch ($Mode) {
    'boot-success' {
        New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null
        $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
        $trial = Join-Path $RepoRoot 'build\boot-success-trial.img'
        $reverseTrial = Join-Path $RepoRoot 'build\boot-success-reverse-trial.img'
        $serialLog = Join-Path $LogRoot `
            "$stamp-runtime-boot-success-serial.log"
        & $Python $BootSuccessRunner --qemu $Qemu --image $Image `
            --kernel $KernelImage --signature $KernelSignature `
            --policy $BootTrustPolicy --openssl $OpenSsl --root $RepoRoot `
            --output $trial --reverse-output $reverseTrial `
            --log $serialLog --timeout 15
        if ($LASTEXITCODE -ne 0) {
            throw 'REIST boot-success runtime failed.'
        }
    }
    'boot-control' {
        New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null
        $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
        $trial = Join-Path $RepoRoot 'build\boot-control-trial.img'
        $serialLog = Join-Path $LogRoot `
            "$stamp-runtime-boot-control-serial.log"
        & $Python $BootControlRunner --qemu $Qemu --image $Image `
            --kernel $KernelImage --signature $KernelSignature `
            --policy $BootTrustPolicy --openssl $OpenSsl --root $RepoRoot `
            --output $trial --log $serialLog --timeout 4
        if ($LASTEXITCODE -ne 0) {
            throw 'REIST boot-control runtime failed.'
        }
    }
    'boot-integrity' {
        New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null
        $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
        $tampered = Join-Path $RepoRoot 'build\boot-integrity-tampered.img'
        $serialLog = Join-Path $LogRoot `
            "$stamp-runtime-boot-integrity-serial.log"
        & $Python $BootIntegrityRunner --qemu $Qemu --image $Image `
            --output $tampered --log $serialLog --timeout 12
        if ($LASTEXITCODE -ne 0) {
            throw 'REIST boot-integrity runtime failed.'
        }
    }
    'normal' {
        if ($Video -eq 'framebuffer') {
            Invoke-FramebufferSmoke
        } else {
            Invoke-Smoke 'guest-smoke.log' @()
        }
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
    'memory-resilience' {
        $relativeOutput = 'build/memory-resilience'
        $proofImage = Join-Path $RepoRoot "$relativeOutput/reist-os.img"
        & $BuildScript -Target qemu -Video vga -ResilientPageBootProof `
            -OutputDirectory $relativeOutput -SkipReleaseSbom
        if ($LASTEXITCODE -ne 0 -or
            !(Test-Path -LiteralPath $proofImage -PathType Leaf)) {
            throw 'Resilient-page boot-proof image build failed.'
        }
        Invoke-Smoke 'guest-smoke-memory-resilience.log' @(
            '--vmware-vga', '--expect-resilient-page-boot-proof',
            '--timeout', '180'
        ) $true $proofImage
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
    'http-server' {
        Invoke-Smoke 'guest-smoke-http-server.log' @(
            '--nic', 'rtl8139', '--expect-http-server'
        )
    }
    'storage-recovery' {
        Invoke-StorageRecoverySmoke
    }
    'storage-service-restart' {
        $relativeOutput = 'build/storage-service-restart'
        $restartImage = Join-Path $RepoRoot "$relativeOutput/reist-os.img"
        & $BuildScript -Target qemu -Video vga -StorageFaultInjection `
            -OutputDirectory $relativeOutput -SkipReleaseSbom
        if ($LASTEXITCODE -ne 0 -or
            !(Test-Path -LiteralPath $restartImage -PathType Leaf)) {
            throw 'Isolated storage-service restart image build failed.'
        }
        Invoke-Smoke 'guest-smoke-storage-service-restart.log' @(
            '--smp', '4', '--expect-smp', '--vmware-vga', '--expect-svga2d',
            '--expect-storage-recovery', '--expect-storage-ap-restart',
            '--timeout', '120'
        ) $true $restartImage
    }
    'storage-io-failure' {
        Invoke-Smoke 'guest-smoke-storage-io-failure.log' @(
            '--expect-storage-io-failure'
        )
    }
    'storage-maintenance' {
        Invoke-AdminMaintenance
        Invoke-Smoke 'guest-smoke-storage-maintenance.log'
    }
    'storage-reconnect' {
        Invoke-StorageReconnect
    }
    'wcet-baseline' {
        if ($Target -eq 'qemu') {
            Invoke-Smoke 'guest-smoke-wcet-baseline.log' @(
                '--expect-wcet-baseline', '--wcet-budget', $WcetBudget,
                '--wcet-platform', 'qemu'
            )
        } else {
            Invoke-VmwareWcetBaseline
        }
    }
    'fdd-hotplug' {
        Invoke-FddHotplug
    }
    'ext2-stat' {
        Invoke-Ext2Stat
    }
    'vfs-symbolic-links' {
        Invoke-Ext2Symlink
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
    'editor-load' {
        Invoke-SystemLayout -EditorOnly $true
    }
    'chkdsk-readonly' {
        Invoke-SystemLayout $true
    }
    'chkdsk-fat12' {
        Invoke-AdminMaintenance -ChkdskOnly
    }
    'pci-audio' {
        # Prove VMware readiness before the independent QEMU TCG capture.
        # Some Workstation hosts transiently reject vmrun directly after the
        # QEMU audio process exits even though no QEMU process remains.
        Invoke-VmwareAudioService
        $wav = Join-Path $RepoRoot 'build\pci-audio.wav'
        $audioLog = Join-Path $RepoRoot 'build\guest-pci-audio.log'
        & $Python $PciAudioRunner --qemu $Qemu --image $Image `
            --wav $wav --log $audioLog
        if ($LASTEXITCODE -ne 0) {
            Get-Content -LiteralPath $audioLog -Tail 40
            throw 'REIST PCI audio runtime failed.'
        }
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
    'vmware-svga2d' {
        if ($Target -eq 'qemu') {
            Invoke-Smoke 'guest-smoke-svga2d.log' @(
                '--vmware-vga', '--expect-svga2d', '--boot-only'
            ) $false
        } else {
            & $VmwareSvga2dRunner
            if ($LASTEXITCODE -ne 0) { throw 'VMware SVGA2D runtime failed.' }
        }
    }
    'vmware-svga2d-lifecycle' {
        if ($Target -eq 'qemu') {
            Invoke-RuntimeDesktop $false $true $false $false $true
        } else {
            & $VmwareSvga2dRunner
            if ($LASTEXITCODE -ne 0) {
                throw 'VMware SVGA2D console lifecycle failed.'
            }
        }
    }
    'vmware-mouse' {
        & $VmwareMouseRunner
        if ($LASTEXITCODE -ne 0) {
            throw 'VMware mouse runtime failed.'
        }
    }
    'vmware-hover-cadence' {
        $relativeOutput = 'build/vmware-hover-cadence'
        $sourcePackage = Join-Path $RepoRoot `
            "$relativeOutput/vmware/reist-os"
        New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null
        $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
        $buildLog = Join-Path $LogRoot `
            "$stamp-runtime-vmware-hover-build.log"
        & $BuildScript -Target vmware -Video vga -CompositorHoverProbe `
            -OutputDirectory $relativeOutput -SkipReleaseSbom *> $buildLog
        if ($LASTEXITCODE -ne 0 -or
            !(Test-Path -LiteralPath $sourcePackage -PathType Container)) {
            Get-Content -LiteralPath $buildLog -Tail 40
            throw 'VMware supervised hover image build failed.'
        }
        & $VmwareMouseRunner -HoverCadence -SourcePackage $sourcePackage
        if ($LASTEXITCODE -ne 0) {
            throw 'VMware hover cadence runtime failed.'
        }
    }
    'curl-client' {
        Invoke-Smoke 'guest-smoke-curl-client.log' @(
            '--nic', 'rtl8139', '--vmware-vga', '--expect-curl-client',
            '--timeout', '180'
        )
    }
    'curl-https-client' {
        $relativeOutput = 'build/curl-https-runtime'
        $probeImage = Join-Path $RepoRoot "$relativeOutput/reist-os.img"
        & $BuildScript -Target qemu -Video vga -CurlTlsRuntimeProbe `
            -OutputDirectory $relativeOutput -SkipReleaseSbom
        if ($LASTEXITCODE -ne 0) {
            throw 'curl HTTPS runtime-probe build failed.'
        }
        Invoke-Smoke 'guest-smoke-curl-https-client.log' @(
            '--nic', 'rtl8139', '--vmware-vga',
            '--expect-tls-curl-client', '--timeout', '180'
        ) $true $probeImage
    }
    'curl-https-public-client' {
        $relativeOutput = 'build/curl-https-public-runtime'
        $publicImage = Join-Path $RepoRoot "$relativeOutput/reist-os.img"
        & $BuildScript -Target qemu -Video vga `
            -OutputDirectory $relativeOutput -SkipReleaseSbom
        if ($LASTEXITCODE -ne 0) {
            throw 'curl public HTTPS production build failed.'
        }
        Invoke-Smoke 'guest-smoke-curl-https-public-client.log' @(
            '--nic', 'rtl8139', '--vmware-vga',
            '--expect-public-tls-curl-client', '--timeout', '240'
        ) $true $publicImage
    }
    'vmware-compositor-restart' {
        & $VmwareMouseRunner -ExpectCompositorRestart
        if ($LASTEXITCODE -ne 0) {
            throw 'VMware compositor restart runtime failed.'
        }
    }
    'vmware-benchmark' {
        & $VmwareMouseRunner -Benchmark
        if ($LASTEXITCODE -ne 0) {
            throw 'VMware benchmark runtime failed.'
        }
    }
    'vmware-rename' {
        & $VmwareMouseRunner -Rename
        if ($LASTEXITCODE -ne 0) {
            throw 'VMware rename runtime failed.'
        }
    }
    'runtime-desktop' {
        Invoke-RuntimeDesktop
    }
    'runtime-desktop-notepad' {
        Invoke-RuntimeDesktop -NotepadProbe $true
    }
    'runtime-desktop-notepad-fonts' {
        Invoke-RuntimeDesktop -NotepadFontProbe $true
    }
    'runtime-desktop-control' {
        Invoke-RuntimeDesktop -ControlProbe $true
    }
    'runtime-desktop-trash-restore' {
        Invoke-RuntimeDesktop -TrashRestoreProbe $true
    }
    'runtime-desktop-explorer-scroll' {
        Invoke-RuntimeDesktop -ExplorerScrollProbe $true
    }
    'runtime-desktop-explorer-views' {
        Invoke-RuntimeDesktop -ExplorerViewsProbe $true
    }
    'runtime-desktop-shortcuts' {
        $relativeOutput = 'build/runtime-desktop-shortcuts'
        $probeImage = Join-Path $RepoRoot "$relativeOutput/reist-os.img"
        New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null
        $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
        $buildLog = Join-Path $LogRoot `
            "$stamp-runtime-desktop-shortcuts-build.log"
        & $BuildScript -Target qemu -Video framebuffer `
            -OutputDirectory $relativeOutput -SkipReleaseSbom *> $buildLog
        if ($LASTEXITCODE -ne 0 -or
            !(Test-Path -LiteralPath $probeImage -PathType Leaf)) {
            Get-Content -LiteralPath $buildLog -Tail 40
            throw 'Desktop shortcut runtime image build failed.'
        }
        Invoke-RuntimeDesktop -ShortcutProbe $true -ImagePath $probeImage
    }
    'runtime-desktop-metrics' {
        Invoke-RuntimeDesktop $false $true
    }
    'runtime-desktop-surface' {
        Invoke-RuntimeDesktop $false $false $true
    }
    'runtime-desktop-hover-cadence' {
        $relativeOutput = 'build/runtime-desktop-hover-cadence'
        $probeImage = Join-Path $RepoRoot "$relativeOutput/reist-os.img"
        New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null
        $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
        $buildLog = Join-Path $LogRoot `
            "$stamp-runtime-qemu-hover-build.log"
        & $BuildScript -Target qemu -Video framebuffer `
            -CompositorHoverProbe -OutputDirectory $relativeOutput `
            -SkipReleaseSbom *> $buildLog
        if ($LASTEXITCODE -ne 0 -or
            !(Test-Path -LiteralPath $probeImage -PathType Leaf)) {
            Get-Content -LiteralPath $buildLog -Tail 40
            throw 'QEMU supervised hover image build failed.'
        }
        Invoke-RuntimeDesktop -HoverProbe $true -SupervisedProbe $true `
            -ImagePath $probeImage -Smp 4
    }
    'runtime-desktop-audio' {
        & $BuildScript -Target qemu -Video framebuffer `
            -SoundplayerSurfaceProbe -SkipReleaseSbom
        if ($LASTEXITCODE -ne 0) {
            throw 'Sound Player Surface probe build failed.'
        }
        Invoke-RuntimeDesktop -SoundProbe $true
    }
    'runtime-desktop-guidemo-click' {
        & $BuildScript -Target qemu -Video framebuffer -SkipReleaseSbom
        if ($LASTEXITCODE -ne 0) {
            throw 'GUIDEMO click probe build failed.'
        }
        Invoke-RuntimeDesktop -GuidemoClickProbe $true
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
