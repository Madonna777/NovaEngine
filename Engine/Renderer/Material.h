// ===========================================================================
//  Material.h
// ---------------------------------------------------------------------------
//  A shader plus the resources bound to its registers. Material is what a draw
//  call needs and nothing else: which program, and where each register's data
//  comes from.
// ===========================================================================
#pragma once

#include <Renderer/ConstantBuffer.h>
#include <Renderer/D3D12Helpers.h>
#include <Renderer/Shader.h>

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Nova::Renderer
{
/// Slot count for constant buffers, matching the root signature's declared
/// parameter count. A fixed array rather than a map because the binding is by
/// register index: a hash lookup per draw call to resolve what is an array
/// index is exactly the kind of cost that shows up as a mysterious frame-time
/// regression on a scene with many objects.
inline constexpr std::size_t kMaxConstantBufferSlots = 8;

/// One texture slot. A placeholder with no view behind it yet.
///
/// @note Deliberately carries only the resource and its format. The descriptor
///       half - a shader resource view plus its slot in a descriptor heap - has
///       no consumer until the first textured material, and modelling it now
///       would mean guessing the heap's lifetime, its per-frame update rules and
///       its root signature entry, all three of which change when the texture
///       milestone decides them.
struct TextureBinding
{
    ComPtr<ID3D12Resource> resource;
    DXGI_FORMAT            format = DXGI_FORMAT_UNKNOWN;
};

/// Binds a shader and its constant buffers to the command list.
class Material final
{
public:
    Material()  = default;
    ~Material() = default;

    Material(const Material&)            = delete;
    Material& operator=(const Material&) = delete;
    Material(Material&&) noexcept        = default;
    Material& operator=(Material&&) noexcept = default;

    /// Adopts a compiled shader.
    ///
    /// @param shader Compiled pair. Moved in, because the bytecode blobs are
    ///               large and the material never needs the source any more.
    void SetShader(Shader shader) { shader_ = std::move(shader); }

    [[nodiscard]] const Shader& GetShader() const noexcept { return shader_; }
    [[nodiscard]] bool HasShader() const noexcept { return !shader_.GetVertexShaderBytecode().pShaderBytecode; }

    /// Uploads @p data into the buffer bound to @p slot.
    ///
    /// @param slot       Root parameter / register index (b0, b1, ...).
    /// @param data       Payload; copied.
    /// @param frameIndex Frame slot, so the write cannot race the GPU.
    ///
    /// @throws std::invalid_argument if the slot exceeds kMaxConstantBufferSlots,
    ///         or the buffer has not been created.
    template <typename T>
    void SetConstantBuffer(std::uint32_t slot, const T& data, std::uint32_t frameIndex)
    {
        RequireSlot(slot);
        if (!constantBuffers_[slot])
        {
            // Sized from T rather than reported by the shader: the buffer must
            // exist before the first update, and a caller who knows the struct
            // is the only one who can know its size.
            constantBuffers_[slot].Create(device_, sizeof(T), frameCount_);
        }
        constantBuffers_[slot].Update(data, frameIndex);
    }

    /// Creates the bound buffer if it does not exist yet, sized @p sizeInBytes.
    ///
    /// Separate from SetConstantBuffer because the camera buffer is created once
    /// at startup with a known size and then updated every frame; letting the
    /// first Update allocate would tie the buffer's size to whichever struct type
    /// happened to be passed first.
    void EnsureConstantBuffer(std::uint32_t slot, std::uint32_t sizeInBytes);

    /// @return GPU address for @p slot and @p frameIndex, or 0 when unbound.
    [[nodiscard]] UINT64 GetConstantBufferAddress(std::uint32_t slot, std::uint32_t frameIndex) const;

    /// Writes every bound constant buffer into the root parameters.
    ///
    /// @param commandList     List being recorded.
    /// @param frameIndex      Frame slot whose data should be bound.
    ///
    /// @note Binds by ROOT CONSTANT BUFFER, not by descriptor table. That is
    ///       legal only because these are CBV root parameters: a descriptor
    ///       table would need a descriptor heap and a table slot per buffer,
    ///       which is the right shape for dozens of bindless resources and the
    ///       wrong one for three fixed ones.
    void Bind(ID3D12GraphicsCommandList* commandList, std::uint32_t frameIndex) const;

    /// Sets the device used when a bound buffer is created on demand.
    void SetDevice(ID3D12Device* device) noexcept { device_ = device; }

    /// Number of frame slots newly created buffers get.
    void SetFrameCount(std::uint32_t frameCount) noexcept { frameCount_ = frameCount; }

    /// Binds a texture into @p slot. Stored but not yet bound to a shader
    /// register: the SRV/descriptor-pool plumbing lands with the texture
    /// milestone, and a stub that silently binds nothing would hide that.
    void SetTexture(std::uint32_t slot, TextureBinding texture);

    [[nodiscard]] std::string_view GetName() const noexcept { return name_; }
    void SetName(std::string_view name) { name_ = std::string{ name }; }

private:
    void RequireSlot(std::uint32_t slot) const
    {
        if (slot >= kMaxConstantBufferSlots)
        {
            throw std::out_of_range("Material constant buffer slot " + std::to_string(slot) +
                                    " exceeds kMaxConstantBufferSlots");
        }
    }

    Shader shader_;
    std::string name_;

    /// Index == register index. A null entry is an unbound register, and Bind
    /// skips it rather than binding a zero address - the debug layer rejects a
    /// null CBV address only for descriptors, but the shader would read zero.
    std::array<ConstantBuffer, kMaxConstantBufferSlots> constantBuffers_;

    /// Same indexing rule as constant buffers_; reserved for the texture
    /// milestone so the shape of Material is already correct.
    std::array<TextureBinding, kMaxConstantBufferSlots> textures_;

    ID3D12Device* device_ = nullptr;
    std::uint32_t frameCount_ = 2;
};

} // namespace Nova::Renderer