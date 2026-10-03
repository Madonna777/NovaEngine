# NovaEngine

A 3D game engine in C++20 on DirectX 12, for Windows 10/11 x64.

Work-in-progress. The Core layer builds and is exercised by the Sandbox
smoke test; the renderer has not started.

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
    |   |-- Core/            Logging, platform, assertions, handles  [IMPLEMENTED]
    |   |-- Math/            DirectXMath facade                     [NEXT]
    |   |-- Renderer/        D3D12 backend
    |   |-- Scene/           Scene graph, entity storage
    |   `-- Input/           Keyboard, mouse, gamepad
    |-- Editor/              Long-lived tool (placeholder)
    |-- Sandbox/             Disposable test app (Core smoke test)
    |-- Shaders/             HLSL + compilation manifest
    `-- ThirdParty/          Vendored deps - currently none

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

## Module dependency rules

`Core` depends on the standard library and vcpkg only. It must not depend on
`Math`, `Renderer`, `Scene` or `Input` - the Renderer will want to assert on GPU
state, so it will want `NOVA_ASSERT`, and a cycle there is a link error.

Build order is `Math` -> `Input` -> `Renderer` -> `Scene`. Scene comes after
Renderer deliberately: the scene graph allocates from GPU-visible heaps, so the
renderer owns memory and the scene describes what to put in it, not the other
way round.

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

Run `Sandbox.exe` to see all of it working and to confirm the toolchain.

## Next

1. `Math` - `Nova::Math` over DirectXMath, plus a unit test suite. Pure CPU
   code, no OS dependency, testable without a GPU.
2. Precompiled headers for the modules (the log header pulls in spdlog).
3. `Renderer` - device, swap chain and command queue via GLFW.