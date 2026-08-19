@echo off
setlocal EnableExtensions

rem Exact approved target as exposed by the current USB adapter.
rem Do not generalize these values.
set "DISK_NUMBER=3"
set "EXPECTED_SERIAL=000000001F4C"
set "EXPECTED_SIZE=120034121728"

for %%I in ("%~dp0..") do set "PROJECT_ROOT=%%~fI"
set "IMAGE=%PROJECT_ROOT%\build\reist-os.img"
set "INSTALLER=%~dp0install-physical-disk.ps1"

if not exist "%INSTALLER%" (
    echo ERROR: Installer not found: %INSTALLER%
    pause
    exit /b 1
)
if not exist "%IMAGE%" (
    echo ERROR: Real-hardware image not found: %IMAGE%
    echo Build it first with:
    echo   scripts\build-windows.ps1 -Target real_hw -Video vga
    pause
    exit /b 1
)
for %%I in ("%IMAGE%") do set "IMAGE_SIZE=%%~zI"
if not "%IMAGE_SIZE%"=="67108864" (
    echo ERROR: Expected a 67108864-byte REIST image, got %IMAGE_SIZE% bytes.
    pause
    exit /b 1
)

fltmc >nul 2>&1
if errorlevel 1 (
    echo Requesting administrator privileges...
    powershell.exe -NoProfile -ExecutionPolicy Bypass -Command ^
        "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

echo.
echo ======================================================================
echo DESTRUCTIVE REIST OS INSTALLATION
echo.
echo Target : PhysicalDrive%DISK_NUMBER%
echo Model  : Samsung SSD 840 Series
echo Serial : %EXPECTED_SERIAL%
echo Size   : %EXPECTED_SIZE% bytes
echo Image  : %IMAGE%
echo.
echo ALL EXISTING PARTITIONS AND DATA ON THIS DISK WILL BE LOST.
echo The old primary and backup GPT structures will be overwritten.
echo ======================================================================
echo.
choice /C YN /N /M "Permanently erase this exact disk and install REIST OS [Y/N]? "
if errorlevel 2 (
    echo Cancelled. No disk was changed.
    pause
    exit /b 0
)

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%INSTALLER%" ^
    -DiskNumber %DISK_NUMBER% ^
    -ExpectedSerial "%EXPECTED_SERIAL%" ^
    -ExpectedSize %EXPECTED_SIZE% ^
    -ImagePath "%IMAGE%" ^
    -ConfirmDestructive
if errorlevel 1 (
    echo.
    echo ERROR: Installation or readback verification failed.
    echo Keep the disk disconnected from the target PC until investigated.
    pause
    exit /b 1
)

echo.
echo REIST OS was written and verified successfully.
echo Disconnect the USB adapter, install the SSD in the target PC,
echo and select the disk as the first legacy BIOS boot device.
exit /b 0
