# yumu_studio

> A Windows desktop application for audio/video processing and transcription, powered by whisper.cpp, FFmpeg, and Vulkan GPU acceleration.

[![GitHub release](https://img.shields.io/github/v/release/YOUR_USERNAME/yumu-studio-win)](../../releases)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11-blue)](../../releases)

---

## 📦 Download & Install

Go to the [**Releases**] page to download the latest portable `.zip`.

**Requirements:**
- Windows 10 / 11 (64-bit)
- GPU with Vulkan 1.1+ support (recommended for hardware acceleration)

**Steps:**
1. Download the portable .zip file
2. Extract the `.zip` to any folder
3. Launch `yumu_studio.exe`

---

## ✨ Features

- 🎙️ Audio/video transcription powered by [whisper.cpp](https://github.com/ggml-org/whisper.cpp)
- ⚡ GPU-accelerated inference via Vulkan backend
- 🎬 Audio/video processing via FFmpeg
- 🪟 Native Windows desktop UI

---

## 🖥️ System Requirements

| Component | Minimum | Recommended |
|-----------|---------|-------------|
| OS | Windows 10/11 (64-bit) | Windows 10/11 (64-bit) |
| CPU | x86-64, 4 cores | x86-64, 8+ cores |
| RAM | 4 GB | 8 GB+ |
| GPU | Vulkan 1.1 compatible | Discrete GPU (NVIDIA/AMD) |
| Storage | 2 GB free | 5 GB+ free |

---

## 🔨 Build from Source

### Prerequisites

- [Git](https://git-scm.com/)
- [CMake](https://cmake.org/) 3.21+
- [Visual Studio 2022](https://visualstudio.microsoft.com/) (with C++ Desktop workload)
- [Vulkan SDK](https://vulkan.lunarg.com/) 1.3+
- [FFmpeg](https://ffmpeg.org/) development libraries (LGPL build, dynamic linking)

### # Build Instructions


```bash
# 1. Clone the Repositories
# Clone whisper.cpp 
git clone https://github.com/ggerganov/whisper.cpp

# You may place it anywhere; this guide uses C:\yumu_deps\whisper.cpp 
# Replace paths below if you choose a different location.
# Download yumu\_studio source

# 2. Build whisper.cpp with Vulkan Support
cd C:\yumu_deps\whisper.cpp

cmake -B build ` -DBUILD_SHARED_LIBS=OFF ` -DGGML_VULKAN=ON

cmake --build build --config Release
#After this step, the compiled static libraries will be inside `C:\yumu_deps\whisper.cpp\build\Release\`.

# 3. Build yumu_studio
cd C:\yumu_studio\scripts

.\rebuild_whisper_vulkan.ps1 ` -WhisperSrc "C:\yumu_deps\whisper.cpp" ` -WhisperInstall "C:\yumu_deps\whisper_install" ` -QtPath "C:\Qt\6.11.0\msvc2022_64" ` -YumuRoot "C:\yumu_studio"


# 4. Create a Portable Package
cd C:\yumu_studio\scripts
.\make_portable.bat

```

---

## 🐛 Known Issues & Planned Improvements

The following issues are confirmed and currently being worked on. Contributions and workarounds are welcome via [Issues](../../issues).

| # | Type | Description | Status |
|---|------|-------------|--------|
| 1 | 🐛 Bug / UX | **Progress bar for video/audio playback is not user-friendly** — The seek bar lacks fine-grained control, does not display timestamps on hover, and may not update smoothly during long media files. | 🔧 In Progress |
| 2 | 🚧 Missing Feature | **SRT subtitle burn-in not yet supported** — Currently, `.srt` subtitle files cannot be hardcoded (burned) into the output video. Exporting subtitles as a separate `.srt` file is supported, but muxing them as permanent overlays via FFmpeg's `subtitles` filter is not yet implemented. | 📋 Planned |

> If you encounter other bugs or have feature requests, please [open an issue](../../issues/new).

---

## 📝 Changelog

### v0.1.8.7 (2026-05-02)
- Updated whisper.cpp to latest upstream
- Improved Vulkan GPU memory management
- Fixed FFmpeg audio stream handling for certain container formats
- UI performance improvements

---

## 🤝 Contributing

Contributions, bug reports, and feature requests are welcome!

---

## 📄 Third-party Licenses

This project uses the following open-source components:

| Component | License | Link |
|-----------|---------|------|
| [whisper.cpp](https://github.com/ggml-org/whisper.cpp) | MIT | https://github.com/ggml-org/whisper.cpp/blob/master/LICENSE |
| [FFmpeg](https://ffmpeg.org/) | LGPL v2.1+ | https://ffmpeg.org/legal.html |
| [Vulkan SDK](https://vulkan.lunarg.com/) | Apache 2.0 | https://vulkan.lunarg.com/software/license/vulkan-1.4.321.0-windows-license-summary.txt |

> **FFmpeg Notice (LGPL compliance):** This software uses libraries from the FFmpeg project under the LGPLv2.1. FFmpeg is linked dynamically. The FFmpeg source code and binaries can be obtained from https://ffmpeg.org.

---

## 📄 License

This project is licensed under the **GNU General Public License v3.0** — see the LICENSE file for details.
