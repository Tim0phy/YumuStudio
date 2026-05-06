# Yumu Studio for Windows

A Windows C++/Qt6 subtitle editing tool.

---

##


---

## Features

1. **AI Transcription** — whisper.cpp (CLI mode), offline, 98+ languages
2. **Waveform Timeline** — drag subtitle edges, Ctrl+Wheel zoom (v8.7), auto-snap
3. **Subtitle Editor** — add/delete/split/merge/shift-all, search+replace (v8.7)
4. **Export** — export SRT only
5. **Style Preview** — live preview in export dialog (v8.7)
6. **Drag & Drop** — drop video files onto the window (v8.7)

---

## Build Requirements

| Dependency | Version | Purpose |
|---|---|---|
| Qt | 6.6+ | GUI, multimedia playback |
| whisper.cpp | latest | Offline AI transcription |
| FFmpeg | 6.0+ | Video decoding & subtitle burn |
| CMake | 3.20+ | Build system |
| MSVC | 2022 | Compiler |

---

## Build Instructions

```powershell
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

## Settings

All settings saved to: `%APPDATA%\YumuStudio\settings.ini`

Configure in **Tools → Settings**:
- **Paths tab**: FFmpeg path, whisper-cli.exe path, model .bin path
- **AI Recognition tab**: language, threads, translate flag
- **Translation tab**: backend (OpenAI / Anthropic / Ollama / Gemini), API key, target language

---

## Keyboard Shortcuts

| Key | Action |
|---|---|
| `Ctrl+O` | Open video |
| `Ctrl+G` | Generate subtitles (AI) |
| `Ctrl+T` | Translate subtitles |
| `Ctrl+E` | Export burned video |
| `Ctrl+S` | Export SRT |
| `Ctrl+F` | Search in subtitle editor |
| `Ctrl+H` | Find & Replace |
| `Ctrl+Wheel` | Zoom waveform (1×–32×) |
| `Space` | Play/pause video |
| `Insert` | Add subtitle row |
| `Delete` | Delete selected subtitle |
