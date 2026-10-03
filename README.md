# NovaEngine

A 3D game engine in C++20 on DirectX 12, for Windows 10/11 x64.

Work-in-progress. The window / input / main-loop layer builds and opens a real
HWND; nothing is rendered yet, because there is no swap chain.

## Build requirements

| Tool | Version | Notes |
|---|---|---|
| Visual Studio | 2026 (MSVC 19.51) | 2022 works via the generator override, but is not the installed toolchain |
| CMake | 4.4.4 | Minimum declared is `3.28...4.4` |
| vcpkg | at `C:\vcpkg` | Manifest mode. `VCPKG_ROOT` must be set |
| Windows SDK | 10.0.26100.0 | Provides `d3d12.lib` / `dxgi.lib` directly |

`cl.exe` is not on the default `PATH` - use the presets, or launch a
"Developer Command Prompt" / "x64 Native Tools" shell.

## Building

```powershell
# Day to day: optimised, symbols kept, incremental
cmake --preset vs2026-relwithdebinfo
cmake --build --preset vs2026-relwithdebinfo
.\build\vs2026-relwithdebinfo\bin\RelWithDebInfo\Sandbox.exe

# Debug
cmake --preset vs2026-debug
cmake --build --preset vs2026-debug

# Unit tests (needs the Tests/ directory - not created yet)
cmake --preset vs2026-tests
```

There are also `ninja-debug` and `ninja-dev` presets. **Ninja is not currently
installed** on this machine; the VS presets work as-is. Once Ninja is on `PATH`
those presets are preferable for iteration, because the Visual Studio generator
re-evaluates the whole project graph on every build. You can still open the
source folder in VS 2026 for debugging - generator and IDE are independent
choices.

Binaries land in `build/<preset>/bin/<Config>/`.

## Layout

    NovaEngine/
    |-- CMakeLists.txt        Thin root: toolchain, standard, options, recursion
    |-- CMakePresets.json
    |-- vcpkg.json            Manifest, with opt-in features per layer
    |-- cmake/
    |   |-- NovaCompilerSettings.cmake   Shared target settings
    |   `-- NovaWarnings.cmake            Central warning policy
    |-- Engine/
    |   |-- Core/            Logging, platform, asserts, handles,
    |   |                    window, input, application loop  [IMPLEMENTED]
    |   |-- Math/            DirectXMath facade                     [NEXT]
    |   |-- Renderer/        D3D12 backend
    |   `-- Scene/           Scene graph, entity storage
    |-- Editor/              Long-lived tool (placeholder)
    |-- Sandbox/             Disposable test app
    |-- Shaders/             HLSL + compilation manifest
    `-- ThirdParty/          Vendored deps - currently none

Keyboard and mouse live in `Core`, not in a separate `Input` module. They need a
window handle to poll, and splitting them out would mean `Core` -> `Input` ->
`Core` the moment `Input` also wanted an assertion.

## Conventions

- `namespace Nova`. `#pragma once`. Public headers included as `<Core/Log.h>`;
  `src/` is private to its module.
- `camelCase` variables, `PascalCase` functions and types, `UPPER_SNAKE`
  constants.
- RAII for anything with a lifetime. Raw pointers never own.
- `NOVA_ASSERT` / `NOVA_VERIFY` rather than `assert`, and they stay enabled in
  release builds. `NOVA_VERIFY` for any expression with side effects.
- Doxygen comments (`///`) on every public API.
- `<windows.h>` in `.cpp` files only, never in headers.
- Explicit source lists in CMake. No `file(GLOB)`.
- Files stay under 200 lines of code (comments excluded). If one is about to
  cross that, split it at a real seam rather than letting it grow.

### Logging

`Nova::Log` has two channels with **independent level filters**:

| Channel  | Macro prefix      | Used by                          |
| -------- | ----------------- | -------------------------------- |
| `Core`   | `NOVA_*`          | everything under `Engine/`       |
| `Client` | `NOVA_CLIENT_*`   | `Sandbox/`, `Editor/`, game code |

Both channels write to one shared sink set, so a Core and a Client record can
never interleave mid-line in a log file. The channel is tagged by the logger
name via the pattern's `%n`.

The point of the split is filtering, not labelling: to raise the engine to Trace
while silencing client logging entirely, set the levels per channel. A category
field on a single logger cannot do that.

