# 重新構建 whisper.cpp（禁用 Vulkan，避免 LNK2019 ggml_backend_vk_reg 錯誤）
# 在 whisper.cpp 目錄下執行此腳本

param(
    [string]$InstallPrefix = "C:\libs\whisper"
)

Write-Host "[whisper.cpp] Building WITHOUT Vulkan to avoid link errors..." -ForegroundColor Cyan

cmake -B build `
    -DGGML_VULKAN=OFF `
    -DGGML_CUDA=OFF `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_INSTALL_PREFIX="$InstallPrefix"

cmake --build build --config Release --parallel
cmake --install build

Write-Host "[whisper.cpp] Done. Installed to $InstallPrefix" -ForegroundColor Green
Write-Host ""
Write-Host "Now build YumuStudio:" -ForegroundColor Yellow
Write-Host "  cmake -B build -DWHISPER_ROOT=$InstallPrefix -DCMAKE_PREFIX_PATH=C:\Qt\6.6.0\msvc2022_64"
Write-Host "  cmake --build build --config Release --parallel"
