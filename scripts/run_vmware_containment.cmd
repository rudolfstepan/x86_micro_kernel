@echo off
setlocal
set "VMRUN=C:\Program Files\VMware\VMware Workstation\vmrun.exe"
set "PACKAGE=%~dp0..\build\vmware\reist-os"
set "VMX=%PACKAGE%\reist-os.vmx"
set "SERIAL=%PACKAGE%\vmware-serial.log"

if not exist "%VMRUN%" (
  echo Required VMware Workstation tool vmrun.exe was not found.
  exit /b 2
)
if not exist "%VMX%" (
  echo VMware package is missing; build target vmware first.
  exit /b 2
)

type nul > "%SERIAL%"
"%VMRUN%" -T ws start "%VMX%" nogui
if not "%ERRORLEVEL%"=="0" exit /b 3

pwsh.exe -NoLogo -NoProfile -File "%~dp0run_vmware_containment.ps1" -SourcePackage "%PACKAGE%"
set "MONITOR_RC=%ERRORLEVEL%"

"%VMRUN%" -T ws stop "%VMX%" hard
set "STOP_RC=%ERRORLEVEL%"
if not "%MONITOR_RC%"=="0" exit /b %MONITOR_RC%
if not "%STOP_RC%"=="0" exit /b %STOP_RC%

echo VMWARE CONTAINMENT PASS
exit /b 0
