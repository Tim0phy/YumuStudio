cmake_minimum_required(VERSION 3.20)
project(YumuStudio VERSION 8.7.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Multimedia MultimediaWidgets Network)

file(GLOB SRC_FILES "src/*.cpp" "src/*.h")
add_executable(YumuStudio WIN32 ${SRC_FILES})

target_link_libraries(YumuStudio PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets
    Qt6::Multimedia Qt6::MultimediaWidgets Qt6::Network
)

# ── whisper.cpp（可選）────────────────────────────────────────────────────────
if(DEFINED WHISPER_ROOT)
    message(STATUS "[YumuStudio] WHISPER_ROOT = ${WHISPER_ROOT}")

    # 頭文件
    foreach(inc
        "${WHISPER_ROOT}/include"
        "${WHISPER_ROOT}/ggml/include"
        "${WHISPER_ROOT}/build/include"
        "${WHISPER_ROOT}/build/src"
        "${WHISPER_ROOT}/build/ggml/src"
    )
        if(EXISTS "${inc}") 
            target_include_directories(YumuStudio PRIVATE "${inc}")
        endif()
    endforeach()

    # 掃描所有可能的 lib 路徑（包含 install/lib 目錄）
    file(GLOB_RECURSE ALL_WHISPER_LIBS
        "${WHISPER_ROOT}/build/src/Release/*.lib"
        "${WHISPER_ROOT}/build/ggml/src/Release/*.lib"
        "${WHISPER_ROOT}/build/Release/*.lib"
        "${WHISPER_ROOT}/lib/Release/*.lib"
        "${WHISPER_ROOT}/lib/*.lib"
    )

    # 分類
    set(VULKAN_LIBS  "")
    set(OTHER_LIBS   "")
    foreach(L ${ALL_WHISPER_LIBS})
        string(TOLOWER "${L}" LL)
        if(LL MATCHES "vulkan")
            list(APPEND VULKAN_LIBS "${L}")
            message(STATUS "[YumuStudio]  vulkan-lib: ${L}")
        else()
            list(APPEND OTHER_LIBS "${L}")
            message(STATUS "[YumuStudio]  + ${L}")
        endif()
    endforeach()

    target_link_libraries(YumuStudio PRIVATE ${OTHER_LIBS})
    target_compile_definitions(YumuStudio PRIVATE HAVE_WHISPER_LIB)

    # ── Vulkan 處理 ───────────────────────────────────────────────────────────
    # ggml.lib 只要是用 GGML_VULKAN=ON 編譯的，就必須連結 Vulkan SDK
    # 即使沒有獨立的 ggml-vulkan.lib，ggml.lib 內部仍有 Vulkan 符號
    find_package(Vulkan QUIET)
    if(Vulkan_FOUND)
        message(STATUS "[YumuStudio] ✅ Vulkan SDK: ${Vulkan_INCLUDE_DIRS}")
        target_include_directories(YumuStudio PRIVATE ${Vulkan_INCLUDE_DIRS})
        target_link_libraries(YumuStudio PRIVATE Vulkan::Vulkan)
        if(VULKAN_LIBS)
            target_link_libraries(YumuStudio PRIVATE ${VULKAN_LIBS})
        endif()
        message(STATUS "[YumuStudio] ✅ GPU: Vulkan enabled")
    else()
        if(VULKAN_LIBS OR TRUE)   # TRUE = always check, ggml.lib may embed vulkan
            message(FATAL_ERROR
                "[YumuStudio] ❌ Vulkan SDK not found!\n"
                "Your whisper.cpp ggml.lib was compiled with Vulkan support.\n"
                "You MUST install Vulkan SDK to link YumuStudio.\n"
                "Download: https://vulkan.lunarg.com/sdk/home#windows\n"
                "After install: delete build/ and re-run cmake (no recompile needed).")
        endif()
    endif()

    # ── CUDA（可選）──────────────────────────────────────────────────────────
    find_package(CUDAToolkit QUIET)
    if(CUDAToolkit_FOUND)
        target_link_libraries(YumuStudio PRIVATE CUDA::cudart_static CUDA::cublas)
        message(STATUS "[YumuStudio] ✅ CUDA also available")
    endif()
endif()

if(MSVC)
    target_compile_options(YumuStudio PRIVATE /utf-8 /W3 /wd4996)
    target_compile_definitions(YumuStudio PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

target_compile_definitions(YumuStudio PRIVATE APP_VERSION="8.7.0" APP_NAME="Yumu Studio")
install(TARGETS YumuStudio RUNTIME DESTINATION bin)
