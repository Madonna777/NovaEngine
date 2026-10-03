// ===========================================================================
//  HandleRegistry.h
// ---------------------------------------------------------------------------
//  Dense/sparse storage with O(1) handle creation, lookup, and destruction.
//
//  THE LAYOUT, AND WHY IT IS NOT A std::vector<T>
//  ------------------------------------------------
//  Two parallel arrays:
//
//      m_Dense    [alive slots only, contiguous, cache-friendly]
//      m_Sparse   [handle index -> (generation, position in m_Dense)]
//
//  WHY DENSE: iteration over 100k entities must touch memory linearly. If
//  dead slots left holes, the renderer would stream through the entire
//  high-water mark - including entities deleted ten minutes ago - paying full
//  memory bandwidth for pure padding. Because m_Dense contains only live data,
//  a sequential sweep is fully utilised.
//
//  WHY SPARSE: callers hold handles, not array positions, so a handle must
//  resolve to a dense position in one hop. m_Sparse grows with the handle space
//  and is index-addressed, so lookup is a single load from a predictable offset.
//
//  WHY DESTRUCTION IS A SWAP-REMOVE: erasing from the middle of a vector costs
//  O(n) and would invalidate every handle after the hole. Instead the last live
//  element is moved into the vacated position in O(1); only the moved element's
//  own sparse entry needs patching. No handle ever changes, because a handle is
//  keyed on its stable index, not on its position in the array.
//
//  WHY THE GENERATION LIVES IN THE SPARSE ENTRY: the dense slot is erased on
//  destroy, so the counter that must outlive it cannot be stored there. Keeping
//  it in m_Sparse is what lets the slot be reused immediately while every handle
//  previously issued for it stays permanently invalid.
// ===========================================================================
#pragma once

#include <Core/Assert.h>
#include <Core/Handle.h>

#include <cstdint>
#include <utility>
#include <vector>

namespace Nova
{
/// Stores instances of @p T and hands out generation-checked handles to them.
///
/// @tparam Tag Handle tag, giving the registry's handles a distinct type.
/// @tparam T   Stored type. Must be default-constructible and assignable.
///             The vector-backed slots require it; a later revision can swap in
///             a raw allocator plus optional storage to lift that restriction.
///
/// @note Not thread-safe. Guard with your own mutex if worker threads touch it.
template<typename Tag, typename T>
class HandleRegistry final
{
public:
    using HandleType = Handle<Tag>;

    /// One live instance. Stored contiguously in m_Dense.
    struct Slot
    {
        T                  value{};
        typename HandleType::IndexType handleIndex = Detail::kInvalidHandleIndex;
    };

    HandleRegistry() = default;

    /// Pre-sizes the arrays so a batch of subsequent Create calls performs no
    /// reallocation.
    ///
    /// @note Worth doing once at load time. std::vector growth is amortised
    ///       O(1) but still copies every element - and moving 100k components
    ///       mid-frame is a visible hitch. One Reserve turns that into a single
    ///       allocation.
    void Reserve(std::size_t capacity)
    {
        m_Dense.reserve(capacity);
        m_FreeIndices.reserve(capacity / 4);
    }

    /// Creates a default-constructed instance.
    [[nodiscard]] HandleType Create()
    {
        const auto index = AcquireIndex();
        const auto position = m_Dense.size();

        m_Dense.emplace_back();
        m_Dense.back().value     = T{};
        m_Dense.back().handleIndex = index;

        m_Sparse[index].denseIndex = static_cast<HandleType::IndexType>(position);
        return HandleType::FromParts(index, m_Sparse[index].generation);
    }

    /// Creates an instance move-constructed from @p args.
    template<typename... Args>
    [[nodiscard]] HandleType Emplace(Args&&... args)
    {
        const auto index    = AcquireIndex();
        const auto position = m_Dense.size();

        m_Dense.emplace_back();
        m_Dense.back().value      = T{std::forward<Args>(args)...};
        m_Dense.back().handleIndex = index;

        m_Sparse[index].denseIndex = static_cast<HandleType::IndexType>(position);
        return HandleType::FromParts(index, m_Sparse[index].generation);
    }

    /// Destroys the entity @p handle refers to, invalidating all references.
    ///
    /// @return False if the handle was already invalid or is stale - which makes
    ///         double-destroy safe, an important property when both gameplay and
    ///         a deferred death timer can remove the same entity.
    bool Destroy(HandleType handle) noexcept
    {
        const auto index = handle.GetIndex();
        if (index >= m_Sparse.size())
        {
            return false;
        }

        SparseEntry& sparse = m_Sparse[index];
        if (sparse.denseIndex == Detail::kInvalidHandleIndex ||
            sparse.generation != handle.GetGeneration())
        {
            return false; // Stale handle from a previous life of this slot.
        }

        const auto position = sparse.denseIndex;
        const auto last     = m_Dense.size() - 1;

        // Swap-remove: move the last live element into the hole.
        if (position != last)
        {
            m_Dense[position]              = std::move(m_Dense[last]);
            m_Sparse[m_Dense[position].handleIndex].denseIndex = position;
        }
        m_Dense.pop_back();

        // Retire the slot. Incrementing the generation is what makes every
        // previously-issued handle for this index fail validation forever.
        // Zero is skipped so that "generation 0" can never match a live entity,
        // and because the null handle is all-zero - a handle to index 0 with
        // generation 0 would otherwise be indistinguishable from it.
        sparse.generation = (sparse.generation == std::numeric_limits<std::uint32_t>::max())
                                ? 1u
                                : sparse.generation + 1u;
        sparse.denseIndex = Detail::kInvalidHandleIndex;

        m_FreeIndices.push_back(index);
        return true;
    }