Severity and channel enums are in `<Core/LogTypes.h>` and the macros in
`<Core/LogMacros.h>`; both are pulled in by `<Core/Log.h>`, so consumers only
include the one header. `LogTypes.h` exists separately so the assert handler and
the Editor's severity filter can name `LogLevel` without parsing spdlog.

## Module dependency rules

`Core` depends on the standard library and vcpkg only - currently `spdlog` and
`glfw3`. It must not depend on `Math`, `Renderer` or `Scene`: the Renderer will
want to assert on GPU state, so it will want `NOVA_ASSERT`, and a cycle there is
a link error.

Build order is `Math` -> `Renderer` -> `Scene`. Scene comes after Renderer
deliberately: the scene graph allocates from GPU-visible heaps, so the renderer
owns memory and the scene describes what to put in it, not the other way round.

## Windowing and input

| Header | Role |
| --- | --- |
| `Core/Window.h` | RAII window. `GLFW_CLIENT_API = GLFW_NO_API` - no GL context is created. |
| `Core/GlfwRuntime.h` | The **only** place that calls `glfwInit` / `glfwTerminate`. Reference counted. |
| `Core/KeyCodes.h` | `Nova::Key::Code`. Engine-owned numbering starting at 0. |
| `Core/InputGlfw.h` | `Nova::Key` -> GLFW translation. Internal; the only Core header that includes GLFW. |
| `Core/Input.h` | Per-frame snapshot plus edge detection (`IsKeyDown` / `IsKeyPressed` / `IsKeyReleased`). |
| `Core/Application.h` | Base class: owns the `Window`, runs the loop, calls the virtual `OnUpdate`. |

Three decisions worth knowing before reading the code:

**`glfwInit` is reference counted, not per-window.** It is process-global, so a
second window calling `glfwTerminate` in its destructor tears the library down
underneath the first - undefined behaviour that usually surfaces as a crash
inside GLFW, minutes later. `GlfwRuntime.h` is the single owner.

**`Nova::Key::Code` is not GLFW's numbering.** GLFW's `GLFW_KEY_SPACE` is 32;
reading it as Nova's 32 would bind the wrong key. The translation lives in
`InputGlfw.h`, with `static_assert`s that compare the mapping against GLFW's own
headers - so a mis-translated key is a build failure.

**`SetVSync` only records the request.** `glfwSwapInterval` acts on an OpenGL
context, and this engine's window has none. Under D3D12 the vertical blank
belongs to the DXGI swap chain, so the renderer reads `IsVSyncEnabled()` when it
creates one. Until then the value is inert.

### What the window looks like right now

White, not black. With no swap chain there is nothing to clear, so the client
area shows the Win32 window-class background, which is white. That is not a bug
in this layer and not something it should paper over with a black brush hack -
it is the renderer's job, and it goes away when `IDXGISwapChain` exists.

The main loop is also unpaced until then, so `Sandbox` reports six-figure FPS.
`ApplicationProperties::targetFramesPerSecond` caps it if a fixed rate is wanted.

## Current state

`Nova::Core` provides:

- `Log` - spdlog facade with lazy console fallback, file sink, and
  `NOVA_TRACE`..`NOVA_CRITICAL` macros that capture source location.
- `Platform` - CPU capability and topology detection (with correct XGETBV/OS
  support checks), memory, and a high-resolution monotonic timer.
- `Assert` - always-on assertion macros plus Win32 error formatting, in UTF-8.
- `Handle` / `HandleRegistry<Tag, T>` - generation-checked 64-bit entity
  handles over dense/sparse storage, with O(1) create, lookup and
  swap-remove destroy.
- `Window` / `GlfwRuntime` / `Input` / `Key` - a real window, polled keyboard
  and mouse, and `Nova::Key` identifiers with no GLFW dependency.
- `Application` - the base class both executables derive from: owns the window,
  runs the loop, computes delta time, and calls a virtual `OnUpdate`.

Run `Sandbox.exe`, then press **Escape** (or click the title-bar X). What it
proves: GLFW initialises, an HWND is created and shown, the loop runs, input
polling reaches the keyboard and mouse, and teardown is clean with exit code 0.
What it does not prove: anything about rendering - nothing is drawn.

## Next

1. `Math` - `Nova::Math` over DirectXMath, plus a unit test suite. Pure CPU
   code, no OS dependency, testable without a GPU.
2. `Renderer` - device, swap chain and command queue on the existing window.
   This is what turns the client area black instead of white, and what gives
   the loop something to pace itself against.
3. Precompiled headers for the modules (the log header pulls in spdlog).