# ===========================================================================
#  Nova::Renderer
# ===========================================================================
#  The DirectX 12 backend: device, command submission, and swap chain.
#
#  DEPENDENCY RULE (the mirror of Core's): Renderer may depend on Core, Math and
#  Windows system libraries. It must NOT be depended upon by Core.
#
#  WHY THAT RULE IS THE ONE THAT IS ACTUALLY ENFORCED TODAY
#  ---------------------------------------------------------
#  Core owns Application, which owns the main loop, and the natural next step
#  for a renderer bring-up is to have Application own the device too: create it
#  after the window, call BeginFrame/Clear/EndFrame from inside the loop, destroy
#  it before the window. Every one of those steps is correct in isolation.
#
#  Together they make Core depend on Renderer. And the cycle is not hypothetical:
#  the renderer's first real feature - an object-space or screen-space pass that
#  needs the frame delta - wants Application, or at minimum wants the same
#  assertion and logging helpers Core owns. The cycle can then only be broken
#  with forward declarations and an ugly header, or by moving Application up a
#  level that does not exist yet.
#
#  So the loop drives the renderer through VIRTUAL HOOKS instead
#  (Application::OnRenderFrame), the concrete Application subclass in Sandbox
#  owns the D3D12Context, and the dependency runs one way. The observable
#  behaviour is identical - BeginFrame, clear, EndFrame, in that order, every
#  frame, and the context destroyed before the window - and the only thing that
#  changed is which side of the boundary the renderer sits on.
#
#  SOURCES ARE LISTED EXPLICITLY, never file(GLOB ...), and each is added in the
#  same commit as the file it names. Both reasons are spelled out in
#  Core/CMakeLists.txt; the short version is that a GLOB does not re-run when a
#  file appears, and a source listed before it exists breaks CONFIGURE for every
#  commit in between.
#
#  WHY FIVE TRANSLATION UNITS FOR ONE CLASS
#  ----------------------------------------
#  D3D12Context is one class and five .cpp files, and that is deliberate rather
#  than a staging error. Each file is one concern that changes for its own
#  reasons and would otherwise be buried in a 700-line translation unit:
#
#      D3D12Helpers.cpp        HRESULT throwing - the only thing shared
#      D3D12Context.cpp        frame lifecycle - the hot path
#      D3D12Device.cpp         factory, adapter policy, device, capabilities
#      D3D12CommandQueue.cpp   queue, fence, allocators, command list
#      D3D12SwapChain.cpp      swap chain, render target views, resize
#      Pipeline.cpp            shader compile + root signature + PSO
#      VertexBuffer.cpp        upload-heap vertex buffer + its IA view
#
#  Adapter policy changes when a laptop ships with a new GPU. Resize changes
#  when multi-monitor support lands. Pipeline construction changes whenever an
#  engine effect ships. Merging them would mean reading all of
#  that to find any one of it.
# ===========================================================================

nova_add_module(Renderer
    SOURCES
        src/D3D12CommandQueue.cpp
        src/D3D12Context.cpp
        src/D3D12Device.cpp
        src/D3D12Helpers.cpp
        src/D3D12SwapChain.cpp
        src/Pipeline.cpp
        src/VertexBuffer.cpp
    HEADERS
        D3D12Context.h
        D3D12Helpers.h
        Pipeline.h
        VertexBuffer.h
    DEPENDS
        Nova::Core
        d3d12
        dxgi
        d3dcompiler
)

# ---------------------------------------------------------------------------
#  D3D12 debug layer
# ---------------------------------------------------------------------------
# WHY A SEPARATE DEFINE AND NOT #ifndef NDEBUG: the debug layer is not free.
# It adds a validation pass to every API call and, on some drivers, a
# synchronisation point, which makes Debug-build frame times useless for
# measuring anything. A developer bisecting a synchronisation bug wants it on in
# an optimised build; a developer reading a profiler wants it off in Debug. Both
# are legitimate, so the flag is independent of the optimisation level.
#
# WHY IT IS 0 OR 1 AND NOT SIMPLY DEFINED OR UNDEFINED: the code tests it with
# #if, and an undefined macro in #if is 0 - so a build that somehow missed this
# definition would silently run unvalidated and report nothing. Always defining
# it means the value is always the one decided here. See the fallback in
# D3D12Helpers.h for the same reason on the source side.
target_compile_definitions(Renderer PRIVATE
    $<$<CONFIG:Debug>:NOVA_D3D12_DEBUG_LAYER=1>
    $<$<NOT:$<CONFIG:Debug>>:NOVA_D3D12_DEBUG_LAYER=0>
)