    /// @return True if @p handle refers to a live instance in THIS registry.
    [[nodiscard]] bool IsAlive(HandleType handle) const noexcept
    {
        const auto index = handle.GetIndex();
        return index < m_Sparse.size() &&
               m_Sparse[index].denseIndex != Detail::kInvalidHandleIndex &&
               m_Sparse[index].generation == handle.GetGeneration();
    }

    /// @return Pointer to the instance, or nullptr if the handle is stale.
    [[nodiscard]] T* TryGet(HandleType handle) noexcept
    {
        const auto index = handle.GetIndex();
        if (index >= m_Sparse.size())
        {
            return nullptr;
        }
        const SparseEntry& sparse = m_Sparse[index];
        if (sparse.denseIndex == Detail::kInvalidHandleIndex ||
            sparse.generation != handle.GetGeneration())
        {
            return nullptr;
        }
        return &m_Dense[sparse.denseIndex].value;
    }

    /// @overload
    [[nodiscard]] const T* TryGet(HandleType handle) const noexcept
    {
        return const_cast<HandleRegistry*>(this)->TryGet(handle);
    }

    /// @return Reference to the instance. Asserts if @p handle is stale.
    ///
    /// @note Dereferences a stale handle is UB, hence the assert. Prefer
    ///       TryGet at any point where staleness is genuinely possible - such
    ///       as anything crossing a job boundary or a script callback.
    [[nodiscard]] T& Get(HandleType handle) noexcept
    {
        T* value = TryGet(handle);
        NOVA_ASSERT_MSG(value != nullptr,
                        "dereferenced a stale handle - use TryGet where staleness is possible");
        return *value;
    }

    /// @overload
    [[nodiscard]] const T& Get(HandleType handle) const noexcept
    {
        return const_cast<HandleRegistry*>(this)->Get(handle);
    }

    /// Invokes @p function(handle, value&) for every live instance, in storage
    /// order.
    ///
    /// @note The value reference is MUTABLE, because the dominant use is
    ///       updating components. A caller wanting read-only access simply
    ///       declares its parameter as const T& - it binds to a mutable
    ///       reference at no cost, so one overload covers both.
    ///
    /// @note Do not create or destroy instances inside the callback: a
    ///       destroy performs a swap-remove that invalidates the iteration
    ///       position. Queue removals and apply them after the walk. Mutating
    ///       the VALUE is safe.
    template<typename Function>
    void ForEach(Function&& function)
    {
        for (std::size_t position = 0; position < m_Dense.size(); ++position)
        {
            Slot& slot = m_Dense[position];
            function(HandleType::FromParts(slot.handleIndex, m_Sparse[slot.handleIndex].generation),
                     slot.value);
        }
    }

    /// @return Number of live instances.
    [[nodiscard]] std::size_t AliveCount() const noexcept
    {
        return m_Dense.size();
    }

    /// @return Total handle slots ever issued, live or recycled.
    [[nodiscard]] std::size_t Capacity() const noexcept
    {
        return m_Sparse.size();
    }

    /// Destroys every instance and releases all memory.
    ///
    /// @note Increments nothing: every outstanding handle becomes invalid
    ///       because the sparse array is cleared, which is the desired
    ///       behaviour when tearing down a scene.
    void Clear() noexcept
    {
        m_Dense.clear();
        m_Sparse.clear();
        m_FreeIndices.clear();
    }

private:
    /// Persistent per-handle-index state. Survives slot reuse, unlike Slot.
    struct SparseEntry
    {
        std::uint32_t          generation = 0;
        typename HandleType::IndexType denseIndex = Detail::kInvalidHandleIndex;
    };

    /// @return A handle index, reusing a retired slot when one is available.
    ///
    /// @note Reuse comes first, before minting a new index. Always reusing a
    ///       free slot keeps m_Sparse small; issuing fresh indices
    ///       unconditionally would grow the sparse array without bound over a
    ///       long session full of spawn/despawn cycles.
    [[nodiscard]] typename HandleType::IndexType AcquireIndex()
    {
        if (!m_FreeIndices.empty())
        {
            const auto index = m_FreeIndices.back();
            m_FreeIndices.pop_back();
            NOVA_ASSERT_MSG(m_Sparse[index].denseIndex == Detail::kInvalidHandleIndex,
                            "recycled handle slot was still marked live");
            return index;
        }

        const auto index = m_NextIndex;
        NOVA_ASSERT_MSG(index != Detail::kInvalidHandleIndex,
                        "handle index space exhausted (4.29 billion entities)");

        ++m_NextIndex;
        m_Sparse.resize(static_cast<std::size_t>(index) + 1);
        m_Sparse[index].generation = 1; // Live handles always carry a non-zero generation.
        return index;
    }

    std::vector<Slot>                        m_Dense;       // Live slots only
    std::vector<SparseEntry>                 m_Sparse;      // Handle index -> dense position
    std::vector<typename HandleType::IndexType> m_FreeIndices; // Retired indices, reused LIFO
    typename HandleType::IndexType           m_NextIndex = 0;
};

} // namespace Nova