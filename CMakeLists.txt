cmake_minimum_required(VERSION 3.20)
project(YumuStudio VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# whisper.cpp is no longer required at build time. Backends (whisper.cpp CLI,
# Python engines) are fetched or compiled on the user's machine at runtime from
# the in-app Engine Center.
set(YUMU_HAS_VULKAN 0)
set(YUMU_HAS_CUDA 0)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Multimedia MultimediaWidgets Network)

file(GLOB SRC_FILES "src/*.cpp" "src/*.h")
add_executable(YumuStudio WIN32 ${SRC_FILES} src/resources.qrc)
if(WIN32)
    target_sources(YumuStudio PRIVATE src/app.rc)
endif()

target_link_libraries(YumuStudio PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets
    Qt6::Multimedia Qt6::MultimediaWidgets Qt6::Network
)

if(MSVC)
    # /Zc:__cplusplus reports the true C++ standard level (otherwise MSVC keeps
    # __cplusplus == 199711L), which Qt's qcompilerdetection.h requires for C++17.
    target_compile_options(YumuStudio PRIVATE /Zc:__cplusplus /utf-8 /W3 /wd4996)
    target_compile_definitions(YumuStudio PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

target_compile_definitions(YumuStudio PRIVATE
    APP_VERSION="2.0.0"
    APP_NAME="Yumu Studio"
)
install(TARGETS YumuStudio RUNTIME DESTINATION bin)
install(FILES "${CMAKE_SOURCE_DIR}/scripts/local_asr_runner.py" DESTINATION bin/scripts)

# ── Optional developer self-test harness (not built by default) ─────────────
option(YUMU_BUILD_SELFTEST "Build the sandbox download self-test tool" OFF)
if(YUMU_BUILD_SELFTEST AND EXISTS "${CMAKE_SOURCE_DIR}/tests/yumu_selftest.cpp")
    add_executable(yumu_selftest tests/yumu_selftest.cpp
        src/appconfig.cpp src/backendcatalog.cpp src/backendinstaller.cpp
        src/localmodelmanager.cpp src/hardwareprobe.cpp
        src/subtitlerefiner.cpp src/glossary.cpp src/subtitlemodel.cpp src/ffmpegexporter.cpp
        src/videopreview.cpp src/uitheme.cpp src/stylepreviewwidget.cpp
        src/subtitlestylecard.cpp src/subtitlecorrectionengine.cpp src/learnedterms.cpp
        src/appupdatechecker.cpp)
    target_include_directories(yumu_selftest PRIVATE src)
    target_link_libraries(yumu_selftest PRIVATE
        Qt6::Core Qt6::Gui Qt6::Network Qt6::Widgets
        Qt6::Multimedia Qt6::MultimediaWidgets)
    target_compile_definitions(yumu_selftest PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN YUMU_SELFTEST_PROBE)
    if(MSVC)
        target_compile_options(yumu_selftest PRIVATE /Zc:__cplusplus /utf-8 /W3 /wd4996)
    endif()
endif()
