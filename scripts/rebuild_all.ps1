<#
.SYNOPSIS
    一鍵腳本：構建 YumuStudio（支援 GPU Vulkan 加速）
.PARAMETER WhisperSrc
    whisper.cpp 源碼目錄
.PARAMETER WhisperInstall
    whisper.cpp 安裝目錄
.PARAMETER QtPath
    Qt6 MSVC 路徑
.PARAMETER NoGPU
    加此參數則禁用 Vulkan GPU（不推薦，CPU-only）
.EXAMPLE
    # 保留 GPU（推薦，需要先安裝 Vulkan SDK）
    .\rebuild_all.ps1 -WhisperSrc "C:\whisper.cpp"

    # 不要 GPU，速度較慢但無需 Vulkan SDK
    .\rebuild_all.ps1 -WhisperSrc "C:\whisper.cpp" -NoGPU
#>
param(
    [string]$WhisperSrc     = "C:\whisper.cpp",
    [string]$WhisperInstall = "C:\libs\whisper",
    [string]$QtPath         = "C:\Qt\6.6.0\msvc2022_64",
    [switch]$NoGPU          = $false
)

$ErrorActionPreference = "Stop"
function Log($m) { Write-Host "[rebuild] $m" -ForegroundColor Cyan }
function OK($m)  { Write-Host "[OK] $m"       -ForegroundColor Green }
function WARN($m){ Write-Host "[WARN] $m"     -ForegroundColor Yellow }
function ERR($m) { Write-Host "[ERR] $m"      -ForegroundColor Red; exit 1 }

# ─── 檢查 Vulkan SDK ──────────────────────────────────────────────────────────
if (-not $NoGPU) {
    $vkEnv = $env:VULKAN_SDK
    if (-not $vkEnv -or -not (Test-Path $vkEnv)) {
        Write-Host ""
        Write-Host "════════════════════════════════════════════════════════" -ForegroundColor Yellow
        Write-Host " ⚠️  未偵測到 Vulkan SDK（GPU 加速所需）" -ForegroundColor Yellow
        Write-Host "════════════════════════════════════════════════════════" -ForegroundColor Yellow
        Write-Host ""
        Write-Host " 您的 whisper.cpp 是用 Vulkan 支援編譯的，"
        Write-Host " 連結 YumuStudio 需要 Vulkan SDK 提供符號。"
        Write-Host ""
        Write-Host " 解決方法（保留 GPU）："
        Write-Host "   1. 安裝 Vulkan SDK："
        Write-Host "      https://vulkan.lunarg.com/sdk/home#windows" -ForegroundColor Cyan
        Write-Host "   2. 安裝後重新開啟 PowerShell（讓環境變數生效）"
        Write-Host "   3. 再次執行此腳本"
        Write-Host ""
        Write-Host " 或者，如果您接受 CPU-only 模式（速度較慢）："
        Write-Host "   .\rebuild_all.ps1 -WhisperSrc '$WhisperSrc' -NoGPU" -ForegroundColor Gray
        Write-Host ""
        
        $choice = Read-Host "現在自動開啟 Vulkan SDK 下載頁面？(Y/N)"
        if ($choice -eq "Y" -or $choice -eq "y") {
            Start-Process "https://vulkan.lunarg.com/sdk/home#windows"
            Write-Host ""
            Write-Host "請安裝完成後，重新開啟 PowerShell 並再次執行此腳本。" -ForegroundColor Green
        }
        exit 0
    }
    OK "Vulkan SDK 已安裝：$vkEnv"
}

# ─── 驗證 whisper.cpp 源碼 ────────────────────────────────────────────────────
if (-not (Test-Path "$WhisperSrc\CMakeLists.txt")) {
    ERR "找不到 whisper.cpp：$WhisperSrc`n請執行：git clone https://github.com/ggerganov/whisper.cpp $WhisperSrc"
}

# ─── 重新編譯 whisper.cpp ─────────────────────────────────────────────────────
Log "清除舊 whisper build..."
$wb = "$WhisperSrc\build"
if (Test-Path $wb) { Remove-Item -Recurse -Force $wb }

Push-Location $WhisperSrc
if ($NoGPU) {
    Log "Configure whisper.cpp（CPU-only 模式）..."
    cmake -B build `
        -DGGML_VULKAN=OFF -DGGML_CUDA=OFF -DGGML_METAL=OFF `
        -DWHISPER_BUILD_TESTS=OFF `
        -DCMAKE_BUILD_TYPE=Release `
        -DCMAKE_INSTALL_PREFIX="$WhisperInstall"
} else {
    Log "Configure whisper.cpp（Vulkan GPU 加速模式）..."
    cmake -B build `
        -DGGML_VULKAN=ON `
        -DGGML_CUDA=OFF `
        -DWHISPER_BUILD_TESTS=OFF `
        -DCMAKE_BUILD_TYPE=Release `
        -DCMAKE_INSTALL_PREFIX="$WhisperInstall"
}

if ($LASTEXITCODE -ne 0) { ERR "whisper.cpp configure 失敗" }

Log "編譯 whisper.cpp（約 5-10 分鐘）..."
cmake --build build --config Release --parallel
if ($LASTEXITCODE -ne 0) { ERR "whisper.cpp 編譯失敗" }

Log "安裝 whisper.cpp → $WhisperInstall"
cmake --install build --config Release
if ($LASTEXITCODE -ne 0) { ERR "whisper.cpp install 失敗" }
Pop-Location
OK "whisper.cpp 編譯安裝完成"

# ─── 構建 YumuStudio ─────────────────────────────────────────────────────────
$yumuRoot = $PSScriptRoot | Split-Path -Parent
Log "清除舊 YumuStudio build..."
$yb = "$yumuRoot\build"
if (Test-Path $yb) { Remove-Item -Recurse -Force $yb }

Push-Location $yumuRoot
Log "Configure YumuStudio..."
cmake -B build `
    -DWHISPER_ROOT="$WhisperInstall" `
    -DCMAKE_PREFIX_PATH="$QtPath"
if ($LASTEXITCODE -ne 0) { ERR "YumuStudio configure 失敗" }

Log "構建 YumuStudio..."
cmake --build build --config Release --parallel
if ($LASTEXITCODE -ne 0) { ERR "YumuStudio 構建失敗" }
Pop-Location

$exe = "$yumuRoot\build\Release\YumuStudio.exe"
if (Test-Path $exe) {
    $mode = if ($NoGPU) { "CPU-only" } else { "Vulkan GPU" }
    OK "================================================"
    OK " YumuStudio.exe 構建成功！（模式：$mode）"
    OK " 位置：$exe"
    OK "================================================"
    $run = Read-Host "立即啟動？(Y/N)"
    if ($run -match "^[Yy]$") { Start-Process $exe }
} else {
    ERR "找不到輸出：$exe"
}
