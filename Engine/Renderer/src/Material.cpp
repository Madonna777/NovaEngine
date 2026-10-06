// ===========================================================================
//  Material.cpp
// ===========================================================================

#include <Renderer/Material.h>

#include <Core/Log.h>

#include <cstdint>

namespace Nova::Renderer
{
void Material::EnsureConstantBuffer(std::uint32_t slot, std::uint32_t sizeInBytes)
{
    RequireSlot(slot);

    if (constantBuffers_[slot])
    {
        return;
    }

    if (device_ == nullptr)
    {
        // Creating without a device is a wiring mistake: the material is not
        // attached to a renderer yet. Failing loudly beats allocating nothing
        // and binding a zero address.
        throw std::logic_error("Material::EnsureConstantBuffer needs SetDevice before the first "
                               "buffer");
    }

    constantBuffers_[slot].Create(device_, sizeInBytes, frameCount_);
}

UINT64 Material::GetConstantBufferAddress(std::uint32_t slot, std::uint32_t frameIndex) const
{
    if (slot >= kMaxConstantBufferSlots)
    {
        return 0;
    }
    return constantBuffers_[slot].GetGPUVirtualAddress(frameIndex);
}

void Material::Bind(ID3D12GraphicsCommandList* commandList, std::uint32_t frameIndex) const
{
    if (commandList == nullptr)
    {
        return;
    }

    // One loop over the array, in register order. The order does not matter to
    // the GPU - root parameters are independent - but iterating in order keeps
    // a truncated profile's call sequence reproducible.
    for (std::uint32_t slot = 0; slot < kMaxConstantBufferSlots; ++slot)
    {
        if (!constantBuffers_[slot])
        {
            continue;
        }

        const UINT64 address = constantBuffers_[slot].GetGPUVirtualAddress(frameIndex);
        if (address == 0)
        {
            continue;
        }

        // SetGraphicsRootConstantBufferView is a COMMAND LIST METHOD, and the
        // name matters: there is a same-shaped free function in the d3dx12
        // helper header, which this engine does not use. The method takes the
        // address by value and needs no descriptor heap, which is the whole
        // reason the root signature declares root descriptors.
        commandList->SetGraphicsRootConstantBufferView(slot, address);
    }
}

void Material::SetTexture(std::uint32_t slot, TextureBinding texture)
{
    RequireSlot(slot);
    textures_[slot] = std::move(texture);
}
} // namespace Nova::Renderer