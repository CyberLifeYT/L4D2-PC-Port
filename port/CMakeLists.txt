# l4d2 - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(l4d2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(L4D2_SOURCES
    src/main.cpp
    src/port_app.cpp
    src/overlay.cpp
    src/capture.cpp
    src/gpu_stats.cpp
    src/pcinput.cpp
    src/console.cpp
)

if(WIN32)
    add_executable(l4d2 WIN32 ${L4D2_SOURCES})
else()
    add_executable(l4d2 ${L4D2_SOURCES})
endif()

# Xenos GPU emulation plugin (rexgpu-xenos.dll staged next to the exe).
rexglue_setup_target(l4d2 GPU_PLUGINS xenos)

# Unresolved-import stubs (auto-generated): links DLLs that reference
# import thunks without bodies yet. Stubs abort at runtime if called.
include("${CMAKE_CURRENT_SOURCE_DIR}/src/stubs/stubs.cmake")

# Fill cross-module import slots (tier0/vstdlib data + function imports) at
# each DLL's entry point, like the real Xbox loader does.
include("${CMAKE_CURRENT_SOURCE_DIR}/src/stubs/imports.cmake")

# Default player settings, shipped in the patch as l4d2_settings.default.ini.
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/l4d2_settings.ini"
               "${CMAKE_CURRENT_BINARY_DIR}/l4d2_settings.ini" COPYONLY)
