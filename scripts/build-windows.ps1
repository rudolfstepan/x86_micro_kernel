[CmdletBinding()]
param(
    [ValidateSet('qemu', 'vmware', 'real_hw')]
    [string]$Target = 'real_hw',
    [ValidateSet('vga', 'framebuffer')]
    [string]$Video = 'vga',
    [switch]$FaultInjection,
    [switch]$StorageFaultInjection,
    [switch]$StorageIoFaultInjection,
    [switch]$HandoverFaultInjection,
    [ValidateRange(0, 2)]
    [int]$HandoverNodeId = 0,
    [switch]$RunTests,
    [string[]]$ProgramSource = @('examples/userspace/hello.c'),
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

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
Push-Location $RepoRoot
try {
    # GNU Make may execute simple recipe commands directly instead of through
    # SHELL, so the native MSYS2 mkdir/rm/touch/cp tools must also be on PATH.
    $env:Path = "$MsysBin;$env:Path"
    # Object paths are shared by all compiler frontends.  A clean native build
    # prevents same-target objects from an earlier GCC/WSL invocation being
    # silently reused with Zig on Windows.
    $VmrunCommand = Get-Command 'vmrun' -ErrorAction SilentlyContinue
    $Vmrun = @(
        $(if ($VmrunCommand) { $VmrunCommand.Source }),
        'C:\Program Files\VMware\VMware Workstation\vmrun.exe',
        'C:\Program Files (x86)\VMware\VMware Workstation\vmrun.exe'
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) } |
        Select-Object -First 1
    if ($Vmrun) {
        $runningVms = & $Vmrun -T ws list 2>$null
        if ($runningVms | Select-String -SimpleMatch $PackagedVmx -Quiet) {
            throw "The generated VMware VM is still running. Shut it down before rebuilding: $PackagedVmx"
        }
    }
    & $Make 'clean' "SHELL=$(To-MakePath $MsysShell)"
    if ($LASTEXITCODE -ne 0) {
        throw "Build cleanup failed with exit code $LASTEXITCODE."
    }
    $makeArguments = @(
        'kernel',
        "TARGET=$Target",
        "VIDEO=$Video",
        "SHELL=$(To-MakePath $MsysShell)",
        "AS=$(To-MakePath $Nasm)",
        "CC=$(To-MakePath $Zig) cc -target x86-freestanding -Wno-unused-command-line-argument",
        "LD=$(To-MakePath $Zig) ld.lld"
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
    if ($HandoverFaultInjection) {
        $makeArguments += 'HANDOVER_FAULT_INJECTION=1'
        $makeArguments += "HANDOVER_NODE_ID=$HandoverNodeId"
    }
    & $Make @makeArguments
    if ($LASTEXITCODE -ne 0) {
        throw "Kernel build failed with exit code $LASTEXITCODE."
    }

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
        --zig $Zig
    if ($LASTEXITCODE -ne 0) {
        throw "System program build failed with exit code $LASTEXITCODE."
    }
    $programBuildArguments = @(
        'scripts/build_user_program.py'
    ) + $ProgramSource + @('--output', $UserPrg, '--zig', $Zig)
    & $Python @programBuildArguments
    if ($LASTEXITCODE -ne 0) {
        throw "Example user program build failed with exit code $LASTEXITCODE."
    }

    & $Python 'scripts/create_floppy_boot_image.py' `
        --stage1 $FloppyStage1 `
        --stage2 $Stage2 `
        --kernel $Kernel `
        --output $FloppyImage `
        --data-file "$ProgramName=$UserPrg" `
        --data-file "SYSINFO.PRG=$(Join-Path $UserProgramDir 'SYSINFO.PRG')" `
        --data-file "REPEAT.PRG=$(Join-Path $UserProgramDir 'REPEAT.PRG')" `
        --data-file "CALC.PRG=$(Join-Path $UserProgramDir 'CALC.PRG')" `
        --data-file "DATE.PRG=$(Join-Path $UserProgramDir 'DATE.PRG')" `
        --data-file "UPTIME.PRG=$(Join-Path $UserProgramDir 'UPTIME.PRG')" `
        --data-file "MEMINFO.PRG=$(Join-Path $UserProgramDir 'MEMINFO.PRG')" `
        --data-file "ASCII.PRG=$(Join-Path $UserProgramDir 'ASCII.PRG')" `
        --data-file "CAT.PRG=$(Join-Path $UserProgramDir 'CAT.PRG')" `
        --data-file "LS.PRG=$(Join-Path $UserProgramDir 'LS.PRG')" `
        --data-file "SAVE.PRG=$(Join-Path $UserProgramDir 'SAVE.PRG')" `
        --data-file "BASIC.PRG=$(Join-Path $UserProgramDir 'BASIC.PRG')" `
        --data-file "SPAWN.PRG=$(Join-Path $UserProgramDir 'SPAWN.PRG')" `
        --data-file "PS.PRG=$(Join-Path $UserProgramDir 'PS.PRG')" `
        --data-file "KILL.PRG=$(Join-Path $UserProgramDir 'KILL.PRG')" `
        --data-file "PWD.PRG=$(Join-Path $UserProgramDir 'PWD.PRG')" `
        --data-file "SHELL.PRG=$(Join-Path $UserProgramDir 'SHELL.PRG')" `
        --data-file "DESKTOP.PRG=$(Join-Path $UserProgramDir 'DESKTOP.PRG')" `
        --data-file "MKDIR.PRG=$(Join-Path $UserProgramDir 'MKDIR.PRG')" `
        --data-file "RMDIR.PRG=$(Join-Path $UserProgramDir 'RMDIR.PRG')" `
        --data-file "DEL.PRG=$(Join-Path $UserProgramDir 'DEL.PRG')" `
        --data-file "COPY.PRG=$(Join-Path $UserProgramDir 'COPY.PRG')" `
        --data-file "ECHO.PRG=$(Join-Path $UserProgramDir 'ECHO.PRG')" `
        --data-file "CLS.PRG=$(Join-Path $UserProgramDir 'CLS.PRG')" `
        --data-file "DRIVES.PRG=$(Join-Path $UserProgramDir 'DRIVES.PRG')" `
        --data-file "EDIT.PRG=$(Join-Path $UserProgramDir 'EDIT.PRG')" `
        --data-file "CHILDEX.PRG=$(Join-Path $UserProgramDir 'CHILDEX.PRG')" `
        --data-file "FAULTDE.PRG=$(Join-Path $UserProgramDir 'FAULTDE.PRG')" `
        --data-file "FAULTUD.PRG=$(Join-Path $UserProgramDir 'FAULTUD.PRG')" `
        --data-file "FAULTPF.PRG=$(Join-Path $UserProgramDir 'FAULTPF.PRG')" `
        --data-file "FAULTSTK.PRG=$(Join-Path $UserProgramDir 'FAULTSTK.PRG')" `
        --data-file "GTEST.PRG=$(Join-Path $UserProgramDir 'GTEST.PRG')" `
        --data-file "REIST.PRG=$(Join-Path $UserProgramDir 'REIST.PRG')" `
        --data-file "STORAGE.PRG=$(Join-Path $UserProgramDir 'STORAGE.PRG')" `
        --data-file "SLEEPER.PRG=$(Join-Path $UserProgramDir 'SLEEPER.PRG')"
    if ($LASTEXITCODE -ne 0) {
        throw "Floppy image creation failed with exit code $LASTEXITCODE."
    }

    & $Python 'scripts/create_native_boot_image.py' `
        --stage1 $Stage1 `
        --stage2 $Stage2 `
        --kernel $Kernel `
        --output $RawImage `
        --vmdk $Vmdk `
        --vmware-dir $VmwareDir `
        --floppy $FloppyImage `
        --data-file "$ProgramName=$UserPrg" `
        --data-file "SYSINFO.PRG=$(Join-Path $UserProgramDir 'SYSINFO.PRG')" `
        --data-file "REPEAT.PRG=$(Join-Path $UserProgramDir 'REPEAT.PRG')" `
        --data-file "CALC.PRG=$(Join-Path $UserProgramDir 'CALC.PRG')" `
        --data-file "DATE.PRG=$(Join-Path $UserProgramDir 'DATE.PRG')" `
        --data-file "UPTIME.PRG=$(Join-Path $UserProgramDir 'UPTIME.PRG')" `
        --data-file "MEMINFO.PRG=$(Join-Path $UserProgramDir 'MEMINFO.PRG')" `
        --data-file "ASCII.PRG=$(Join-Path $UserProgramDir 'ASCII.PRG')" `
        --data-file "CAT.PRG=$(Join-Path $UserProgramDir 'CAT.PRG')" `
        --data-file "LS.PRG=$(Join-Path $UserProgramDir 'LS.PRG')" `
        --data-file "SAVE.PRG=$(Join-Path $UserProgramDir 'SAVE.PRG')" `
        --data-file "BASIC.PRG=$(Join-Path $UserProgramDir 'BASIC.PRG')" `
        --data-file "SPAWN.PRG=$(Join-Path $UserProgramDir 'SPAWN.PRG')" `
        --data-file "PS.PRG=$(Join-Path $UserProgramDir 'PS.PRG')" `
        --data-file "KILL.PRG=$(Join-Path $UserProgramDir 'KILL.PRG')" `
        --data-file "PWD.PRG=$(Join-Path $UserProgramDir 'PWD.PRG')" `
        --data-file "SHELL.PRG=$(Join-Path $UserProgramDir 'SHELL.PRG')" `
        --data-file "DESKTOP.PRG=$(Join-Path $UserProgramDir 'DESKTOP.PRG')" `
        --data-file "MKDIR.PRG=$(Join-Path $UserProgramDir 'MKDIR.PRG')" `
        --data-file "RMDIR.PRG=$(Join-Path $UserProgramDir 'RMDIR.PRG')" `
        --data-file "DEL.PRG=$(Join-Path $UserProgramDir 'DEL.PRG')" `
        --data-file "COPY.PRG=$(Join-Path $UserProgramDir 'COPY.PRG')" `
        --data-file "ECHO.PRG=$(Join-Path $UserProgramDir 'ECHO.PRG')" `
        --data-file "CLS.PRG=$(Join-Path $UserProgramDir 'CLS.PRG')" `
        --data-file "DRIVES.PRG=$(Join-Path $UserProgramDir 'DRIVES.PRG')" `
        --data-file "EDIT.PRG=$(Join-Path $UserProgramDir 'EDIT.PRG')" `
        --data-file "CHILDEX.PRG=$(Join-Path $UserProgramDir 'CHILDEX.PRG')" `
        --data-file "FAULTDE.PRG=$(Join-Path $UserProgramDir 'FAULTDE.PRG')" `
        --data-file "FAULTUD.PRG=$(Join-Path $UserProgramDir 'FAULTUD.PRG')" `
        --data-file "FAULTPF.PRG=$(Join-Path $UserProgramDir 'FAULTPF.PRG')" `
        --data-file "FAULTSTK.PRG=$(Join-Path $UserProgramDir 'FAULTSTK.PRG')" `
        --data-file "GTEST.PRG=$(Join-Path $UserProgramDir 'GTEST.PRG')" `
        --data-file "REIST.PRG=$(Join-Path $UserProgramDir 'REIST.PRG')" `
        --data-file "STORAGE.PRG=$(Join-Path $UserProgramDir 'STORAGE.PRG')" `
        --data-file "SLEEPER.PRG=$(Join-Path $UserProgramDir 'SLEEPER.PRG')"
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
Write-Host "  Complete VM:   $PackagedVmx" -ForegroundColor Cyan
Write-Host "  User PRG:      $UserPrg"
Write-Host "  Double-click:  $(Join-Path $VmwareDir 'START-VMWARE.cmd')"
