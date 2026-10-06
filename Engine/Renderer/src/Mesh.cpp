// ===========================================================================
//  Mesh.cpp
// ===========================================================================

#include <Renderer/Mesh.h>

#include <Core/Log.h>

#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace Nova::Renderer
{
namespace
{
/// Creates an upload-heap buffer holding one copy of @p data.
///
/// WHY UPLOAD HEAP AND NOT DEFAULT FOR NOW: an upload heap is CPU-write,
/// GPU-read, and needs no barrier between the write and the read - the driver
/// keeps the write visible. That makes it correct for the first frame and
/// wrong for every frame after a change, which is why the alternative below is
/// written down rather than discovered later.
///
/// The upgrade path is a two-stage copy, not a different Create: keep the
/// immutable geometry in a DEFAULT heap buffer, keep a per-frame slot ring in
/// UPLOAD, and copy the staging ring into the default buffer once per frame.
/// The two buffers and the copy are the whole change; Create, Bind and Draw
/// above it do not move.
ComPtr<ID3D12Resource> CreateUploadBuffer(ID3D12Device* device, const void* data,
                                         UINT64 byteCount, const char* what)
{
    const D3D12_HEAP_PROPERTIES heap{ D3D12_HEAP_TYPE_UPLOAD };

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width            = byteCount;
    desc.Height           = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels        = 1;
    desc.Format           = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags            = D3D12_RESOURCE_FLAG_NONE;

    ComPtr<ID3D12Resource> buffer;
    // GENERIC_READ is the only legal initial state for upload heap. The name
    // reads backwards for a buffer the CPU writes, and it is a read from the
    // GPU's point of view - which is the only point of view that matters here.
    NOVA_THROW_IF_FAILED(device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&buffer)));

    void* mapped = nullptr;
    NOVA_THROW_IF_FAILED(buffer->Map(0, nullptr, &mapped));
    std::memcpy(mapped, data, static_cast<std::size_t>(byteCount));
    buffer->Unmap(0, nullptr);

    NOVA_DEBUG("Mesh {} buffer: {} bytes", what, byteCount);
    return buffer;
}
} // namespace

Vertex Vertex::Create(Math::Vec3 pos, Math::Vec3 nrm, Math::Vec2 texCoord)
{
    Vertex vertex;
    vertex.position = pos;
    vertex.normal   = nrm;
    vertex.uv       = texCoord;
    vertex.color    = Math::Vec4(1.0F, 1.0F, 1.0F, 1.0F);
    return vertex;
}

void Mesh::Create(ID3D12Device* device, const std::vector<Vertex>& vertices,
                  const std::vector<uint32_t>& indices)
{
    if (device == nullptr)
    {
        throw std::invalid_argument("Mesh::Create requires a device");
    }

    if (vertices.empty() || indices.empty())
    {
        // Checked here rather than by the caller because the callers are
        // exactly the two paths that produce empty results by design: a failed
        // file load and a primitive generator that was asked for zero
        // subdivisions. Both would otherwise reach a zero-sized upload.
        throw std::invalid_argument("Mesh::Create requires at least one vertex and one index");
    }

    if (indices.size() % 3U != 0U)
    {
        // D3D12 will not notice. It reads three indices at a time and a fourth
        // trailing index is an out-of-bounds read on the GPU.
        throw std::invalid_argument("Mesh::Create index count (" + std::to_string(indices.size()) +
                                    ") is not a multiple of 3");
    }

    const uint32_t largest = *std::max_element(indices.begin(), indices.end());
    if (largest >= vertices.size())
    {
        // This is the single most common malformed-mesh bug and it produces no
        // error at all at draw time: the input assembler reads past the vertex
        // buffer, and D3D12's own bounds checking either clamps or faults
        // depending on the driver. Named here because the message reaches the
        // log where the file that caused it does.
        throw std::invalid_argument("Mesh::Create index " + std::to_string(largest) +
                                    " is out of range for " + std::to_string(vertices.size()) +
                                    " vertices");
    }

    vertexCount_ = vertices.size();
    indexCount_  = indices.size();

    vertexBuffer_ = CreateUploadBuffer(device, vertices.data(),
                                       vertexCount_ * sizeof(Vertex), "vertex");
    indexBuffer_  = CreateUploadBuffer(device, indices.data(),
                                       indexCount_ * sizeof(uint32_t), "index");

    vertexBufferView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes    = static_cast<UINT>(vertexCount_ * sizeof(Vertex));
    vertexBufferView_.StrideInBytes  = static_cast<UINT>(sizeof(Vertex));

    // R32_UINT, never R16_UINT. A 16-bit index caps a mesh at 65536 vertices,
    // which is roughly a small room, and the failure mode is a model that
    // vanishes or tears once it crosses the threshold - with nothing in the
    // log. 32-bit indices cost one extra byte per index and remove the ceiling
    // entirely.
    //
    // The stride is sizeof(uint32_t), NOT a 256-byte alignment: index buffers
    // are streamed sequentially by the hardware and carry no constant-data
    // alignment requirement. Conflating the two is a common copy-paste error
    // and wastes a factor of 64 on the index buffer.
    indexBufferView_.BufferLocation = indexBuffer_->GetGPUVirtualAddress();
    indexBufferView_.SizeInBytes    = static_cast<UINT>(indexCount_ * sizeof(uint32_t));
    indexBufferView_.Format         = DXGI_FORMAT_R32_UINT;

    NOVA_INFO("Mesh created: {} vertices, {} indices, {} triangles", vertexCount_, indexCount_,
              indexCount_ / 3U);
}

void Mesh::Bind(ID3D12GraphicsCommandList* commandList) const noexcept
{
    if (commandList == nullptr || !*this)
    {
        return;
    }

    // Both views live in this Mesh, and this function is the only thing that
    // hands out a pointer to them, so their lifetime matches the binding.
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);

    // No offset argument on this SDK's IASetIndexBuffer - it takes the view
    // alone. The offset a caller might expect here lives in
    // DrawIndexedInstanced's startIndexLocation instead, which is the correct
    // home for it: an offset in the binding would have to be re-bound per draw
    // to sub-range a mesh, while startIndexLocation is per-draw state.
    commandList->IASetIndexBuffer(&indexBufferView_);
}

void Mesh::Draw(ID3D12GraphicsCommandList* commandList) const noexcept
{
    DrawIndexedInstanced(commandList, 1, 0);
}

void Mesh::DrawIndexedInstanced(ID3D12GraphicsCommandList* commandList, UINT instanceCount,
                                UINT startInstanceLocation) const noexcept
{
    if (commandList == nullptr || !*this || instanceCount == 0)
    {
        // Zero instances is a legitimate request in some callers and a bug in
        // others; skipping it is correct for both, and a draw with 0 instances
    // is legal but wasteful.
        return;
    }

    // DrawIndexedInstanced rather than DrawInstanced: the index buffer is what
    // lets several vertices share one position, which is the whole reason a
    // cube costs 8 vertices instead of 36.
    commandList->DrawIndexedInstanced(static_cast<UINT>(indexCount_), instanceCount,
                                      /* startIndexLocation   */ 0,
                                      /* baseVertexLocation   */ 0,
                                      startInstanceLocation);
}

} // namespace Nova::Renderer