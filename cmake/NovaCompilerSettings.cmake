# ===========================================================================
#  NovaCompilerSettings.cmake
# ===========================================================================
#  Shared compile/link settings for every Nova target.
#
#  DESIGN NOTE - "Every file should compile independently"
#  ---------------------------------------------------
#  We deliberately do NOT add global include_directories(). The reason is that
#  include directories added at directory scope are inherited by subdirectories
#  and, worse, by anything added later - so a target can silently acquire a
#  dependency it never declared. Target-scoped target_include_directories(...)
#  is the only form that keeps a target's real requirements visible in its own
#  CMakeLists.txt. That file is then the complete, accurate description of what
#  the target needs, which is exactly what makes the dependency graph legible.
# ===========================================================================

include_guard(GLOBAL)

function(nova_configure_target target)
    # -----------------------------------------------------------------------
    # Language standard
    # -----------------------------------------------------------------------
    # Declared PUBLIC: if Nova::Math exposes std::span or concepts in its
    # interface, every consumer needs C++20 too. Declaring it here means the
    # requirement propagates automatically instead of being duplicated per
    # target and eventually forgotten.
    target_compile_features(${target} PUBLIC cxx_std_20)

    # -----------------------------------------------------------------------
    # MSVC runtime
    # -----------------------------------------------------------------------
    # WHY explicit: MSVC offers three CRT models (/MT static, /MD dynamic,
    # /MDd debug dynamic). Mixing them across DLL boundaries is a source of
    # heap corruption that is extremely painful to diagnose. Setting this as a
    # target property (rather than a global flag) makes CMake emit the correct
    # /MD or /MDd automatically for every configuration.
    #
    # We choose the DYNAMIC runtime to match vcpkg's default x64-windows
    # triplet. A static (/MT) runtime would require vcpkg's x64-windows-static
    # triplet and matching static builds of every dependency; mixing a /MD
    # vcpkg package into a /MT engine produces exactly the mismatch above.
    set_target_properties(${target} PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

    # Only meaningful to Visual Studio, but harmless under Ninja. Makes the IDE
    # group sources under a "Source Files" filter tree instead of a flat list.
    set_target_properties(${target} PROPERTIES FOLDER_HEADERGUARD ON)
endfunction()

function(nova_add_module target)
    cmake_parse_arguments(ARG "" "" "SOURCES;HEADERS;DEPENDS" ${ARGN})

    add_library(${target} STATIC ${ARG_SOURCES} ${ARG_HEADERS})

    nova_configure_target(${target})
    nova_apply_warnings(${target})

    # -----------------------------------------------------------------------
    # Public (interface) headers
    # -----------------------------------------------------------------------
    # NOVA_PUBLIC_INCLUDE_ROOT is set by Engine/CMakeLists.txt to the Engine/
    # directory, NOT to Engine/Core/. The distinction matters: every module
    # shares one include root, so headers are addressed as <Core/Log.h>,
    # <Math/Vec3.h>, <Renderer/Device.h>. Adding a module therefore introduces
    # a new subdirectory under an existing root rather than a second root, and
    # two files with the same basename in different modules can never collide
    # in the include search path.
    #
    # src/ stays PRIVATE: implementation headers are unreachable from outside
    # the module, so a private helper can be renamed without touching anything
    # else.
    target_include_directories(${target}
        PUBLIC  "${NOVA_PUBLIC_INCLUDE_ROOT}"
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

    # Every module links Windows system libraries explicitly rather than
    # relying on the implicit default set. Implicit linkage breaks the moment
    # one module stops including <windows.h> for transitive reasons.
    if(WIN32)
        target_link_libraries(${target} PUBLIC kernel32)
    endif()

    foreach(dep IN LISTS ARG_DEPENDS)
        target_link_libraries(${target} PUBLIC ${dep})
    endforeach()

    add_library(Nova::${target} ALIAS ${target})

    set_target_properties(${target} PROPERTIES
        OUTPUT_NAME "Nova${target}")
endfunction()