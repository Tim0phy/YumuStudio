@echo off
setlocal EnableDelayedExpansion
title Yumu Studio - Make Portable

echo.
echo ============================================================
echo   Yumu Studio - Make Portable Package
echo   Copies Qt + whisper + ggml + Vulkan DLLs
echo ============================================================
echo.

:: CONFIG - Edit these paths to match your system
set "SCRIPT_DIR=%~dp0"
set "PROJECT_DIR=%SCRIPT_DIR%.."
set "EXE_PATH=%PROJECT_DIR%\build\Release\YumuStudio.exe"
set "WHISPER_BUILD=E:\timop\Downloads\Yumu_Studio\yumu_studio_win_v8_7\whisper.cpp\build\bin\Release"
set "WHISPER_INSTALL=E:\timop\Downloads\Yumu_Studio\yumu_studio_win_v8_7\whisper_install"
set "OUT_DIR=%PROJECT_DIR%\YumuStudio_Portable"

if not exist "%WHISPER_BUILD%" set "WHISPER_BUILD=E:\timop\Downloads\yumu_studio_win_v8_7\whisper.cpp\build\Release"
if not exist "%WHISPER_BUILD%" set "WHISPER_BUILD=E:\timop\Downloads\yumu_studio_win_v8_7\whisper.cpp\build\bin"

:: AUTO-DETECT Qt
set "QT_ROOT="
for /d %%D in (
    "C:\Qt\6.11.0\msvc2022_64"
    "C:\Qt\6.11.0\msvc2019_64"
    "C:\Qt\6.10.0\msvc2022_64"
    "C:\Qt\6.10.0\msvc2019_64"
    "C:\Qt\6.9.0\msvc2022_64"
    "C:\Qt\6.9.0\msvc2019_64"
    "C:\Qt\6.8.0\msvc2022_64"
    "C:\Qt\6.8.0\msvc2019_64"
    "C:\Qt\6.7.0\msvc2022_64"
    "C:\Qt\6.7.0\msvc2019_64"
    "C:\Qt\6.6.3\msvc2022_64"
    "C:\Qt\6.6.3\msvc2019_64"
    "C:\Qt\6.6.0\msvc2022_64"
    "C:\Qt\6.6.0\msvc2019_64"
    "C:\Qt\6.6.0\mingw_64"
) do (
    if exist "%%~D\bin\windeployqt.exe" (
        set "QT_ROOT=%%~D"
        goto :qt_found
    )
)
echo [!] Qt not found. Enter path:
set /p QT_ROOT="Qt path: "

:qt_found
echo [OK] Qt: %QT_ROOT%

:: VERIFY EXE
if not exist "%EXE_PATH%" (
    echo [!] YumuStudio.exe not found at: %EXE_PATH%
    echo     Enter full path to YumuStudio.exe:
    set /p EXE_PATH="EXE path: "
)
if not exist "%EXE_PATH%" (
    echo [ERROR] Cannot find YumuStudio.exe
    pause & exit /b 1
)
echo [OK] EXE: %EXE_PATH%

:: CREATE OUTPUT DIR
echo.
echo [1/6] Creating output directory...
if exist "%OUT_DIR%" rmdir /s /q "%OUT_DIR%"
mkdir "%OUT_DIR%"
copy "%EXE_PATH%" "%OUT_DIR%\" > nul
echo [OK] Copied YumuStudio.exe

:: WINDEPLOYQT
echo.
echo [2/6] Running windeployqt...
"%QT_ROOT%\bin\windeployqt.exe" ^
    --release ^
    --multimedia ^
    --network ^
    --no-translations ^
    --no-system-d3d-compiler ^
    "%OUT_DIR%\YumuStudio.exe"
echo [OK] windeployqt done

:: WHISPER + GGML DLLs
echo.
echo [3/6] Copying whisper.dll + ggml*.dll...
set "WDLL_COUNT=0"

