@echo off
setlocal EnableExtensions EnableDelayedExpansion

echo.
echo ============================================================
echo   Yumu Studio - Build Installer (Inno Setup)
echo ============================================================
echo.

set "SCRIPT_DIR=%~dp0"
set "PROJECT_DIR=%SCRIPT_DIR%.."
set "ISS_PATH=%SCRIPT_DIR%YumuStudio.iss"
set "ISCC="

for %%P in (
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
    "C:\Program Files\Inno Setup 6\ISCC.exe"
    "C:\Program Files (x86)\Inno Setup 5\ISCC.exe"
    "C:\Program Files\Inno Setup 5\ISCC.exe"
) do if not defined ISCC if exist "%%~P" set "ISCC=%%~P"

if not defined ISCC (
    echo [ERROR] Inno Setup compiler ^(ISCC.exe^) not found.
    echo Please install Inno Setup 6 from https://jrsoftware.org/isinfo.php
    exit /b 1
)

if not exist "%PROJECT_DIR%\YumuStudio_Portable\YumuStudio.exe" (
    echo [INFO] Portable package not found. Building it now...
    call "%SCRIPT_DIR%make_portable.bat"
    if errorlevel 1 exit /b 1
)

if not exist "%PROJECT_DIR%\Releases" mkdir "%PROJECT_DIR%\Releases"

echo [INFO] Building installer with: %ISCC%
"%ISCC%" "%ISS_PATH%"
if errorlevel 1 (
    echo [ERROR] Installer build failed.
    exit /b 1
)

echo [OK] Installer created in %PROJECT_DIR%\Releases\
endlocal
