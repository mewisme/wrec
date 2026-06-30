cmake_minimum_required(VERSION 3.20)
project(wrec VERSION 1.0.0 LANGUAGES CXX RC)

# ponytail: semver must be STRING — unquoted -D0.1.8 is parsed as 0 by CMake/MSVC
set(WREC_VERSION "${PROJECT_VERSION}" CACHE STRING "Version string embedded in wrec.exe")
if(WREC_VERSION MATCHES "^[0-9]+$")
    message(FATAL_ERROR
        "WREC_VERSION looks truncated (${WREC_VERSION}). "
        "Use -DWREC_VERSION:STRING=x.y.z or quote: -D\"WREC_VERSION=x.y.z\"")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "wrec is Windows-only.")
endif()

if(MSVC)
    add_compile_options(/W4 /permissive-)
endif()

# Ninja/single-config generators: default to Release when unset
if(NOT CMAKE_GENERATOR MATCHES "Visual Studio" AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_executable(wrec
    src/res/wrec.rc
    src/main.cpp
    src/app/cli.cpp
    src/app/gui.cpp
    src/app/path_install.cpp
    src/app/record_options.cpp
    src/core/logging.cpp
    src/core/notification.cpp
    src/capture/window_list.cpp
    src/capture/capture_wgc.cpp
    src/capture/capture_printwindow.cpp
    src/capture/capture_source.cpp
    src/capture/d3d_device.cpp
    src/capture/cursor_overlay.cpp
    src/scene/scene.cpp
    src/scene/compositor.cpp
    src/record/hotkeys.cpp
    src/record/frame_synchronizer.cpp
    src/record/recorder_manager.cpp
    src/record/mf_encoder.cpp
    src/record/post_compress.cpp
)

target_include_directories(wrec PRIVATE
    src/res
    src
    src/app
    src/core
    src/capture
    src/scene
    src/record
)

target_link_libraries(wrec PRIVATE
    windowsapp
    d3d11
    dxgi
    dwmapi
    mf
    mfplat
    mfreadwrite
    mfuuid
    shlwapi
    shell32
    psapi
    user32
    gdi32
    comctl32
    comdlg32
    ole32
    uuid
)

target_compile_definitions(wrec PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
    "WREC_VERSION=\"${WREC_VERSION}\""
)
