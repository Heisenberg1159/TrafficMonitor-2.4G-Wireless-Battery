cmake_minimum_required(VERSION 3.20)
project(WirelessBattery LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT: the DLL drops into TrafficMonitor's plugins folder without any runtime install.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/utf-8;/W4;/permissive->")
add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

add_library(battery_core STATIC
    src/hid_device.cpp
    src/battery_protocols.cpp
    src/protocols/provider_base.cpp
    src/protocols/registry.cpp
    # One file per receiver protocol - add yours here.
    src/protocols/compx_mouse.cpp
    src/protocols/mchose_keyboard.cpp)
target_include_directories(battery_core PUBLIC src src/protocols)
target_link_libraries(battery_core PUBLIC hid setupapi)

add_library(WirelessBattery SHARED
    src/plugin.cpp
    src/battery_monitor.cpp
    src/battery_render.cpp
    src/gdiplus_drawer.cpp
    src/options_dialog.cpp
    src/WirelessBattery.rc)
target_link_libraries(WirelessBattery PRIVATE battery_core shell32 gdiplus msimg32 comctl32)

add_executable(WirelessBatteryCli src/cli.cpp)
target_link_libraries(WirelessBatteryCli PRIVATE battery_core)

# Test host that loads the DLL the way TrafficMonitor does.
add_executable(PluginHostTest src/host_test.cpp)

# Renders the custom-drawn items to a PNG (GDI+ stand-in for the host drawer).
add_executable(RenderPreview src/render_preview.cpp src/battery_render.cpp src/gdiplus_drawer.cpp)
target_include_directories(RenderPreview PRIVATE src)
target_link_libraries(RenderPreview PRIVATE gdiplus msimg32)
