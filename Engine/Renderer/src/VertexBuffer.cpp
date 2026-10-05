// ===========================================================================
//  VertexBuffer.cpp
// ===========================================================================

#include <Renderer/VertexBuffer.h>

#include <Core/Log.h>

#include <cstring>
#include <stdexcept>

namespace Nova::Renderer
{
D3D12VertexBuffer::D3D12VertexBuffer(ID3D12Device* device, const std::vector<Vertex>& vertices)
    : vertexCount_(static_cast<std::uint32_t>(vertices.size()))
{
    if (vertices.empty())
    {
        // A zero-sized upload is not a useful buffer and is an error to create.
        // Throwing here catches the upstream bug - usually a forgotten vector -
        // before it becomes a corrupted draw call three frames later.
        throw std::invalid_argument("D3D12VertexBuffer requires at least one vertex");
    }

    const D3D12_HEAP_PROPERTIES heapProperties{ D3D12_HEAP_TYPE_UPLOAD };

    D3D12_RESOURCE_DESC resourceDescription{};
    resourceDescription.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDescription.Alignment        = 0;
    // D3D12 constants buffers must be 256-byte aligned; vertex buffers are not,
    // but the size itself is required to be the actual byte count only.
    resourceDescription.Width            = static_cast<UINT64>(vertices.size() * sizeof(Vertex));
    resourceDescription.Height           = 1;
    resourceDescription.DepthOrArraySize = 1;
    resourceDescription.MipLevels        = 1;
    resourceDescription.Format           = DXGI_FORMAT_UNKNOWN;
    resourceDescription.SampleDesc.Count = 1;
    resourceDescription.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDescription.Flags            = D3D12_RESOURCE_FLAG_NONE;

    // GENERIC_READ is the only legal initial state for upload heap; the CPU
    // writes into it before the GPU ever reads from it.
    NOVA_THROW_IF_FAILED(
        device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDescription,
                                        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                        IID_PPV_ARGS(&buffer_)));

    // ---- upload -----------------------------------------------------------
    //
    // No D3D12_MAP_FLAG... the Map/Unmap pair for upload heap uses an empty
    // CPU-read range to tell D3D12 not to worry about what the CPU reads back -
    // and uploads the whole payload in one call.
    D3D12_RANGE readRange{};
    void*        mapped = nullptr;
    NOVA_THROW_IF_FAILED(buffer_->Map(0, &readRange, &mapped));

    std::memcpy(mapped, vertices.data(), vertices.size() * sizeof(Vertex));

    // Unmap reports what range the CPU touched, so D3D12 can flush the right
    // bytes. The buffer's whole width is treated as dirty, because Map on a
    // freshly created resource has no read-back dependency.
    const D3D12_RANGE writtenRange{ 0, static_cast<SIZE_T>(vertices.size() * sizeof(Vertex)) };
    buffer_->Unmap(0, &writtenRange);

    NOVA_INFO("Vertex buffer created: {} vertices, {} bytes", vertexCount_,
              static_cast<std::uint64_t>(vertices.size() * sizeof(Vertex)));
}

D3D12_VERTEX_BUFFER_VIEW D3D12VertexBuffer::GetVertexBufferView() const noexcept
{
    D3D12_VERTEX_BUFFER_VIEW view{};
    view.BufferLocation = buffer_ ? buffer_->GetGPUVirtualAddress() : 0;
    view.SizeInBytes    = vertexCount_ * sizeof(Vertex);
    view.StrideInBytes  = sizeof(Vertex);
    return view;
}

} // namespace Nova::Renderer