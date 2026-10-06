// ===========================================================================
//  ConstantBuffer.h
// ---------------------------------------------------------------------------
//  A GPU-visible constant buffer: the bridge that makes a CPU struct readable
//  by a shader as a cbuffer.
// ===========================================================================
#pragma once

#include <Renderer/D3D12Helpers.h>

#include <cstdint>
#include <cstring>

namespace Nova::Renderer
{
/// One upload-heap region per frame in flight, so a frame's constants are never
/// overwritten by the frame the CPU is already building.
///
/// @note Non-copyable: the COM resource is owned, and a copy would be a second
///       owner of the same handle.
class ConstantBuffer final
{
public:
    /// D3D12's constant buffer alignment. 256, not 16.
    ///
    /// WHY 256 AND NOT sizeof(float4): the hardware reads constant data in
    /// 256-byte lines, and a shader-visible buffer whose start is not 256-byte
    /// aligned either reads garbage or is rejected by the debug layer with a
    /// message about "constant buffer offset alignment" that names neither the
    /// struct nor the register. The cost of the padding is at most 255 bytes
    /// per buffer; the cost of getting it wrong is a shader reading zeros.
    static constexpr UINT kAlignment = 256;

    ConstantBuffer() = default;

    ~ConstantBuffer()
    {
        // Upload heaps must be unmapped before release, or the driver cannot
        // reclaim the pages. RAII means an exception from Update cannot skip it.
        Unmap();
    }

    ConstantBuffer(const ConstantBuffer&)            = delete;
    ConstantBuffer& operator=(const ConstantBuffer&) = delete;
    ConstantBuffer(ConstantBuffer&&) noexcept        = default;
    ConstantBuffer& operator=(ConstantBuffer&&) noexcept = default;

    /// Allocates room for @p sizeInBytes on each of @p frameCount slots.
    ///
    /// @param device       Owning device.
    /// @param sizeInBytes  Payload size of ONE slot, before alignment padding.
    /// @param frameCount   Slots to allocate. Pass the renderer's frames-in-
    ///                      flight count so a frame in flight cannot overwrite
    ///                      the constants of a frame the GPU is still reading.
    void Create(ID3D12Device* device, std::uint32_t sizeInBytes, std::uint32_t frameCount);

    /// Copies @p data into the slot for @p frameIndex.
    ///
    /// @param data        Source bytes; copied, so the caller's struct may die.
    /// @param sizeInBytes Payload size. Must be <= the size given to Create.
    /// @param frameIndex  Frame slot, wrapped modulo the slot count.
    ///
    /// @pre Create has run.
    void Update(const void* data, std::uint32_t sizeInBytes, std::uint32_t frameIndex);

    /// Typed convenience: copies a whole struct and nothing else.
    template <typename T>
    void Update(const T& data, std::uint32_t frameIndex)
    {
        static_assert(std::is_trivially_copyable_v<T>,
                      "Constant buffers are raw bytes; a non-trivially-copyable "
                      "struct would upload pointers, not values");
        Update(&data, sizeof(T), frameIndex);
    }

    /// @return GPU address of one slot, for SetGraphicsRootConstantBuffer.
    ///
    /// @note The address is a byte offset into the mapped range. It is valid
    ///       until the buffer is destroyed; nothing here reallocates.
    [[nodiscard]] UINT64 GetGPUVirtualAddress(std::uint32_t frameIndex) const;

    /// @return Address of the first slot. The single-buffer case, kept so a
    ///         caller that never passes a frame index is still explicit.
    [[nodiscard]] UINT64 GetGPUVirtualAddress() const
    {
        return GetGPUVirtualAddress(0);
    }

    /// @return True once Create has allocated the resource.
    ///
    /// @note explicit, so a ConstantBuffer in an `if` is a deliberate test
    ///       rather than something an arithmetic expression can trigger.
    explicit operator bool() const noexcept { return buffer_ != nullptr; }

    /// @return Aligned payload size of one slot.
    [[nodiscard]] UINT GetSlotSize() const noexcept { return slotSize_; }

    [[nodiscard]] std::uint32_t GetFrameCount() const noexcept { return frameCount_; }

    /// Maps the whole allocation once and keeps it mapped.
    ///
    /// WHY MAP ONCE INSTEAD OF PER-UPDATE: Map/Unmap is a driver call, and on
    /// an upload heap it costs a page-table update per call. Updating a constant
    /// buffer every frame per draw means paying that hundreds of times a frame
    /// for a memcpy that is already the cheapest part. Keeping the mapping
    /// alive for the buffer's lifetime is the standard D3D12 pattern, and it is
    /// why this class is non-copyable and has an explicit lifetime.
    void Map();

    /// Releases the mapping. Idempotent.
    void Unmap() noexcept;

private:
    ComPtr<ID3D12Resource> buffer_;

    /// Mapped base pointer, or null when unmapped.
    UINT8* mappedPointer_ = nullptr;

    /// Aligned size of one slot in bytes.
    UINT slotSize_ = 0;

    /// Number of slots in the allocation.
    UINT frameCount_ = 0;
};

} // namespace Nova::Renderer