if exist "%WHISPER_BUILD%" (
    for %%F in ("%WHISPER_BUILD%\*.dll") do (
        copy "%%F" "%OUT_DIR%\" > nul 2>&1
        set /a WDLL_COUNT+=1
        echo     + %%~nxF
    )
    echo [OK] Copied !WDLL_COUNT! DLLs from whisper build
) else (
    echo [!] whisper build not found: %WHISPER_BUILD%
    echo     Enter path to folder containing whisper.dll:
    set /p WHISPER_BUILD="whisper build: "
    if exist "!WHISPER_BUILD!" (
        for %%F in ("!WHISPER_BUILD!\*.dll") do (
            copy "%%F" "%OUT_DIR%\" > nul
            echo     + %%~nxF
        )
    ) else (
        echo [WARN] Not found - manually copy whisper.dll later
    )
)

if exist "%WHISPER_INSTALL%\bin" (
    for %%F in ("%WHISPER_INSTALL%\bin\*.dll") do (
        copy "%%F" "%OUT_DIR%\" > nul 2>&1
        echo     + %%~nxF (install)
    )
)

:: VULKAN DLL
echo.
echo [4/6] Copying Vulkan runtime...
if defined VULKAN_SDK (
    if exist "%VULKAN_SDK%\Bin\vulkan-1.dll" (
        copy "%VULKAN_SDK%\Bin\vulkan-1.dll" "%OUT_DIR%\" > nul
        echo     + vulkan-1.dll (VULKAN_SDK)
    )
)
if exist "C:\Windows\System32\vulkan-1.dll" (
    copy "C:\Windows\System32\vulkan-1.dll" "%OUT_DIR%\" > nul
    echo     + vulkan-1.dll (System32)
)

:: MSVC RUNTIME
echo.
echo [5/6] Copying MSVC runtime...
for %%F in (msvcp140.dll msvcp140_1.dll msvcp140_2.dll vcruntime140.dll vcruntime140_1.dll concrt140.dll) do (
    if exist "C:\Windows\System32\%%F" (
        copy "C:\Windows\System32\%%F" "%OUT_DIR%\" > nul
        echo     + %%F
    )
)
for %%F in (libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll) do (
    if exist "%QT_ROOT%\bin\%%F" (
        copy "%QT_ROOT%\bin\%%F" "%OUT_DIR%\" > nul
        echo     + %%F (MinGW)
    )
)

:: VERIFY
echo.
echo [6/6] Verifying key DLLs...
set "MISSING=0"
for %%F in (Qt6Core.dll Qt6Gui.dll Qt6Widgets.dll Qt6Multimedia.dll Qt6Network.dll whisper.dll ggml.dll) do (
    if exist "%OUT_DIR%\%%F" (
        echo     [OK]      %%F
    ) else (
        echo     [MISSING] %%F
        set "MISSING=1"
    )
)

:: LAUNCHER
(
echo @echo off
echo cd /d "%%~dp0"
echo start "" "YumuStudio.exe"
) > "%OUT_DIR%\Launch.bat"

:: README
(
echo Yumu Studio for Windows - Portable
echo =====================================
echo Usage: Double-click YumuStudio.exe or Launch.bat
echo.
echo AI Transcription:
echo   Model: https://huggingface.co/ggerganov/whisper.cpp
echo   Recommended: ggml-medium.bin
echo   Set model path in Settings.
echo.
echo Translation:
echo   Set OpenAI/Anthropic API key in Settings
echo   or use local Ollama.
echo.
echo Burn subtitles to MP4:
echo   FFmpeg: https://www.gyan.dev/ffmpeg/builds/
echo   Set ffmpeg.exe path in Settings.
) > "%OUT_DIR%\README.txt"

:: DONE
echo.
echo ============================================================
echo   Done!  Location: %OUT_DIR%
echo ============================================================
echo.
set "DLL_COUNT=0"
for %%F in ("%OUT_DIR%\*.dll") do set /a DLL_COUNT+=1
echo Total DLLs: %DLL_COUNT%
echo.
if "%MISSING%"=="1" (
    echo WARNING: Missing DLLs above.
    echo Check WHISPER_BUILD path at top of this script.
    echo.
)
set /p OPEN="Open folder? (Y/N): "
if /i "%OPEN%"=="Y" explorer "%OUT_DIR%"
set /p RUN="Launch YumuStudio? (Y/N): "
if /i "%RUN%"=="Y" start "" "%OUT_DIR%\YumuStudio.exe"
echo.
pause
endlocal
