// ===========================================================================
//  Handle.h
// ---------------------------------------------------------------------------
//  Generation-checked, strongly-typed entity references.
//
//  THE PROBLEM THIS SOLVES
//  -----------------------
//  A raw pointer or index into an entity array has no way to report that the
//  thing it pointed at is gone. In a game loop this is the classic
//  use-after-free:
//
//      Entity e = CreateEnemy();
//      RemoveEnemy(e);                    // slot recycled next frame
//      DrawHealthBar(e);                  // e still looks valid
//
//  Worse, the failure is silent and intermittent: if nothing else grabbed the
//  recycled slot, the code "works" in testing and corrupts memory in front of
//  players. Entity indices also let you accidentally pass a Transform handle
//  where a Mesh handle is expected, with no diagnostic.
//
//  THE SOLUTION
//  -------------
//  1. GENERATION COUNTERS. A handle carries the generation of the slot it was
//     created from. Destroying an entity increments the slot's generation, so
//     every stale handle to it fails validation immediately. The slot itself is
//     recycled for a new entity - keeping memory dense - but old handles can
//     never be confused with new ones.
//
//  2. TYPE SAFETY BY TEMPLATE. Handle<Tag> for distinct tags are distinct
//     types. A Handle<Mesh> will not implicitly convert to a Handle<Transform>.
//
//  WHY A PLAIN STRUCT AND NOT std::variant, OR A CLASS WITH A VTABLE
//  ---------------------------------------------------------------
//  Handles get stored by the million in SoA arrays and copied per component.
//  This type is 8 bytes, trivially copyable, standard-layout, and
//  byte-comparable - which means arrays of handles memcmp-compact, and the
//  renderer can serialise a handle list with a single memcpy. A vtable would
//  make it 16+ bytes and non-trivially-copyable for no benefit.
//
//  WHY 32 BITS OF GENERATION IS ENOUGH: generation increments once per
//  destroy on a given slot. To alias a live handle you must destroy that exact
//  slot 2^32 times, so wrapping is not a practical concern - but the counter is
//  never allowed to reach zero, which is what guarantees a default-constructed
//  handle always fails validation.
// ===========================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <string_view>

namespace Nova
{
namespace Detail
{
/// Value stored in the index field of a default-constructed handle.
///
/// @note Chosen as all-ones so that 0x00000000FFFFFFFF, the lowest value a
///       valid handle can take, is never mistaken for the null handle.
inline constexpr std::uint32_t kInvalidHandleIndex = 0xFFFFFFFFu;

/// Index bits, packed into the low half of a 64-bit handle.
inline constexpr std::uint32_t kHandleIndexBits = 32;
} // namespace Detail

/// A 64-bit, generation-checked reference to a slot in a HandleRegistry.
///
/// @tparam Tag Distinct type per entity category, making handles
///             non-interchangeable at compile time.
///
/// @note Default-constructible and never valid, so a member handle is safe to
///       leave uninitialised in a `default`/`reset` aggregate.
template<typename Tag>
class Handle final
{
public:
    /// Raw storage type. Exposed for serialisation and interop only.
    using ValueType = std::uint64_t;

    /// Index into the registry's dense array.
    using IndexType = std::uint32_t;

    /// Construct a null handle. Always invalid.
    constexpr Handle() noexcept = default;

    /// Constructs a handle from its packed representation.
    ///
    /// @note Public because this is how handles are persisted and passed
    ///       across a network or scripting boundary. Passing an arbitrary value
    ///       from untrusted data is safe: a handle built from an out-of-range
    ///       index or a stale generation fails validation rather than reading
    ///       memory.
    [[nodiscard]] static constexpr Handle FromValue(ValueType value) noexcept
    {
        Handle handle;
        handle.m_Value = value;
        return handle;
    }

    /// Reconstructs a handle from the parts stored by a registry.
    [[nodiscard]] static constexpr Handle FromParts(IndexType index,
                                                     std::uint32_t generation) noexcept
    {
        return FromValue((static_cast<ValueType>(generation) << Detail::kHandleIndexBits) |
                         static_cast<ValueType>(index));
    }

