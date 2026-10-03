# ThirdParty

Vendored dependencies that are **not** managed by vcpkg.

## Policy

**Default: everything goes through vcpkg.** Managed packages stay out of git,
get consistent build flags and CRT linkage from the toolchain file, and are
updated by bumping `builtin-baseline` in `vcpkg.json` - one line, reviewable in
a diff.

A vendored library belongs here only when one of these is true:

1. vcpkg has no port, and writing/maintaining one is genuinely worse than
   carrying the source (a handful of files, no build system to port).
2. The library requires build flags vcpkg cannot express.
3. The library must be modified, and the patch is small enough to maintain.

Anything vendored here must state *why* above, and carry its upstream commit or
version in a header comment so it can be re-based later.

## Current contents

None. Everything is currently vcpkg-managed.

## Dependencies and where they come from

| Dependency | Source | Used by | Note |
|---|---|---|---|
| spdlog | vcpkg | `Nova::Core` | Only external dependency at present. |
| glfw3 | vcpkg | `Renderer`, `Input` | Window + input. Wrapped, never included directly outside the wrapper. |
| stb | vcpkg | `Renderer`, `Editor` | Image decode. Single-header, public domain. |
| tracy | vcpkg | all modules | Profiling. Optional - see below. |
| imgui | vcpkg | `Editor` | UI. Needs a D3D12 backend bound to our device. |
| assimp | vcpkg | `Editor` | Model import. CPU-only; belongs in Editor, not the shipping runtime. |
| directxmath | vcpkg | `Math`, `Renderer` | SIMD math. See the note below. |
| gtest | vcpkg | tests | Behind the `tests` vcpkg feature. |

## Two notes that are not obvious

**DirectX 12 is not a vcpkg package.** `d3d12.lib`, `dxgi.lib`, `dxguid.lib`
and `d3dcompiler_47.dll` come from the Windows SDK (10.0.26100.0 here). vcpkg's
role is only the libraries in the table above. This is a common dead end - there
is no `d3d12` port to look for.

**glm is deliberately absent.** `Nova::Math` will wrap DirectXMath instead. glm
is a header-only C++ SIMD library whose types do not carry the 16-byte alignment
D3D12 constant buffers require; retrofitting `alignas(16)` across every
signature once components depend on it is a large, expensive change. DirectXMath
is intrinsics-backed, already required by the platform headers, matches the HLSL
side of the engine, and is what the D3D12 reference samples use.