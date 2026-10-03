# Shaders

HLSL sources and the compilation pipeline that turns them into bytecode the
D3D12 pipeline state can consume. Populated with the Renderer milestone.

## Planned layout

    Shaders/
    |-- include/          Shared .hlsl headers (common types, lighting, noise)
    |-- shaders/          Entry points compiled per entrypoint
    `-- shaders.json      Manifest: entrypoint -> permutations -> profile

## Why a manifest instead of a bare glob

A shader library has *permutations* - the same source compiled several ways
(lighting models, shadow modes, vertex morph targets, instancing variants).
Globbing the directory cannot express a permutation set, and the usual outcome
is a build that silently compiles one variant and leaves the #define branches
in place, so the shader carries runtime cost for features nobody enabled.

`shaders.json` will declare each entrypoint together with the permutations it
supports, letting the build:

- compile only variants actually referenced by the renderer,
- pass a stable permutation hash to the runtime so a PSO cache can be keyed on
  it and stale cache entries are detected instead of misapplied,
- emit a compile-error report that names the permutation which failed.

## Compilation toolchain (decided, not yet wired up)

- `dxc` (DirectXShaderCompiler) for Shader Model 6.x, producing DXIL.
- Optional `fxc` for SM 5.1 bytecode only if a legacy path is ever needed. Not
  expected - SM 6.x is the target and D3D12 supports DXIL natively.

Both are consumed as custom CMake targets so shader compilation is part of the
normal build graph and shows up as a compilation step, not a manual "did you
remember to rebuild the shaders" chore.

## GPU concepts to remember when these land

- **DXIL is not a binary blob to load.** `ID3DBlob` from `DxcBuffer` is the
  input to pipeline state creation; there is no separate byte array to keep in
  sync.
- **Permutations are part of the PSO.** Two different permutations are different
  pipeline state objects, even from identical source. They cannot share a cache
  entry unless the permutation hash matches.
- **A shader library (`[numthreads]`) is not a pipeline.** Compute entry points
  using Shader Model 6.6+ libraries are built with
  `ID3D12PipelineLibrary`, which requires D3D12's pipeline library support and a
  different creation path than `D3D12CreatePipeline` with a single DXC blob.
- **DXC must run at runtime**, not just build time, when shader libraries or
  `IDxcUtils::CreateReflection` are involved. The redistributable `dxcompiler.dll`
  ships alongside the executable.