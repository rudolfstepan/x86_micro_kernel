@echo off
setlocal
set "PACKAGE=%~dp0..\build\vmware\reist-os"
pwsh.exe -NoLogo -NoProfile -File "%~dp0run_vmware_containment.ps1" -SourcePackage "%PACKAGE%"
exit /b %ERRORLEVEL%
