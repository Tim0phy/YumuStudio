# Yumu Studio

> Windows 原生桌面子幕工作站 · Qt 6 · 本地 AI 語音轉文字 → 字幕編輯 → 匯出影片，一條龍完成

[![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11-blue)](../../releases)
[![Qt](https://img.shields.io/badge/Qt-6.7--6.11-green)](https://www.qt.io)
[![License: GPL-3.0](https://img.shields.io/badge/License-GPL--3.0-yellow.svg)](LICENSE.txt)
[![Version](https://img.shields.io/badge/version-2.0.0-orange)](CMakeLists.txt)

Yumu Studio 是一套在 Windows 上運行的影音轉寫／字幕編輯／成品匯出工具。所有 AI 辨識與翻譯都在外部程序中執行，不綁定特定引擎，也不需要額外安裝 Python。只要進入「設定 → 引擎」安裝需要的後端，就能離線使用，字幕與模型都留在自己的電腦上。

> [!NOTE]
> 目前主線版本為 **v2.0.0**；`YumuStudio-main/` 是舊版快照。

---

## 📦 下載與安裝

前往 [Releases](../../releases) 下載對應版本。v2 同時提供**安裝檔**與**便攜包**，首次使用時才會下載引擎與模型。

### 方法一：安裝檔（推薦）

下載 `YumuStudio-2.0.0-Setup.exe`（約 46 MB），雙擊執行：

1. 跟隨安裝精靈選擇安裝路徑（預設 `C:\Program Files\Yumu Studio`）。
2. 可選建立桌面捷徑與開始功能表項目。
3. 安裝完成後從開始功能表或桌面捷徑啟動。
4. 首次啟動會開啟設定頁引導硬體偵測，之後不會再打擾。
5. 到「設定 → 引擎」選擇並安裝變體，同一頁下載模型。
6. 回到工作區載入影片，按 `Ctrl+G` 開始轉寫。

安裝版會在控制台建立解除安裝項目，日後移除更整齊。

### 方法二：便攜包

下載 `YumuStudio_Portable.zip`（約數十 MB）：

1. 解壓到任意可寫入資料夾（不建議放在 `C:\Program Files`，因為需要寫入權限）。
2. 執行 `YumuStudio.exe`。
3. 後續步驟同上（首次引導 → 安裝引擎 → 載入影片 → 轉寫）。

> [!TIP]
> 便攜版靠 `portable.flag` 觸發，所有設定與模型都保存在程式旁的 `config/` 與 `models/`，整個資料夾可直接複製到 USB 隨身碟使用。

### 常用快捷鍵

| 動作 | 快捷鍵 |
|------|--------|
| 開啟影片 | `Ctrl+O` 或拖放檔案 |
| 儲存 SRT | `Ctrl+S` |
| 開始轉寫 | `Ctrl+G`（可隨時停止） |
| 翻譯 | `Ctrl+T` |
| 翻譯預覽微調 | `Ctrl+Shift+T` |
| 字幕時間微調 | `Alt+← / Alt+→`（±100ms，加 `Shift` → ±10ms） |
| 刪除字幕 | `Delete` |
| 搜尋／取代 | `Ctrl+F` / `Ctrl+H` |
| 復原／重做 | `Ctrl+Z` / `Ctrl+Shift+Z` |
| 簡易匯出 | `Ctrl+E` |

---

## ✨ 功能

- ✨ **7 種轉寫引擎，15 個變體**：whisper.cpp、faster-whisper、FunASR、Fun-ASR-Nano、Parakeet、Qwen3-ASR、FireRedASR，支援 CPU / CUDA / Vulkan 加速。首次啟動時自動偵測硬體並建議最佳組合。
- ✨ **Pastel 介面**：粉彩雙主題（淺色／深色），配備漢堡選單、檔案工具列與可調式 3:2 工作區。
- ✨ **預覽與時間軸**：硬體解碼（GPU）或軟體解碼（CPU）雙路徑預覽、真實音波時間軸、1×–64× 縮放、磁吸對齊、場景切點偵測。
- ✨ **字幕編輯**：SRT / VTT / TXT 匯入匯出、全文時間位移、搜尋取代、智慧分段、AI 校正、復原／重做。
- ✨ **AI 翻譯與校正**：OpenAI / Anthropic / Ollama / Gemini 四種後端，可批次翻譯與潤稿。
- ✨ **樣式工坊**：內建 6 組預設樣式，可自訂字體、顏色、描邊、陰影與九宮格定位，預覽即時同步。
- ✨ **匯出**：SRT / VTT / TXT 一鍵下載、MP4 字幕燒錄、ProRes 4444 透明字幕 MOV。
- ✨ **安全強化**：輸入路徑驗證、下載白名單、SHA-256 驗證、API Key 加密儲存、錯誤訊息自動遮蔽敏感資訊。
- **便攜模式**：`portable.flag` 存在時，所有設定與模型都留在程式資料夾內，可隨身攜帶使用。
- ✨ **雙發行格式**：v2 同時提供傳統安裝檔（`.exe`）與免安裝便攜包（`.zip`），可按喜好選擇。

---

## 🖥️ 系統需求

| 項目 | 最低 | 推薦 |
|------|------|------|
| OS | Windows 10/11 64-bit | Windows 10/11 64-bit |
| CPU | x86-64 4 核心 | 8 核心以上 |
| 記憶體 | 4 GB | 8 GB+ |
| 顯示卡 | 無（可用 CPU 模式） | NVIDIA（CUDA 12）或支援 Vulkan 1.1+ |
| 硬碟空間 | 2 GB | 5 GB+（含模型與引擎） |

---

## 🔨 從源碼建置

### 前置需求

| 項目 | 要求 |
|------|------|
| OS | Windows 10/11 64-bit |
| 編譯器 | Visual Studio 2022（「使用 C++ 的桌面開發」工作負載） |
| Qt | 6.7 – 6.11，**MSVC 2022 64-bit** 元件 |
| CMake | 3.20+（VS 2022 已內建） |

> [!IMPORTANT]
> 無需預先準備 whisper.cpp、CUDA Toolkit 或 Vulkan SDK。轉寫引擎均為預編譯包或 pip 套件，在應用內按需要取得；建置只需 Qt 6 + MSVC。

### 一鍵腳本（推薦）

```powershell
cd <專案根目錄>
powershell -ExecutionPolicy Bypass -File scripts\rebuild_all.ps1
# 參數：-QtPath "C:\Qt\6.11.0\msvc2022_64"  -Clean  -Launch
```

### 手動 CMake

```powershell
cmake -B build -S . -DCMAKE_PREFIX_PATH="C:\Qt\6.11.0\msvc2022_64"
cmake --build build --config Release --parallel
```

輸出為 `build\Release\YumuStudio.exe`。詳見 [`docs/BUILD_FROM_SOURCE.md`](docs/BUILD_FROM_SOURCE.md)；製作便攜包見 [`docs/MAKE_PORTABLE.md`](docs/MAKE_PORTABLE.md)。

---

## 🐛 已知問題與計劃改進

以下問題已確認並正在處理中。歡迎透過 [Issues](../../issues) 提供意見或 workaround。

| # | 類型 | 說明 | 狀態 |
|---|------|------|------|
| 1 | Bug / UX | **影片／音訊播放進度條操作不便** — 拖曳軸缺乏精細控制、懸停時不顯示時間戳、長片播放時更新不流暢。 | 已修復 |
| 2 | 功能缺失 | **SRT 字幕燒錄尚未支援** — 目前 `.srt` 字幕檔無法直接燒錄進輸出影片。可單獨匯出 `.srt`，但尚未實作透過 FFmpeg `subtitles` filter 疊加為永久字幕。 | 已新增 |

> 如遇到其他問題或有功能建議，歡迎[開立 Issue](../../issues/new)。

---

## 📝 版本紀錄

### v2.0.0（目前主線）
- 介面改為 Pastel 粉彩雙主題
- 新增 Setup 安裝檔（`.exe`），同時保留便攜包（`.zip`）
- 7 引擎 × 15 變體、硬體自動偵測與建議
- 新增 Gemini 翻譯／校正後端
- 字幕編輯器新增搜尋取代、全文時間位移
- 安全強化：路徑驗證、下載白名單、SHA-256、API Key 加密
- 中／英介面執行期切換

### v1（2026-05-06）
- 時間軸加入時間標記 — 在時間軸上新增時間戳指示器，方便準確導覽與調整播放位置
- 修復 SRT 字幕燒錄進輸出影片 — 解決 `.srt` 字幕檔無法正確燒錄（嵌入／疊加）到匯出影片的問題
- 新增 Ollama 本地模型翻譯支援 — 翻譯功能現可連接本地 Ollama 實例，無需外部 API Key 即可使用本地 LLM 模型翻譯字幕
- 編輯器新增翻譯預覽欄 — 字幕編輯介面新增獨立翻譯欄，可直接在原文旁預覽譯文

### v0.1.8.7（2026-05-02）
- 更新 whisper.cpp 至上游最新版
- 改善 Vulkan GPU 記憶體管理
- 修復特定容器格式下的 FFmpeg 音訊流處理問題
- UI 效能改進

---

## 🤝 貢獻

歡迎提交問題回報、功能建議或程式碼貢獻！

---

## 📄 第三方授權

| 組件 | 授權 | 連結 |
|------|------|------|
| [whisper.cpp](https://github.com/ggml-org/whisper.cpp) | MIT | https://github.com/ggml-org/whisper.cpp/blob/master/LICENSE |
| [FFmpeg](https://ffmpeg.org/) | LGPL v2.1+ | https://ffmpeg.org/legal.html |
| [Vulkan SDK](https://vulkan.lunarg.com/) | Apache 2.0 | https://vulkan.lunarg.com/software/license/vulkan-1.4.321.0-windows-license-summary.txt |
| [faster-whisper / CTranslate2](https://github.com/SYSTRAN/faster-whisper) | MIT | https://github.com/SYSTRAN/faster-whisper |
| [sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx) | Apache 2.0 | https://github.com/k2-fsa/sherpa-onnx |
| [FunASR](https://github.com/modelscope/FunASR) | Apache 2.0 | https://github.com/modelscope/FunASR |

> **FFmpeg LGPL 合規**：本專案動態連結 FFmpeg（`libavcodec` 等）。源碼與二進位檔可從 https://ffmpeg.org 取得。

---

## 📄 授權條款

本專案採用 **GNU General Public License v3.0** 授權 — 詳見 LICENSE 檔案。
