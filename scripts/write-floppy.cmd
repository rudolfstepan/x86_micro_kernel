@echo off
setlocal EnableExtensions

rem Request elevation because Windows restricts raw writes to physical volumes.
fltmc >nul 2>&1
if errorlevel 1 (
    echo Requesting administrator privileges...
    powershell.exe -NoProfile -ExecutionPolicy Bypass -Command ^
        "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

for %%I in ("%~dp0..") do set "PROJECT_ROOT=%%~fI"
set "IMAGE=%PROJECT_ROOT%\build\reist-os-floppy.img"
set "WRITER=%~dp0write-floppy.ps1"

if not exist "%WRITER%" (
    echo ERROR: The PowerShell writer was not found:
    echo   %WRITER%
    pause
    exit /b 1
)

if not exist "%IMAGE%" (
    echo ERROR: The floppy image was not found:
    echo   %IMAGE%
    echo Build it first with scripts\build-windows.ps1.
    pause
    exit /b 1
)

for %%I in ("%IMAGE%") do set "IMAGE_SIZE=%%~zI"
if not "%IMAGE_SIZE%"=="1474560" (
    echo ERROR: The image is %IMAGE_SIZE% bytes instead of 1474560 bytes.
    echo Refusing to write an invalid 1.44-MB floppy image.
    pause
    exit /b 1
)

echo.
echo ================================================================
echo WARNING: Drive A: will be overwritten sector by sector.
echo All existing files on the floppy disk will be destroyed.
echo.
echo Image: %IMAGE%
echo Target: A:
echo ================================================================
echo.
choice /C YN /N /M "Write the image to drive A: [Y/N]? "
if errorlevel 2 (
    echo Cancelled.
    exit /b 0
)

echo.
echo Locking drive A: and writing directly to the physical medium...
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%WRITER%" -ImagePath "%IMAGE%" -Drive "A:"
if errorlevel 1 (
    echo.
    echo ERROR: Writing or verification failed.
    echo Close Explorer windows, check the write-protect tab, and retry.
    pause
    exit /b 1
)

echo You can now remove the disk from drive A:.
pause
exit /b 0
