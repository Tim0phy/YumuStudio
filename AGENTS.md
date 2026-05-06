# Yumu Studio - Agent Instructions

## Build Commands

```powershell
# Full build with GPU (requires Vulkan SDK)
.\scripts\rebuild_all.ps1 -WhisperSrc "C:\whisper.cpp"

# CPU-only (no Vulkan needed)
.\scripts\rebuild_all.ps1 -WhisperSrc "C:\whisper.cpp" -NoGPU

# Custom Qt path
.\scripts\rebuild_all.ps1 -WhisperSrc "C:\whisper.cpp" -QtPath "C:\Qt\6.11.0\msvc2022_64"
```

## Key Files

| Path | Purpose |
|------|---------|
| `src/*.cpp, src/*.h` | All source code |
| `src/videopreview.cpp` | Video player with timeline slider |
| `src/waveformwidget.cpp` | Timeline with playhead |
| `src/ffmpegexporter.cpp` | Export burned video |
| `CMakeLists.txt` | Build config |
| `scripts/*.ps1` | Build scripts |

## Important Constraints

### MSVC Compilation
- **DO NOT use** raw string literals (`R"(...)"`) in C++ code - MSVC doesn't fully support them
- Use regular string concatenation instead: `"part1" "part2"`

### Windows Path Handling
- Use `QDir::toNativeSeparators(path)` for FFmpeg subprocess
- FFmpeg path escaping: `p.replace("\\", "/").replace(":", "\\:")`
- FFmpeg args must use `-c:v` NOT `-cv`

### Qt Build
- C++17 required (`CMAKE_CXX_STANDARD = 17`)
- Requires Qt6: Core, Gui, Widgets, Multimedia, MultimediaWidgets, Network

### whisper.cpp Optional
- Must define `WHISPER_ROOT` CMake var to enable
- If enabled, requires Vulkan SDK linkage (or compile without `-DGGML_VULKAN=ON`)
- CUDA optional