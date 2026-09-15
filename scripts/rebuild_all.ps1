<#
.SYNOPSIS
    一鍵腳本：從源代碼構建 YumuStudio。

.DESCRIPTION
    只構建主程式。轉寫引擎（whisper.cpp / faster-whisper / FunASR 等）
    及模型由程式內的「設定 → 音訊轉寫引擎」於執行時自動下載或本機編譯，
    因此不需要 whisper.cpp 源碼，也不需要 Vulkan SDK。

.PARAMETER QtPath
    Qt6 MSVC 路徑，例如 C:\Qt\6.11.0\msvc2022_64。
    未指定時會自動偵測 C:\Qt 下已安裝的版本。

.PARAMETER Clean
    先刪除現有 build 目錄再完整重建（預設保留並增量構建）。

.PARAMETER Launch
    構建成功後立即啟動程式。

.EXAMPLE
    # 一般建構（最常用）
    .\rebuild_all.ps1

    # 指定 Qt 路徑並在建置後啟動
    .\rebuild_all.ps1 -QtPath "C:\Qt\6.11.0\msvc2022_64" -Launch
#>
param(
    [string]$QtPath         = "",
    [switch]$Clean          = $false,
    [switch]$Launch         = $false
)

$ErrorActionPreference = "Stop"
function Log($m) { Write-Host "[rebuild] $m" -ForegroundColor Cyan }
function OK($m)  { Write-Host "[OK] $m"       -ForegroundColor Green }
function WARN($m){ Write-Host "[WARN] $m"     -ForegroundColor Yellow }
function ERR($m) { Write-Host "[ERR] $m"      -ForegroundColor Red; exit 1 }

# ─── 自動偵測 Qt 安裝位置 ───────────────────────────────────────────────────────
function Test-QtPath($p) {
    if (Test-Path "$p\bin\cmake\Qt6\Qt6Config.cmake") { return $true }
    if (Test-Path "$p\lib\cmake\Qt6\Qt6Config.cmake") { return $true }
    return $false
}
if (-not (Test-QtPath $QtPath)) {
    $candidates = @(
        "C:\Qt\6.11.0\msvc2022_64", "C:\Qt\6.10.0\msvc2022_64",
        "C:\Qt\6.9.0\msvc2022_64",  "C:\Qt\6.8.0\msvc2022_64",
        "C:\Qt\6.7.0\msvc2022_64",  "C:\Qt\6.6.0\msvc2022_64"
    )
    foreach ($c in $candidates) {
        if (Test-QtPath $c) {
            if ($QtPath -ne "") { WARN "指定的 QtPath 無效（$QtPath），改用偵測到的 $c" }
            else { Log "偵測到 Qt6：$c" }
            $QtPath = $c
            break
        }
    }
    if (-not (Test-QtPath $QtPath)) {
        ERR "找不到 Qt6（Qt6Config.cmake）。請用 -QtPath 指定 Qt6 MSVC 路徑，例如 -QtPath `"C:\Qt\6.11.0\msvc2022_64`""
    }
}
OK "Qt6 路徑：$QtPath"

# ─── 檢查 Qt 套件與 MSVC 是否匹配 ─────────────────────────────────────────────
# Qt 的 MSVC 與 MinGW 套件產物不能混用：本專案用 MSVC 建構，
# 因此路徑必須是 msvc 版套件（例如 ...\msvc2022_64），不是 mingw_64。
if ($QtPath -match 'mingw') {
    ERR "Qt 套件與建構工具鏈不符：$QtPath 是 MinGW 版套件，本專案需用 MSVC 版 Qt（例如 C:\Qt\6.x.x\msvc2022_64）。" +
        "請安裝 MSVC 2022 64-bit 套件後用 -QtPath 指定，例如 -QtPath `"C:\Qt\6.11.2\msvc2022_64`""
}

$yumuRoot = $PSScriptRoot | Split-Path -Parent
$yb = "$yumuRoot\build"
if ($Clean -and (Test-Path $yb)) {
    Log "清除舊 YumuStudio build..."
    Remove-Item -Recurse -Force $yb
}

# ─── 構建 YumuStudio ─────────────────────────────────────────────────────────
Push-Location $yumuRoot
Log "Configure YumuStudio..."
cmake -B build -DCMAKE_PREFIX_PATH="$QtPath" -DYUMU_BUILD_SELFTEST=OFF
if ($LASTEXITCODE -ne 0) { ERR "YumuStudio configure 失敗" }

Log "構建 YumuStudio..."
cmake --build build --config Release --parallel
if ($LASTEXITCODE -ne 0) { ERR "YumuStudio 構建失敗" }
Pop-Location

$exe = "$yumuRoot\build\Release\YumuStudio.exe"
if (Test-Path $exe) {
    OK "================================================"
    OK " YumuStudio.exe 構建成功！（模式：標準，引擎於程式內下載）"
    OK " 位置：$exe"
    OK "================================================"
    if ($Launch) { Start-Process $exe }
} else {
    ERR "找不到輸出：$exe"
}
