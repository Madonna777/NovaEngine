# ===========================================================================
#  NovaEngine - Root Build Script
# ===========================================================================
#  WHY THIS FILE IS DELIBERATELY THIN
#  ------------------------------------
#  A root CMakeLists that grows logic is a root CMakeLists nobody can reason
#  about. This file does exactly four things:
#     1. Selects the toolchain (vcpkg) BEFORE project() - order is mandatory.
#     2. Declares the project and the language standard.
#     3. Exposes build options.
#     4. Recurses into the module directories.
#
#  All compiler flag policy lives in cmake/NovaCompilerSettings.cmake so that
#  every module inherits identical settings. Duplicated flags across modules
#  inevitably drift apart, and a warning that only fires in Renderer is worse
#  than no warning at all.
# ===========================================================================

cmake_minimum_required(VERSION 3.28...4.4)

# ---------------------------------------------------------------------------
# 1. TOOLCHAIN SELECTION (must precede project())
# ---------------------------------------------------------------------------
# WHY: vcpkg integrates by injecting a CMake toolchain file, which is read once
# during project(). Setting it afterwards has no effect - a classic silent
# failure where the build "works" but never sees vcpkg's find_package() config
# files. We auto-detect from $ENV{VCPKG_ROOT} only as a convenience for CLI
# builds; if you drive the build from CMakePresets.json the preset supplies the
# path explicitly and this block never runs.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VCPKG_ROOT})
        set(CMAKE_TOOLCHAIN_FILE
            "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
            CACHE STRING "vcpkg toolchain file" FORCE)
        message(STATUS "NovaEngine: using vcpkg from VCPKG_ROOT=$ENV{VCPKG_ROOT}")
    else()
        message(WARNING
            "NovaEngine: vcpkg toolchain not specified and VCPKG_ROOT is unset.\n"
            "  Set VCPKG_ROOT, or configure with CMakePresets.json, or pass\n"
            "  -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake\n"
            "  Manifest mode is REQUIRED so find_package(glfw3) resolves.")
    endif()
endif()

project(NovaEngine
        VERSION 0.1.0
        DESCRIPTION "Commercial-grade 3D game engine"
        LANGUAGES CXX)

# ---------------------------------------------------------------------------
# 2. GLOBAL DEFAULTS
# ---------------------------------------------------------------------------
# WHY C++20 and not "latest": pinning the standard makes builds reproducible.
# A future MSVC bump to C++23 could otherwise silently change overload
# resolution and break the build on a machine you didn't touch.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# WHY: Binaries land in one predictable place instead of mirroring the module
# tree. With a multi-config generator you get bin/<Config>/, which is what
# tools that need to locate Sandbox.exe and its DLLs alongside expect.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Single-config generators (Ninja, Makefiles) have no notion of a default
# configuration. Debug keeps asserts and Tracy symbols; RelWithDebInfo is the
# correct default for an engine because it keeps optimisation while retaining
# the symbols you need to debug optimised builds - a plain Release build with
# no PDB is close to useless for diagnosing a GPU-side bug.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
                 Debug RelWithDebInfo Release MinSizeRel)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(NovaCompilerSettings)
include(NovaWarnings)

# ---------------------------------------------------------------------------
# 3. BUILD OPTIONS
# ---------------------------------------------------------------------------
option(NOVA_BUILD_SANDBOX "Build the Sandbox test application" ON)
option(NOVA_BUILD_EDITOR  "Build the Editor application"        ON)
option(NOVA_BUILD_TESTS   "Build unit tests"                    OFF)

# ---------------------------------------------------------------------------
# 4. MODULES
# ---------------------------------------------------------------------------
# WHY the Engine is a library and the apps are thin: every app links the same
# compiled code, so a fix to the renderer ships to both Sandbox and Editor
# without duplication.
add_subdirectory(Engine)

if(NOVA_BUILD_SANDBOX)
    add_subdirectory(Sandbox)
endif()

if(NOVA_BUILD_EDITOR)
    add_subdirectory(Editor)
endif()

if(NOVA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()

message(STATUS "")
message(STATUS "NovaEngine ${PROJECT_VERSION}")
message(STATUS "  Generator : ${CMAKE_GENERATOR}")
# Multi-config generators (Visual Studio) have no CMAKE_BUILD_TYPE - the config
# is chosen per build. Printing it unconditionally shows a stray empty paren.
if(CMAKE_BUILD_TYPE)
    message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
else()
    message(STATUS "  Build type: multi-config (Debug | RelWithDebInfo | Release)")
endif()
message(STATUS "  Sandbox   : ${NOVA_BUILD_SANDBOX}")
message(STATUS "  Editor    : ${NOVA_BUILD_EDITOR}")
message(STATUS "  Tests     : ${NOVA_BUILD_TESTS}")
message(STATUS "")