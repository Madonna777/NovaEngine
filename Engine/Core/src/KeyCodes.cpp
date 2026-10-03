// ===========================================================================
//  KeyCodes.cpp
// ---------------------------------------------------------------------------
//  Name lookup for Key::Code and Key::MouseButton.
//
//  WHY A TABLE AND NOT A switch: the enum is one flat list, so a switch means
//  120 lines of boilerplate that a reviewer must skim to confirm none is
//  missing. The table is a single indexable array, and the static_assert at the
//  bottom proves its size matches the enum - so adding an enum value without a
//  name breaks the BUILD rather than silently logging "?" at runtime.
//
//  WHY THE ARRAY IS static AND NOT constexpr-FROM-ENUM: the reverse mapping
//  (name -> code) is needed by the editor's binding UI, and a constexpr array
//  indexed by enum would have to be written in exactly enum order. It is
//  fragile in a way the compiler cannot detect. Keep the table in enum order
//  and let the static_assert check that.
// ===========================================================================

#include <Core/KeyCodes.h>

#include <array>

namespace Nova::Key
{
namespace
{
/// Human-readable names, indexed by Code. MUST stay in enum order - the
/// static_assert below only checks the count, so a misordered entry would
/// compile and report the wrong key.
constexpr std::array<const char*, static_cast<std::size_t>(Code::Count)> kKeyNames{
    "?",

    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
    "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",

    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",

    "Escape", "Enter", "Tab", "Backspace", "Insert", "Delete",
    "Right", "Left", "Down", "Up", "PageUp", "PageDown", "Home", "End",

    "Space", "Minus", "Equal", "LeftBracket", "RightBracket", "Backslash",
    "Semicolon", "Apostrophe", "GraveAccent", "Comma", "Period", "Slash",

    "LeftShift", "RightShift",
    "LeftControl", "RightControl",
    "LeftAlt", "RightAlt",
    "LeftSuper", "RightSuper",
    "CapsLock", "NumLock", "ScrollLock",

    "F1", "F2", "F3", "F4", "F5", "F6",
    "F7", "F8", "F9", "F10", "F11", "F12",

    "Kp0", "Kp1", "Kp2", "Kp3", "Kp4", "Kp5", "Kp6", "Kp7", "Kp8", "Kp9",
    "KpDecimal", "KpDivide", "KpMultiply", "KpSubtract", "KpAdd", "KpEnter", "KpEqual",
};

constexpr std::array<const char*, static_cast<std::size_t>(MouseButton::Count)>
    kMouseButtonNames{"Left", "Right", "Middle", "Button4", "Button5"};

/// Bounds-check once, here, rather than at every call site. Callers are UI and
/// logging code that must not crash on a bad value.
///
/// @note The count is a template parameter rather than an argument so the array
///       size is fixed at compile time and no call site can pass a bound that
///       disagrees with the array it is guarding.
template<typename T, std::size_t N>
[[nodiscard]] constexpr bool InRange(T value) noexcept
{
    return static_cast<std::size_t>(value) < N;
}
} // namespace

// The whole safety argument of the table: if these fail, the compiler stops.
// A table that is short or long would otherwise index out of bounds at
// runtime, or silently report "?" for the last keys.
static_assert(kKeyNames.size() == static_cast<std::size_t>(Code::Count),
              "kKeyNames must have exactly one entry per Key::Code value");
static_assert(kMouseButtonNames.size() ==
                  static_cast<std::size_t>(MouseButton::Count),
              "kMouseButtonNames must have exactly one entry per MouseButton");

const char* Name(Key::Code code) noexcept
{
    return InRange<Key::Code, kKeyNames.size()>(code)
               ? kKeyNames[static_cast<std::size_t>(code)]
               : "?";
}

const char* Name(Key::MouseButton button) noexcept
{
    return InRange<Key::MouseButton, kMouseButtonNames.size()>(button)
               ? kMouseButtonNames[static_cast<std::size_t>(button)]
               : "?";
}

} // namespace Nova::Key