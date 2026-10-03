# ===========================================================================
#  Nova::Core
# ===========================================================================
#  Platform primitives with no engine-specific knowledge: logging, timing,
#  assertions, and the entity handle system.
#
#  DEPENDENCY RULE (enforced by review, not by tooling, yet):
#  Core may depend on the C++ standard library and vcpkg packages. It must NOT
#  depend on Math, Renderer, Scene or Input. The reason is cycle avoidance: the
#  Renderer will want to assert on GPU state, which means Renderer will want
#  NOVA_ASSERT. If Core needed anything from Renderer to implement logging, that
#  cycle becomes a link error or, worse, a circular header dependency.
#
#  SOURCES ARE LISTED EXPLICITLY - never use file(GLOB ...).
#  WHY: GLOB does not re-run when a file is added or removed unless the whole
#  build directory is deleted. The result is a build that silently compiles a
#  stale source list - you add a .cpp, it is ignored, and you spend an hour
#  wondering why your new class "doesn't exist" when the compiler never saw
#  it. Explicit lists also make the module's dependency surface reviewable.
# ===========================================================================

nova_add_module(Core
    SOURCES
        src/CpuInfo.cpp
        src/Diagnostics.cpp
        src/Log.cpp
        src/Platform.cpp
    HEADERS
        Assert.h
        Handle.h
        HandleRegistry.h
        Log.h
        LogMacros.h
        LogTypes.h
        Platform.h
        Target.h
    DEPENDS
        spdlog::spdlog
)

# ===========================================================================
#  find_package() runs BEFORE nova_add_module, not inside it.
#
#  WHY: vcpkg packages arrive as IMPORTED targets. An imported target does not
#  exist until the config file that declares it has been loaded, so linking
#  spdlog::spdlog before find_package(spdlog) is an error. The module declares
#  WHICH third-party targets it needs (DEPENDS); this is where each one is
#  resolved. Keeping the two lists adjacent is deliberate - a dependency cannot
#  be listed in DEPENDS without a find_package that would fail the configure
#  step if the port were ever removed from vcpkg.json.
#
#  CONFIG rather than MODULE: MODULE mode looks for FindXXX.cmake, which vcpkg
#  does not ship. vcpkg packages ship config packages resolved by CMAKE_PREFIX_PATH,
#  which the toolchain file sets up.
# ===========================================================================
find_package(spdlog CONFIG REQUIRED)