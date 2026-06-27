cmake_minimum_required(VERSION 3.20)
project(wrec VERSION 1.0.0 LANGUAGES CXX)

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
    src/main.cpp
    src/cli.cpp
    src/window_list.cpp
    src/hotkeys.cpp
    src/recorder.cpp
    src/capture_wgc.cpp
    src/capture_printwindow.cpp
    src/d3d_device.cpp
    src/cursor_overlay.cpp
    src/mf_encoder.cpp
    src/logging.cpp
    src/path_install.cpp
    src/record_options.cpp
    src/gui.cpp
)

target_include_directories(wrec PRIVATE src)

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
)
