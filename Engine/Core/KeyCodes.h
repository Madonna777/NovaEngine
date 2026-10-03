// ===========================================================================
//  KeyCodes.h
// ---------------------------------------------------------------------------
//  Engine-owned key identifiers.
//
//  WHY THESE NUMBERS ARE OURS AND NOT GLFW'S
//  ------------------------------------------
//  GLFW assigns its own integer to every key (GLFW_KEY_ESCAPE == 256, and so
//  on). Copying those numbers into Nova::Key would make them an ABI dependency
//  on GLFW, which is exactly the kind of leak a platform layer is supposed to
//  prevent: change the windowing backend and every keycode in gameplay code
//  silently changes meaning.
//
//  So Nova::Key::Code is a compact enum starting at 0, and Input maps it to the
//  backend's value at the point of comparison. The cost is one switch in
//  Input.cpp. The benefit is that input bindings stay valid across a swap of
//  GLFW for native Win32, and can be serialised to a config file as small
//  stable integers.
//
//  WHY KeyCodes.h IS A SEPARATE HEADER: this is the one file in the input path
//  with no dependencies at all - not even the platform layer. That lets a
//  serialisation module or an editor widget name a key without pulling in GLFW
//  or anything else.
// ===========================================================================
#pragma once

#include <cstdint>

namespace Nova
{
/// Key identifiers. Values are engine-owned and stable; see the file header for
/// why they deliberately do not match GLFW's.
namespace Key
{
enum class Code : std::uint16_t
{
    Unknown = 0,

    // Letters
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

    // Digits (top row)
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,

    // Navigation
    Escape, Enter, Tab, Backspace, Insert, Delete,
    Right, Left, Down, Up, PageUp, PageDown, Home, End,

    // Editing / punctuation
    Space, Minus, Equal, LeftBracket, RightBracket, Backslash,
    Semicolon, Apostrophe, GraveAccent, Comma, Period, Slash,

    // Modifiers
    LeftShift, RightShift,
    LeftControl, RightControl,
    LeftAlt, RightAlt,
    LeftSuper, RightSuper,
    CapsLock, NumLock, ScrollLock,

    // Function keys
    F1, F2, F3, F4, F5, F6,
    F7, F8, F9, F10, F11, F12,

    // Keypad
    Kp0, Kp1, Kp2, Kp3, Kp4, Kp5, Kp6, Kp7, Kp8, Kp9,
    KpDecimal, KpDivide, KpMultiply, KpSubtract, KpAdd, KpEnter, KpEqual,

    Count
};

/// Mouse button identifiers, using the same engine-owned-numbering rationale.
enum class MouseButton : std::uint8_t
{
    Left = 0,
    Right,
    Middle,
    Button4,
    Button5,

    Count
};

/// @return Short human-readable name for @p code ("Escape", "F1", ...), for
///         log output and the editor's input-binding UI.
///
/// @note Returns "?" for anything out of range. A binding UI that displayed a
///       raw integer instead would be unusable, and this must never throw or
///       assert - it is called while rendering text.
[[nodiscard]] const char* Name(Key::Code code) noexcept;

/// @return Short human-readable name for @p button ("Left", "Right", ...).
[[nodiscard]] const char* Name(Key::MouseButton button) noexcept;

} // namespace Key
} // namespace Nova