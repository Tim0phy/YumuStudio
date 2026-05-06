<#
.SYNOPSIS
    重新編譯 whisper.cpp 並明確啟用 Vulkan GPU，解決 LNK2019 ggml_backend_vk_reg
.DESCRIPTION
    你的情況：
      - Vulkan SDK 已安裝 ✅
      - ggml.lib 內含 Vulkan 呼叫 ✅
      - 但缺少 ggml-vulkan.lib（因為 whisper.cpp 沒用 -DGGML_VULKAN=ON 編譯）❌
    解法：加 -DGGML_VULKAN=ON 重新編譯 whisper.cpp
.PARAMETER WhisperSrc
    whisper.cpp 源碼目錄（預設與此腳本同級的 whisper.cpp 目錄）
.EXAMPLE
    .\rebuild_whisper_vulkan.ps1 -WhisperSrc "E:\timop\Downloads\yumu_studio_win_v8_7\whisper.cpp"
#>
param(
    [string]$WhisperSrc     = "",
    [string]$WhisperInstall = "",   # 留空則原地 build，不 install
    [string]$QtPath         = "C:\Qt\6.6.0\msvc2022_64",
    [string]$YumuRoot       = ""    # 留空則自動偵測（scripts/ 上層）
)

$ErrorActionPreference = "Stop"
function Log($m)  { Write-Host "[whisper-rebuild] $m" -ForegroundColor Cyan }
function OK($m)   { Write-Host "[OK] $m"               -ForegroundColor Green }
function ERR($m)  { Write-Host "[ERR] $m"              -ForegroundColor Red; exit 1 }
function WARN($m) { Write-Host "[WARN] $m"             -ForegroundColor Yellow }

# ── 自動推斷路徑 ──────────────────────────────────────────────────────────────
if (-not $YumuRoot) {
    $YumuRoot = $PSScriptRoot | Split-Path -Parent
}
if (-not $WhisperSrc) {
    # 嘗試常見位置
    $candidates = @(
        "$YumuRoot\..\whisper.cpp",
        "E:\timop\Downloads\yumu_studio_win_v8_7\whisper.cpp",
        "C:\whisper.cpp"
    )
    foreach ($c in $candidates) {
        if (Test-Path "$c\CMakeLists.txt") { $WhisperSrc = $c; break }
    }
}
if (-not $WhisperSrc -or -not (Test-Path "$WhisperSrc\CMakeLists.txt")) {
    ERR "找不到 whisper.cpp 源碼，請用 -WhisperSrc 參數指定，例如：`n  .\rebuild_whisper_vulkan.ps1 -WhisperSrc 'E:\timop\Downloads\yumu_studio_win_v8_7\whisper.cpp'"
}
$WhisperSrc = (Resolve-Path $WhisperSrc).Path
if (-not $WhisperInstall) { $WhisperInstall = "$WhisperSrc\install" }

Log "whisper.cpp 源碼 : $WhisperSrc"
Log "whisper.cpp 安裝 : $WhisperInstall"
Log "YumuStudio 根目錄: $YumuRoot"

# ── 驗證 Vulkan SDK ───────────────────────────────────────────────────────────
$vkSdk = $env:VULKAN_SDK
if (-not $vkSdk -or -not (Test-Path $vkSdk)) {
    ERR "VULKAN_SDK 環境變數未設定或目錄不存在。`n請安裝 Vulkan SDK：https://vulkan.lunarg.com/sdk/home#windows"
}
OK "Vulkan SDK : $vkSdk"

# ── 刪除舊 whisper build ──────────────────────────────────────────────────────
$wBuild = "$WhisperSrc\build"
if (Test-Path $wBuild) {
    Log "刪除舊 whisper build..."
    Remove-Item -Recurse -Force $wBuild
}

# ── Configure whisper.cpp（明確 GGML_VULKAN=ON）──────────────────────────────
Log "Configure whisper.cpp with -DGGML_VULKAN=ON ..."
Push-Location $WhisperSrc

$cmakeArgs = @(
    "-B", "build",
    "-DGGML_VULKAN=ON",          # 明確啟用 Vulkan GPU 後端
    "-DGGML_CUDA=OFF",            # 不用 CUDA（如需 CUDA 改為 ON）
    "-DGGML_METAL=OFF",
    "-DWHISPER_BUILD_TESTS=OFF",
    "-DWHISPER_BUILD_EXAMPLES=ON",
    "-DCMAKE_BUILD_TYPE=Release",
    "-DCMAKE_INSTALL_PREFIX=$WhisperInstall"
)
& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { ERR "Configure 失敗" }

# ── 編譯 ─────────────────────────────────────────────────────────────────────
Log "編譯 whisper.cpp（約 5-15 分鐘）..."
cmake --build build --config Release --parallel
if ($LASTEXITCODE -ne 0) { ERR "編譯失敗" }

# ── 安裝 ─────────────────────────────────────────────────────────────────────
Log "安裝到 $WhisperInstall ..."
cmake --install build --config Release
if ($LASTEXITCODE -ne 0) { ERR "安裝失敗" }
Pop-Location

# ── 驗證 ggml-vulkan.lib 是否生成 ────────────────────────────────────────────
$vkLib = Get-ChildItem -Recurse "$WhisperSrc\build" -Filter "ggml-vulkan*.lib" -ErrorAction SilentlyContinue
if ($vkLib) {
    OK "ggml-vulkan.lib 已生成：$($vkLib.FullName)"
} else {
    WARN "ggml-vulkan.lib 未在 build/ 找到，但 ggml.lib 可能已內建 Vulkan 符號，繼續..."
}

# ── 重新構建 YumuStudio ───────────────────────────────────────────────────────
Log "刪除舊 YumuStudio build..."
$yBuild = "$YumuRoot\build"
if (Test-Path $yBuild) { Remove-Item -Recurse -Force $yBuild }

Log "Configure YumuStudio (WHISPER_ROOT=$WhisperInstall) ..."
Push-Location $YumuRoot
cmake -B build `
    "-DWHISPER_ROOT=$WhisperInstall" `
    "-DCMAKE_PREFIX_PATH=$QtPath"
if ($LASTEXITCODE -ne 0) { ERR "YumuStudio Configure 失敗" }

Log "Build YumuStudio..."
cmake --build build --config Release --parallel
if ($LASTEXITCODE -ne 0) { ERR "YumuStudio 構建失敗" }
Pop-Location

# ── 完成 ─────────────────────────────────────────────────────────────────────
$exe = "$YumuRoot\build\Release\YumuStudio.exe"
if (Test-Path $exe) {
    OK "=================================================="
    OK " ✅ YumuStudio.exe 構建成功！（Vulkan GPU 加速）"
    OK " 位置：$exe"
    OK "=================================================="
} else {
    ERR "找不到輸出：$exe"
}
