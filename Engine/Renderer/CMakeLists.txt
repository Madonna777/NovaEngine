# ===========================================================================
#  Nova::Renderer
# ===========================================================================
#  The DirectX 12 backend: device, command submission, and swap chain.
#
#  SOURCES ARE LISTED EXPLICITLY, never file(GLOB ...), and each is added in the
#  same commit as the file it names. Both reasons are spelled out in
#  Core/CMakeLists.txt; the short version is that a GLOB does not re-run when a
#  file appears, and a source listed before it exists breaks CONFIGURE for every
#  commit in between.
nova_add_module(Renderer
    SOURCES
        src/D3D12Helpers.cpp
    HEADERS
        D3D12Helpers.h
    DEPENDS
        Nova::Core
        d3d12
        dxgi
)