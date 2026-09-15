@echo off
setlocal EnableExtensions EnableDelayedExpansion
title Yumu Studio - Make Portable

echo.
echo ============================================================
echo   Yumu Studio - Portable Package (runtime engines)
echo ============================================================
echo.

set "SCRIPT_DIR=%~dp0"
set "PROJECT_DIR=%SCRIPT_DIR%.."
set "EXE_PATH=%PROJECT_DIR%\build\Release\YumuStudio.exe"
set "OUT_DIR=%PROJECT_DIR%\YumuStudio_Portable"
set "QT_ROOT=%QT_ROOT%"
set "FFMPEG_EXE="
set "MISSING=0"

if not exist "%EXE_PATH%" if exist "%PROJECT_DIR%\build-verify\Release\YumuStudio.exe" set "EXE_PATH=%PROJECT_DIR%\build-verify\Release\YumuStudio.exe"
if not defined QT_ROOT for /d %%D in ("C:\Qt\6.11.0\msvc2022_64" "C:\Qt\6.10.0\msvc2022_64" "C:\Qt\6.9.0\msvc2022_64" "C:\Qt\6.8.0\msvc2022_64" "C:\Qt\6.7.0\msvc2022_64") do if not defined QT_ROOT if exist "%%~D\bin\windeployqt.exe" set "QT_ROOT=%%~D"
if exist "%PROJECT_DIR%\bin\ffmpeg.exe" set "FFMPEG_EXE=%PROJECT_DIR%\bin\ffmpeg.exe"
if not defined FFMPEG_EXE for /f "delims=" %%F in ('where ffmpeg 2^>nul') do if not defined FFMPEG_EXE set "FFMPEG_EXE=%%F"

if not exist "%EXE_PATH%" (echo [ERROR] Cannot find %EXE_PATH% & exit /b 1)
if not defined QT_ROOT (echo [ERROR] Qt with windeployqt.exe was not found. & echo Set QT_ROOT before running. & exit /b 1)
echo [OK] EXE: %EXE_PATH%
echo [OK] Qt:  %QT_ROOT%

if exist "%OUT_DIR%" rmdir /s /q "%OUT_DIR%"
mkdir "%OUT_DIR%\bin" "%OUT_DIR%\config" "%OUT_DIR%\engines" "%OUT_DIR%\models"
type nul > "%OUT_DIR%\portable.flag"
copy /y "%EXE_PATH%" "%OUT_DIR%\YumuStudio.exe" >nul

"%QT_ROOT%\bin\windeployqt.exe" --release --multimedia --network --no-translations --no-system-d3d-compiler "%OUT_DIR%\YumuStudio.exe"
if errorlevel 1 (echo [ERROR] windeployqt failed. & exit /b 1)
if defined FFMPEG_EXE copy /y "%FFMPEG_EXE%" "%OUT_DIR%\bin\ffmpeg.exe" >nul
if defined VULKAN_SDK if exist "%VULKAN_SDK%\Bin\vulkan-1.dll" copy /y "%VULKAN_SDK%\Bin\vulkan-1.dll" "%OUT_DIR%\" >nul
if exist "%SystemRoot%\System32\vulkan-1.dll" copy /y "%SystemRoot%\System32\vulkan-1.dll" "%OUT_DIR%\" >nul
if exist "%PROJECT_DIR%\scripts\local_asr_runner.py" copy /y "%PROJECT_DIR%\scripts\local_asr_runner.py" "%OUT_DIR%\" >nul
if exist "%PROJECT_DIR%\scripts\requirements-local-asr.txt" copy /y "%PROJECT_DIR%\scripts\requirements-local-asr.txt" "%OUT_DIR%\" >nul

(
echo @echo off
echo cd /d "%%~dp0"
echo start "" "%%~dp0YumuStudio.exe"
) > "%OUT_DIR%\Launch.bat"
(
echo Yumu Studio for Windows - Portable
echo =====================================
echo Start YumuStudio.exe or Launch.bat.
echo This package installs speech engines and models at runtime.
echo Downloaded engines are stored in engines, models in models, settings in config.
) > "%OUT_DIR%\README.txt"

for %%F in (YumuStudio.exe Qt6Core.dll Qt6Gui.dll Qt6Widgets.dll Qt6Multimedia.dll Qt6Network.dll portable.flag) do if exist "%OUT_DIR%\%%F" (echo [OK] %%F) else (echo [MISSING] %%F & set "MISSING=1")
if exist "%OUT_DIR%\local_asr_runner.py" (echo [OK] local_asr_runner.py) else echo [WARN] local_asr_runner.py not bundled
if exist "%OUT_DIR%\bin\ffmpeg.exe" (echo [OK] bin\ffmpeg.exe) else echo [WARN] bin\ffmpeg.exe not bundled
if "%MISSING%"=="1" (echo [ERROR] Portable package is incomplete. & exit /b 1)
echo [OK] Portable package created: %OUT_DIR%
endlocal

