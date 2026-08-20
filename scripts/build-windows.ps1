[CmdletBinding()]
param(
    [ValidateSet('qemu', 'vmware', 'real_hw')]
    [string]$Target = 'real_hw',
    [ValidateSet('vga', 'framebuffer')]
    [string]$Video = 'vga',
    [switch]$Clean,
    [switch]$FaultInjection,
    [switch]$StorageFaultInjection,
    [switch]$StorageIoFaultInjection,
    [switch]$AhciFaultInjection,
    [ValidateSet('timeout', 'tfes', 'tfd')]
    [string]$AhciFaultMode = 'timeout',
    [switch]$HandoverFaultInjection,
    [switch]$DhcpLeaseFaultInjection,
    [switch]$DhcpRenewFaultInjection,
    [switch]$VbeRuntimeTest,
    [ValidateRange(0, 3)]
    [int]$HandoverNodeId = 0,
    [switch]$RunTests,
    [string[]]$ProgramSource = @('userspace/programs/hello.c'),
    [ValidatePattern('^[A-Za-z0-9_]{1,8}\.PRG$')]
    [string]$ProgramName = 'HELLO.PRG',
    [ValidateSet('Auto', 'Physical', 'Image')]
    [string]$VmwareFloppy = 'Auto',
    [ValidatePattern('^[A-Za-z]:$')]
    [string]$FloppyDrive = 'A:'
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
$Python = Resolve-NativeTool 'python' @(
    'C:\Python314\python.exe',
    'C:\Python313\python.exe'
)
$MsysShell = Resolve-NativeTool 'sh' @('C:\msys64\usr\bin\sh.exe')
$MsysBin = Split-Path -Parent $MsysShell

$BuildDir = Join-Path $RepoRoot 'build'
$ZigLocalCache = Join-Path $BuildDir 'zig-cache'
$ZigGlobalCache = Join-Path $BuildDir 'zig-global-cache'
$Stage1 = Join-Path $BuildDir 'stage1_mbr.bin'
$FloppyStage1 = Join-Path $BuildDir 'stage1_floppy.bin'
$Stage2 = Join-Path $BuildDir 'stage2_bios.bin'
$Kernel = Join-Path $BuildDir 'kernel.bin'
$RawImage = Join-Path $BuildDir 'reist-os.img'
$FloppyImage = Join-Path $BuildDir 'reist-os-floppy.img'
$Vmdk = Join-Path $BuildDir 'reist-os.vmdk'
$Vmx = Join-Path $BuildDir 'reist-os.vmx'
$VmwareDir = Join-Path $BuildDir 'vmware\reist-os'
$PackagedVmx = Join-Path $VmwareDir 'reist-os.vmx'
$UserProgramDir = Join-Path $BuildDir 'programs'
$UserPrg = Join-Path $UserProgramDir $ProgramName.ToUpperInvariant()
$BuildConfig = Join-Path $BuildDir '.windows-build-config.json'

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
New-Item -ItemType Directory -Force -Path $ZigLocalCache, $ZigGlobalCache | Out-Null
Push-Location $RepoRoot
try {
    # GNU Make may execute simple recipe commands directly instead of through
    # SHELL, so the native MSYS2 mkdir/rm/touch/cp tools must also be on PATH.
    $env:Path = "$MsysBin;$env:Path"
    # Keep Zig's compiler caches inside the workspace.  The default cache under
    # %LOCALAPPDATA% may be inaccessible in restricted Windows environments.
    $env:ZIG_LOCAL_CACHE_DIR = $ZigLocalCache
    $env:ZIG_GLOBAL_CACHE_DIR = $ZigGlobalCache
    # Object paths are shared by all compiler frontends. Reuse them only when
    # the complete Windows build configuration and tool paths are unchanged.
    $VmrunCommand = Get-Command 'vmrun' -ErrorAction SilentlyContinue
    $Vmrun = @(
        $(if ($VmrunCommand) { $VmrunCommand.Source }),
        'C:\Program Files\VMware\VMware Workstation\vmrun.exe',
        'C:\Program Files (x86)\VMware\VMware Workstation\vmrun.exe'
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) } |
        Select-Object -First 1
    if ($Target -ne 'qemu' -and $Vmrun) {
        $runningVms = & $Vmrun -T ws list 2>$null
        if ($runningVms | Select-String -SimpleMatch $PackagedVmx -Quiet) {
            throw "The generated VMware VM is still running. Shut it down before rebuilding: $PackagedVmx"
        }
    }
    $configuration = [ordered]@{
        target = $Target
        video = $Video
        fault_injection = [bool]$FaultInjection
        storage_fault_injection = [bool]$StorageFaultInjection
        storage_io_fault_injection = [bool]$StorageIoFaultInjection
        ahci_fault_injection = [bool]$AhciFaultInjection
        ahci_fault_mode = $AhciFaultMode
        handover_fault_injection = [bool]$HandoverFaultInjection
        handover_node_id = $HandoverNodeId
        dhcp_lease_fault_injection = [bool]$DhcpLeaseFaultInjection
        dhcp_renew_fault_injection = [bool]$DhcpRenewFaultInjection
        vbe_runtime_test = [bool]$VbeRuntimeTest
        nasm = $Nasm
        zig = $Zig
    }
    $configurationJson = $configuration | ConvertTo-Json -Compress
    $previousConfiguration = if (Test-Path -LiteralPath $BuildConfig) {
        Get-Content -LiteralPath $BuildConfig -Raw
    } else { '' }
    $configurationChanged = $previousConfiguration -ne $configurationJson
    if ($Clean) {
        Write-Host 'Full clean requested; removing all generated artifacts...'
        & $Make 'clean' "SHELL=$(To-MakePath $MsysShell)"
        if ($LASTEXITCODE -ne 0) {
            throw "Build cleanup failed with exit code $LASTEXITCODE."
        }
        New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
    } elseif ($configurationChanged) {
        Write-Host 'Build configuration changed; invalidating kernel objects while preserving the userspace SDK and PRGs...'
        Get-ChildItem -LiteralPath $BuildDir -Filter '.config-*' -File |
            ForEach-Object {
                Remove-Item -LiteralPath $_.FullName -Force
            }
    } else {
        Write-Host 'Build configuration unchanged; reusing incremental artifacts.'
    }
    $makeArguments = @(
        'kernel',
        "TARGET=$Target",
        "VIDEO=$Video",
        "SHELL=$(To-MakePath $MsysShell)",
        "AS=$(To-MakePath $Nasm)",
        "CC=$(To-MakePath $Zig) cc -target x86-freestanding -Wno-unused-command-line-argument",
        "LD=$(To-MakePath $Zig) ld.lld",
        'FRAME_WARNING_FLAGS=-Wframe-larger-than=4096 -Werror=frame-larger-than='
    )
    if ($FaultInjection) {
        $makeArguments += 'FAULT_INJECTION=1'
    }
    if ($StorageFaultInjection) {
        $makeArguments += 'STORAGE_FAULT_INJECTION=1'
    }
    if ($StorageIoFaultInjection) {
        $makeArguments += 'STORAGE_IO_FAULT_INJECTION=1'
    }
    if ($AhciFaultInjection) {
        $makeArguments += 'AHCI_FAULT_INJECTION=1'
        $makeArguments += "AHCI_FAULT_MODE=$AhciFaultMode"
    }
    if ($HandoverFaultInjection) {
        $makeArguments += 'HANDOVER_FAULT_INJECTION=1'
        $makeArguments += "HANDOVER_NODE_ID=$HandoverNodeId"
    }
    if ($DhcpLeaseFaultInjection) {
        $makeArguments += 'DHCP_LEASE_FAULT_INJECTION=1'
    }
    if ($DhcpRenewFaultInjection) {
        $makeArguments += 'DHCP_RENEW_FAULT_INJECTION=1'
    }
    if ($VbeRuntimeTest) {
        $makeArguments += 'VBE_RUNTIME_TEST=1'
    }
    & $Make @makeArguments
    if ($LASTEXITCODE -ne 0) {
        throw "Kernel build failed with exit code $LASTEXITCODE."
    }
    Set-Content -LiteralPath $BuildConfig -Value $configurationJson -NoNewline

    & $Nasm -f bin 'arch/x86/boot/bios/stage1_mbr.asm' -o $Stage1
    if ($LASTEXITCODE -ne 0) { throw 'Stage 1 assembly failed.' }
    $stage2Arguments = @('-f', 'bin')
    if ($Video -eq 'framebuffer') {
        $stage2Arguments += '-DUSE_FRAMEBUFFER'
    }
    $stage2Arguments += @('arch/x86/boot/bios/stage2_bios.asm', '-o', $Stage2)
    & $Nasm @stage2Arguments
    if ($LASTEXITCODE -ne 0) { throw 'Stage 2 assembly failed.' }
    & $Nasm -f bin 'arch/x86/boot/bios/stage1_floppy.asm' -o $FloppyStage1
    if ($LASTEXITCODE -ne 0) { throw 'Floppy stage 1 assembly failed.' }

    New-Item -ItemType Directory -Force -Path $UserProgramDir | Out-Null
    & $Python 'scripts/build_system_programs.py' `
        --output-dir $UserProgramDir `
        --zig $Zig `
        --incremental
    if ($LASTEXITCODE -ne 0) {
        throw "System program build failed with exit code $LASTEXITCODE."
    }
    $programBuildArguments = @(
        'scripts/build_user_program.py'
    ) + $ProgramSource + @('--output', $UserPrg, '--zig', $Zig, '--incremental')
    & $Python @programBuildArguments
    if ($LASTEXITCODE -ne 0) {
        throw "Example user program build failed with exit code $LASTEXITCODE."
    }

    $systemLayout = [ordered]@{
        'bin/shell.prg' = 'SHELL.PRG'; 'bin/ls.prg' = 'LS.PRG'
        'bin/cat.prg' = 'CAT.PRG'; 'bin/basic.prg' = 'BASIC.PRG'
        'bin/edit.prg' = 'EDIT.PRG'; 'bin/pwd.prg' = 'PWD.PRG'
        'bin/mkdir.prg' = 'MKDIR.PRG'; 'bin/rmdir.prg' = 'RMDIR.PRG'
        'bin/del.prg' = 'DEL.PRG'; 'bin/copy.prg' = 'COPY.PRG'
        'bin/rename.prg' = 'RENAME.PRG'; 'bin/stat.prg' = 'STAT.PRG'
        'bin/df.prg' = 'DF.PRG'; 'bin/touch.prg' = 'TOUCH.PRG'
        'bin/tree.prg' = 'TREE.PRG'; 'bin/find.prg' = 'FIND.PRG'
        'bin/rm.prg' = 'RM.PRG'
        'bin/echo.prg' = 'ECHO.PRG'; 'bin/cls.prg' = 'CLS.PRG'
        'sbin/sysinfo.prg' = 'SYSINFO.PRG'; 'sbin/usbinfo.prg' = 'USBINFO.PRG'
        'sbin/audioinfo.prg' = 'AUDIOINFO.PRG'
        'sbin/meminfo.prg' = 'MEMINFO.PRG'
        'sbin/chkdsk.prg' = 'CHKDSK.PRG'; 'sbin/fdisk.prg' = 'FDISK.PRG'
        'sbin/format.prg' = 'FORMAT.PRG'; 'sbin/ps.prg' = 'PS.PRG'
        'sbin/kill.prg' = 'KILL.PRG'; 'sbin/drives.prg' = 'DRIVES.PRG'
        'sbin/devctl.prg' = 'DEVCTL.PRG'; 'sbin/mount.prg' = 'MOUNT.PRG'
        'sbin/umount.prg' = 'UMOUNT.PRG'; 'sbin/svcctl.prg' = 'SVCCTL.PRG'
        'sbin/ifconfig.prg' = 'IFCONFIG.PRG'; 'sbin/ping.prg' = 'PING.PRG'
        'sbin/netstat.prg' = 'NETSTAT.PRG'
        'sbin/udp.prg' = 'UDP.PRG'
        'sbin/nslookup.prg' = 'NSLOOKUP.PRG'
        'sbin/nc.prg' = 'NC.PRG'
        'sbin/httpd.prg' = 'HTTPD.PRG'
        'usr/bin/repeat.prg' = 'REPEAT.PRG'; 'usr/bin/calc.prg' = 'CALC.PRG'
        'usr/bin/date.prg' = 'DATE.PRG'; 'usr/bin/uptime.prg' = 'UPTIME.PRG'
        'usr/bin/ascii.prg' = 'ASCII.PRG'; 'usr/bin/save.prg' = 'SAVE.PRG'
        'usr/bin/spawn.prg' = 'SPAWN.PRG'
        'usr/bin/audiotest.prg' = 'AUDIOTEST.PRG'
        'usr/bin/wavplay.prg' = 'WAVPLAY.PRG'
        'usr/gui/bin/desktop.prg' = 'DESKTOP.PRG'
        'usr/gui/bin/guidemo.prg' = 'GUIDEMO.PRG'
        'usr/gui/bin/notepad.prg' = 'NOTEPAD.PRG'
        'usr/gui/bin/soundplayer.prg' = 'SOUNDPLAYER.PRG'
        'libexec/reist/childex.prg' = 'CHILDEX.PRG'
        'libexec/reist/faultde.prg' = 'FAULTDE.PRG'
        'libexec/reist/faultud.prg' = 'FAULTUD.PRG'
        'libexec/reist/faultpf.prg' = 'FAULTPF.PRG'
        'libexec/reist/faultstk.prg' = 'FAULTSTK.PRG'
        'libexec/reist/gtest.prg' = 'GTEST.PRG'
        'libexec/reist/reist.prg' = 'REIST.PRG'
        'libexec/reist/storage.prg' = 'STORAGE.PRG'
        'libexec/reist/hda.prg' = 'HDA.PRG'
        'libexec/reist/audio.prg' = 'AUDIO.PRG'
        'libexec/reist/sleeper.prg' = 'SLEEPER.PRG'
        'libexec/reist/satawr.prg' = 'SATAWR.PRG'
    }
    $imageDataArguments = @(
        '--data-file', "usr/bin/$($ProgramName.ToLowerInvariant())=$UserPrg"
    )
    $testTonePath = Join-Path $RepoRoot 'assets\audio\testtone-440hz-mono-48k-s16.wav'
    $imageDataArguments += @(
        '--data-file', "usr/share/sounds/440hz.wav=$testTonePath"
    )
    $floppyDataArguments = @(
        '--data-file', "usr/bin/$($ProgramName.ToLowerInvariant())=$UserPrg"
    )
    $floppyExcluded = @(
        'sbin/audioinfo.prg', 'usr/bin/audiotest.prg', 'usr/bin/wavplay.prg',
        'usr/gui/bin/soundplayer.prg',
        'libexec/reist/hda.prg', 'libexec/reist/audio.prg'
    )
    foreach ($entry in $systemLayout.GetEnumerator()) {
        $imageDataArguments += @(
            '--data-file', "$($entry.Key)=$(Join-Path $UserProgramDir $entry.Value)"
        )
        if ($entry.Key -notin $floppyExcluded) {
            $floppyDataArguments += @(
                '--data-file', "$($entry.Key)=$(Join-Path $UserProgramDir $entry.Value)"
            )
        }
    }
    foreach ($configFile in @('system.conf', 'input.conf', 'desktop.conf', 'filetypes.conf')) {
        $configPath = Join-Path $RepoRoot "config\etc\reist\$configFile"
        $imageDataArguments += @(
            '--data-file', "etc/reist/$configFile=$configPath"
        )
        $floppyDataArguments += @(
            '--data-file', "etc/reist/$configFile=$configPath"
        )
    }
    foreach ($demoFile in @('about.txt', 'readme.txt', 'status.jsn')) {
        $demoPath = Join-Path $RepoRoot "htdocs\$demoFile"
        $imageDataArguments += @(
            '--data-file', "htdocs/$demoFile=$demoPath"
        )
        $floppyDataArguments += @(
            '--data-file', "htdocs/$demoFile=$demoPath"
        )
    }

    $floppyArguments = @(
        'scripts/create_floppy_boot_image.py', '--stage1', $FloppyStage1,
        '--stage2', $Stage2, '--kernel', $Kernel, '--output', $FloppyImage
    ) + $floppyDataArguments
    & $Python @floppyArguments
    if ($LASTEXITCODE -ne 0) {
        throw "Floppy image creation failed with exit code $LASTEXITCODE."
    }

    $nativeArguments = @(
        'scripts/create_native_boot_image.py', '--stage1', $Stage1,
        '--stage2', $Stage2, '--kernel', $Kernel, '--output', $RawImage,
        '--vmdk', $Vmdk, '--floppy', $FloppyImage
    ) + $imageDataArguments
    if ($Target -ne 'qemu') {
        $nativeArguments += @('--vmware-dir', $VmwareDir)
    }
    & $Python @nativeArguments
    if ($LASTEXITCODE -ne 0) {
        throw "Native image creation failed with exit code $LASTEXITCODE."
    }

    # Image generation recreates both VMX files. Restore physical floppy
    # backing after every VMware build instead of silently switching to the
    # packaged image again.
    $effectiveFloppy = $VmwareFloppy
    if ($effectiveFloppy -eq 'Auto') {
        $logicalFloppy = Get-CimInstance Win32_LogicalDisk `
            -Filter "DeviceID='$FloppyDrive'" -ErrorAction SilentlyContinue
        if ($Target -eq 'vmware' -and $logicalFloppy -and
            $logicalFloppy.DriveType -eq 2) {
            $effectiveFloppy = 'Physical'
        } else {
            $effectiveFloppy = 'Image'
        }
    }
    if ($effectiveFloppy -eq 'Physical') {
        foreach ($generatedVmx in @($Vmx, $PackagedVmx)) {
            & (Join-Path $PSScriptRoot 'configure-vmware-fdd.ps1') `
                -Mode Physical -Drive $FloppyDrive -VmxPath $generatedVmx
        }
    }

    if ($RunTests) {
        & $Make 'test-unit' "PYTHON=$(To-MakePath $Python)" `
            "SHELL=$(To-MakePath $MsysShell)"
        if ($LASTEXITCODE -ne 0) {
            throw "Host tests failed with exit code $LASTEXITCODE."
        }
    }
}
finally {
    Pop-Location
}

Write-Host ''
Write-Host 'Native boot artifacts:' -ForegroundColor Green
Write-Host "  Raw BIOS disk: $RawImage"
Write-Host "  BIOS floppy:   $FloppyImage"
Write-Host "  VMware disk:   $Vmdk"
Write-Host "  VMware VM:     $Vmx"
Write-Host "  User PRG:      $UserPrg"
if ($Target -ne 'qemu') {
    Write-Host "  Complete VM:   $PackagedVmx" -ForegroundColor Cyan
    Write-Host "  Double-click:  $(Join-Path $VmwareDir 'START-VMWARE.cmd')"
}
