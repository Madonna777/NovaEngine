# ===========================================================================
#  Nova::Core
# ===========================================================================
#  Platform primitives with no engine-specific knowledge: logging, timing,
#  assertions, the entity handle system, and the window / input / main-loop
#  layer the executables are built on.
#
#  DEPENDENCY RULE (enforced by review, not by tooling, yet):
#  Core may depend on the C++ standard library and vcpkg packages. It must NOT
#  depend on Math, Renderer, Scene or Input MODULES. The reason is cycle
#  avoidance: the Renderer will want to assert on GPU state, which means
#  Renderer will want NOVA_ASSERT. If Core needed anything from Renderer to
#  implement logging, that cycle becomes a link error or, worse, a circular
#  header dependency.
#
#  A NOTE ON THAT RULE'S WORDING: Input.h now lives inside Core, so the old
#  sentence - which forbade Core from depending on "Input" - would have been
#  self-contradictory. What the rule protects is inter-module dependency, and
#  that is what this now says. Keyboard and mouse belong here because they need
#  a window handle to poll; a separate Input module would mean Core -> Input ->
#  Core the moment Input also wanted an assertion.
#
#  SOURCES ARE LISTED EXPLICITLY - never use file(GLOB ...).
#  WHY: GLOB does not re-run when a file is added or removed unless the whole
#  build directory is deleted. The result is a build that silently compiles a
#  stale source list - you add a .cpp, it is ignored, and you spend an hour
#  wondering why your new class "doesn't exist" when the compiler never saw
#  it. Explicit lists also make the module's dependency surface reviewable.
#
#  AND THE LIST IS ADDED TO IN THE SAME COMMIT AS THE FILE ITSELF, which is the
#  second reason it is explicit. A source that is listed before it exists fails
#  at CONFIGURE time with "Cannot find source file" - loud, but it still breaks
#  every commit in between. Writing this module's history one commit at a time
#  and configuring each commit caught exactly that, twice.
# ===========================================================================

nova_add_module(Core
    SOURCES
        src/Application.cpp
        src/CpuInfo.cpp
        src/Diagnostics.cpp
        src/GlfwRuntime.cpp
        src/Input.cpp
        src/KeyCodes.cpp
        src/Log.cpp
        src/Platform.cpp
        src/Window.cpp
    HEADERS
        Application.h
        Assert.h
        GlfwRuntime.h
        Handle.h
        HandleRegistry.h
        Input.h
        InputGlfw.h
        KeyCodes.h
        Log.h
        LogMacros.h
        LogTypes.h
        Platform.h
        Target.h
        Window.h
    DEPENDS
        spdlog::spdlog
        glfw
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
#
#  NOTE THE TARGET NAME: glfw, NOT glfw3. The port is called glfw3 and the
#  config package is glfw3Config.cmake, but the exported target is `glfw`. That
#  mismatch is upstream's, not vcpkg's, and it is the kind of thing that fails
#  at configure time with "Target not found" - so it is written down here.
#
#  The x64-windows triplet uses dynamic library linkage, so this resolves to
#  glfw3dll.lib plus glfw3.dll. CMake copies an IMPORTED shared library next to
#  the executable automatically (3.21+), so the DLL needs no manual staging rule
#  - unlike the vcpkg debug DLLs, which no target links and which do still have
#  to be on PATH.
# ===========================================================================
find_package(spdlog CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)