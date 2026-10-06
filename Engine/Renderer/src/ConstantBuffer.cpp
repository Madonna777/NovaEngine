// ===========================================================================
//  ConstantBuffer.cpp
// ===========================================================================

#include <Renderer/ConstantBuffer.h>

#include <Core/Log.h>

#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace Nova::Renderer
{
void ConstantBuffer::Create(ID3D12Device* device, std::uint32_t sizeInBytes, std::uint32_t frameCount)
{
    if (device == nullptr || sizeInBytes == 0 || frameCount == 0)
    {
        // A zero-sized or zero-slot buffer is never a request; it is a caller
        // whose struct was empty or whose frame count came from a bad config.
        // Failing here beats allocating a resource nobody can address.
        throw std::invalid_argument("ConstantBuffer::Create requires a device, a non-zero size "
                                    "and at least one frame slot");
    }

    // Round UP to the 256-byte boundary. Truncating instead would place frame 2
    // at an unaligned address and the shader would read misaligned garbage.
    slotSize_ = (static_cast<UINT>(sizeInBytes) + kAlignment - 1U) & ~(kAlignment - 1U);
    frameCount_ = frameCount;

    // ONE committed resource for all slots, not one per frame: a heap object per
    // frame is a per-frame allocation in a system that has no reason to
    // allocate per frame. Slots are simply stride-sized offsets into it, and
    // every offset is a multiple of 256 by construction above.
    const UINT64 totalBytes = static_cast<UINT64>(slotSize_) * frameCount_;

    // UPLOAD heap, GENERIC_READ: the CPU writes, the GPU reads, and no barrier
    // is needed between them because the driver keeps the write visible. This is
    // the whole reason per-frame constant updates do not stall the pipeline on
    // this engine.
    const D3D12_HEAP_PROPERTIES heapProperties{ D3D12_HEAP_TYPE_UPLOAD };

    D3D12_RESOURCE_DESC resourceDescription{};
    resourceDescription.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDescription.Width            = totalBytes;
    resourceDescription.Height           = 1;
    resourceDescription.DepthOrArraySize = 1;
    resourceDescription.MipLevels        = 1;
    resourceDescription.Format           = DXGI_FORMAT_UNKNOWN;
    resourceDescription.SampleDesc.Count = 1;
    resourceDescription.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDescription.Flags            = D3D12_RESOURCE_FLAG_NONE;

    NOVA_THROW_IF_FAILED(device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE,
                                                          &resourceDescription,
                                                          D3D12_RESOURCE_STATE_GENERIC_READ,
                                                          nullptr, IID_PPV_ARGS(&buffer_)));

    Map();

    NOVA_DEBUG("Constant buffer: {} bytes x {} slots, {} bytes/slot aligned",
               sizeInBytes, frameCount_, slotSize_);
}

void ConstantBuffer::Map()
{
    if (buffer_ == nullptr || mappedPointer_ != nullptr)
    {
        return;
    }

    // Empty read/write ranges: an upload heap has no read-back and no flush
    // semantics, so declaring a real range would be a lie the driver has to
    // honour. D3D12 explicitly treats empty ranges as "no data movement".
    void* pointer = nullptr;
    NOVA_THROW_IF_FAILED(buffer_->Map(0, nullptr, &pointer));
    mappedPointer_ = static_cast<UINT8*>(pointer);
}

void ConstantBuffer::Unmap() noexcept
{
    if (buffer_ == nullptr || mappedPointer_ == nullptr)
    {
        return;
    }

    buffer_->Unmap(0, nullptr);
    mappedPointer_ = nullptr;
}

void ConstantBuffer::Update(const void* data, std::uint32_t sizeInBytes, std::uint32_t frameIndex)
{
    if (mappedPointer_ == nullptr)
    {
        throw std::logic_error("ConstantBuffer::Update called before Create");
    }

    if (sizeInBytes > slotSize_)
    {
        // A struct that grew past its allocation is a caller bug, and silently
        // clamping would corrupt the NEXT frame slot rather than fail here.
        throw std::invalid_argument("ConstantBuffer::Update payload (" +
                                    std::to_string(sizeInBytes) + " bytes) exceeds the slot (" +
                                    std::to_string(slotSize_) + " bytes)");
    }

    const std::uint32_t slot = frameIndex % frameCount_;
    std::memcpy(mappedPointer_ + static_cast<std::size_t>(slot) * slotSize_, data, sizeInBytes);

    // Zero the padding tail of the slot. The shader may read a field the CPU
    // did not fill (a matrix row a caller built by hand, a struct member added
    // on the HLSL side only), and an upload heap does NOT zero its memory, so
    // that read returns whatever the previous frame at this slot left behind.
    if (sizeInBytes < slotSize_)
    {
        std::memset(mappedPointer_ + static_cast<std::size_t>(slot) * slotSize_ + sizeInBytes, 0,
                    slotSize_ - sizeInBytes);
    }
}

UINT64 ConstantBuffer::GetGPUVirtualAddress(std::uint32_t frameIndex) const
{
    if (buffer_ == nullptr)
    {
        return 0;
    }

    const std::uint32_t slot = frameIndex % frameCount_;
    return buffer_->GetGPUVirtualAddress() + static_cast<UINT64>(slot) * slotSize_;
}
} // namespace Nova::Renderer