    /// @return True if this handle is not the null handle.
    ///
    /// @note THIS IS NOT A VALIDITY CHECK. It only rejects the null handle; an
    ///       index within range of a different registry, or a handle whose slot
    ///       has since been destroyed, still returns true. Use a registry's
    ///       IsAlive to test real validity - the distinction matters because
    ///       `if (handle)` reading as "is this entity alive" is the exact
    ///       mistake this class was built to prevent.
    [[nodiscard]] constexpr bool IsSet() const noexcept
    {
        return m_Value != 0;
    }

    /// @return The slot index this handle refers to.
    [[nodiscard]] constexpr IndexType GetIndex() const noexcept
    {
        return static_cast<IndexType>(m_Value & 0xFFFFFFFFu);
    }

    /// @return The generation the slot had when this handle was issued.
    [[nodiscard]] constexpr std::uint32_t GetGeneration() const noexcept
    {
        return static_cast<std::uint32_t>(m_Value >> Detail::kHandleIndexBits);
    }

    /// @return The packed 64-bit representation.
    [[nodiscard]] constexpr ValueType GetValue() const noexcept
    {
        return m_Value;
    }

    /// Invalidates this handle in place.
    constexpr void Reset() noexcept
    {
        m_Value = 0;
    }

    /// @note Explicit, not implicit: a bool conversion would make `if (handle)`
    ///       legal, and most such tests mean "is it alive" - which is a
    ///       different, registry-dependent question. IsSet() is the honest name.
    [[nodiscard]] explicit constexpr operator bool() const noexcept
    {
        return IsSet();
    }

    /// @return Hex representation, for logs and debugger displays.
    ///
    /// @note Defined inline because it is a member of a class template - an
    ///       out-of-line definition would need an explicit instantiation for
    ///       every Tag, and every new tag would produce a link error the first
    ///       time somebody logged a handle.
    [[nodiscard]] std::string ToString() const
    {
        static constexpr std::string_view kHexDigits = "0123456789ABCDEF";

        if (m_Value == 0)
        {
            return "Handle(null)";
        }

        std::string text = "Handle(0x";
        text.reserve(text.size() + 17);
        for (int shift = 60; shift >= 0; shift -= 4)
        {
            text += kHexDigits[(m_Value >> shift) & 0xFULL];
        }
        text += ')';
        return text;
    }

    // -- Comparison ---------------------------------------------------------
    // Value equality is the correct semantics: two handles are the same handle
    // when they name the same entity generation, not merely the same slot.

    [[nodiscard]] friend constexpr bool operator==(const Handle& lhs,
                                                  const Handle& rhs) noexcept
    {
        return lhs.m_Value == rhs.m_Value;
    }

    [[nodiscard]] friend constexpr bool operator!=(const Handle& lhs,
                                                  const Handle& rhs) noexcept
    {
        return !(lhs == rhs);
    }

    // Ordered comparison lets handles be used as dense-array indices, which is
    // what makes the common "sort/dedup a handle list" path possible.
    [[nodiscard]] friend constexpr bool operator<(const Handle& lhs,
                                                 const Handle& rhs) noexcept
    {
        return lhs.m_Value < rhs.m_Value;
    }

private:
    ValueType m_Value = 0;
};

/// Makes Handle usable as a key in unordered containers.
template<typename Tag>
struct std::hash<Nova::Handle<Tag>>
{
    [[nodiscard]] std::size_t operator()(const Nova::Handle<Tag>& handle) const noexcept
    {
        // Fibonacci hashing on the packed value. Handles allocated in sequence
        // differ only in the low bits, so identity hashing would collide
        // massively in any power-of-two bucket count. The multiply-shift mixes
        // the high bits down before the modulus.
        return static_cast<std::size_t>(handle.GetValue() * 0x9E3779B97F4A7C15ull);
    }
};

} // namespace Nova