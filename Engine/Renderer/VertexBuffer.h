// ===========================================================================
//  VertexBuffer.h
// ---------------------------------------------------------------------------
//  Vertex data owned in GPU-visible (upload-heap) memory and the buffer view the
//  input assembler needs to read it.
// ===========================================================================
#pragma once

#include <Renderer/D3D12Helpers.h>

#include <cstdint>
#include <vector>

namespace Nova::Renderer
{
/// One attribute row for the input assembler.
///
/// WHY PLAIN FLOATS AND NOT A NAMED TYPE PER ATTRIBUTE: the pipe that ties this
/// struct to the vertex shader is the D3D12_INPUT_ELEMENT_DESC layout, which
/// reads by offset. A struct member of a class or a float4 type would both work,
/// but a named abstraction here is two types that have to stay in sync - the
/// VERTEX shader does not know where the engine put things, only the IA layout
/// object does.
struct Vertex
{
    /// Object->clip position so far supplied directly in clip space: [x, y, z].
    float position[3];

    /// RGBA colour, forwarded through the pixel shader to the back buffer.
    float color[4];
};

/// Owns an upload-heap vertex buffer and its IA buffer view.
///
/// @note Upload heap is the correct type for vertex data that never changes:
///       it is GPU-writable-once / CPU-written, which is exactly how an
///       authoring-time strip of create-and-leave-alone vertices is consumed.
///       A usage/readback pair with an upload->subresource copy would be
///       required only if that buffer were re-uploaded every frame.
class D3D12VertexBuffer final
{
public:
    D3D12VertexBuffer() = default;

    /// Uploads @p vertices into an upload-heap committed resource.
    ///
    /// @param device    Device to own the resource.
    /// @param vertices  The data. Copied, so the caller's vector may be freed.
    D3D12VertexBuffer(ID3D12Device* device, const std::vector<Vertex>& vertices);

    D3D12VertexBuffer(const D3D12VertexBuffer&)            = delete;
    D3D12VertexBuffer& operator=(const D3D12VertexBuffer&) = delete;
    D3D12VertexBuffer(D3D12VertexBuffer&&)                 = default;
    D3D12VertexBuffer& operator=(D3D12VertexBuffer&&)      = default;

    /// @return The view the input assembler reads from with IASetVertexBuffers.
    ///
    /// @pre The buffer was created. A default-constructed D3D12VertexBuffer has
    ///      no backing resource, and the view is invalid.
    [[nodiscard]] D3D12_VERTEX_BUFFER_VIEW GetVertexBufferView() const noexcept;

    /// @return Number of vertices uploaded.
    [[nodiscard]] std::uint32_t GetVertexCount() const noexcept { return vertexCount_; }

private:
    ComPtr<ID3D12Resource> buffer_;
    std::uint32_t          vertexCount_ = 0;
};

} // namespace Nova::Renderer
