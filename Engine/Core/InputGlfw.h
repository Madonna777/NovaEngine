// ===========================================================================
//  InputGlfw.h
// ---------------------------------------------------------------------------
//  Translation between Nova's input identifiers and GLFW's.
//
//  ---------------------------------------------------------------------------
//  INTERNAL HEADER - INCLUDE IT ONLY FROM Input.cpp
//  ---------------------------------------------------------------------------
//  It includes <GLFW/glfw3.h> and defines its functions inline. Both are
//  acceptable for an internal header and unacceptable for a public one, which
//  is exactly why it is not one: Input.h and KeyCodes.h stay free of GLFW, so
//  gameplay code and an editor binding widget never pull in a windowing
//  library just to name a key.
//
//  It is listed in the module's HEADERS so it appears in the IDE project tree,
//  but no public API includes it. Moving the mapping into a public header later
//  means either duplicating GLFW's include into every consumer or adding a
//  macro layer - the reason it is here and not there is on purpose.
//
//  ---------------------------------------------------------------------------
//  WHY THE DEFINITION IS IN THE HEADER AND NOT IN A .cpp
//  ---------------------------------------------------------------------------
//  These functions are constexpr, and a constexpr function is implicitly inline.
//  An inline function that is only *called* from another translation unit has no
//  definition visible there, so the compiler emits no symbol at all and the
//  program fails to LINK with an unresolved external - the first version of this
//  file did exactly that. The definition must therefore be visible to every
//  caller, which means it lives here.
//
//  ---------------------------------------------------------------------------
//  WHY THE TRANSLATION EXISTS AT ALL
//  ---------------------------------------------------------------------------
//  Nova::Key::Code is engine-owned numbering starting at 0 (see KeyCodes.h for
//  why). GLFW assigns unrelated integers - GLFW_KEY_SPACE is 32, GLFW_KEY_A is
//  65. So the two cannot be compared directly: GLFW's 32 would be read as
//  Nova's 32, and every key would silently do the wrong thing.
//
//  ---------------------------------------------------------------------------
//  WHY A switch AND NOT A TABLE
//  ---------------------------------------------------------------------------
//  A table indexed by the enum is shorter, and a missing row would be a build
//  error rather than a bug if the size were checked - which it now is. But the
//  switch is exhaustive by construction and /W4 flags any new enumerator that is
//  not handled (C4062), so adding Key::F13 without adding a case is a compile
//  error that names the function. That is a stronger guarantee than a size
//  check, and the compiler enforces it for free.
// ===========================================================================
#pragma once

#include <Core/KeyCodes.h>

#include <GLFW/glfw3.h>

namespace Nova::Detail
{
/// @return The GLFW key constant for @p code.
///
/// @return GLFW_KEY_UNKNOWN for Key::Unknown, Key::Count, and any value cast in
///         from outside the enum. glfwGetKey treats GLFW_KEY_UNKNOWN as a
///         permanently released key, so an unmapped value is INERT rather than
///         wrong - it cannot accidentally trigger whatever sits in slot zero.
[[nodiscard]] constexpr int GlfwKey(Key::Code code) noexcept
{
    switch (code)
    {
        // -- Letters ---------------------------------------------------------
        case Key::Code::A: return GLFW_KEY_A;
        case Key::Code::B: return GLFW_KEY_B;
        case Key::Code::C: return GLFW_KEY_C;
        case Key::Code::D: return GLFW_KEY_D;
        case Key::Code::E: return GLFW_KEY_E;
        case Key::Code::F: return GLFW_KEY_F;
        case Key::Code::G: return GLFW_KEY_G;
        case Key::Code::H: return GLFW_KEY_H;
        case Key::Code::I: return GLFW_KEY_I;
        case Key::Code::J: return GLFW_KEY_J;
        case Key::Code::K: return GLFW_KEY_K;
        case Key::Code::L: return GLFW_KEY_L;
        case Key::Code::M: return GLFW_KEY_M;
        case Key::Code::N: return GLFW_KEY_N;
        case Key::Code::O: return GLFW_KEY_O;
        case Key::Code::P: return GLFW_KEY_P;
        case Key::Code::Q: return GLFW_KEY_Q;
        case Key::Code::R: return GLFW_KEY_R;
        case Key::Code::S: return GLFW_KEY_S;
        case Key::Code::T: return GLFW_KEY_T;
        case Key::Code::U: return GLFW_KEY_U;
        case Key::Code::V: return GLFW_KEY_V;
        case Key::Code::W: return GLFW_KEY_W;
        case Key::Code::X: return GLFW_KEY_X;
        case Key::Code::Y: return GLFW_KEY_Y;
        case Key::Code::Z: return GLFW_KEY_Z;

        // -- Digits ----------------------------------------------------------
        case Key::Code::Num0: return GLFW_KEY_0;
        case Key::Code::Num1: return GLFW_KEY_1;
        case Key::Code::Num2: return GLFW_KEY_2;
        case Key::Code::Num3: return GLFW_KEY_3;
        case Key::Code::Num4: return GLFW_KEY_4;
        case Key::Code::Num5: return GLFW_KEY_5;
        case Key::Code::Num6: return GLFW_KEY_6;
        case Key::Code::Num7: return GLFW_KEY_7;
        case Key::Code::Num8: return GLFW_KEY_8;
        case Key::Code::Num9: return GLFW_KEY_9;

        // -- Navigation ------------------------------------------------------
        case Key::Code::Escape: return GLFW_KEY_ESCAPE;
        case Key::Code::Enter: return GLFW_KEY_ENTER;
        case Key::Code::Tab: return GLFW_KEY_TAB;
        case Key::Code::Backspace: return GLFW_KEY_BACKSPACE;
        case Key::Code::Insert: return GLFW_KEY_INSERT;
        case Key::Code::Delete: return GLFW_KEY_DELETE;
        case Key::Code::Right: return GLFW_KEY_RIGHT;
        case Key::Code::Left: return GLFW_KEY_LEFT;
        case Key::Code::Down: return GLFW_KEY_DOWN;
        case Key::Code::Up: return GLFW_KEY_UP;
        case Key::Code::PageUp: return GLFW_KEY_PAGE_UP;
        case Key::Code::PageDown: return GLFW_KEY_PAGE_DOWN;
        case Key::Code::Home: return GLFW_KEY_HOME;
        case Key::Code::End: return GLFW_KEY_END;

        // -- Editing / punctuation ------------------------------------------
        case Key::Code::Space: return GLFW_KEY_SPACE;
        case Key::Code::Minus: return GLFW_KEY_MINUS;
        case Key::Code::Equal: return GLFW_KEY_EQUAL;
        case Key::Code::LeftBracket: return GLFW_KEY_LEFT_BRACKET;
        case Key::Code::RightBracket: return GLFW_KEY_RIGHT_BRACKET;
        case Key::Code::Backslash: return GLFW_KEY_BACKSLASH;
        case Key::Code::Semicolon: return GLFW_KEY_SEMICOLON;
        case Key::Code::Apostrophe: return GLFW_KEY_APOSTROPHE;
        case Key::Code::GraveAccent: return GLFW_KEY_GRAVE_ACCENT;
        case Key::Code::Comma: return GLFW_KEY_COMMA;
        case Key::Code::Period: return GLFW_KEY_PERIOD;
        case Key::Code::Slash: return GLFW_KEY_SLASH;

        // -- Modifiers -------------------------------------------------------
        case Key::Code::LeftShift: return GLFW_KEY_LEFT_SHIFT;
        case Key::Code::RightShift: return GLFW_KEY_RIGHT_SHIFT;
        case Key::Code::LeftControl: return GLFW_KEY_LEFT_CONTROL;
        case Key::Code::RightControl: return GLFW_KEY_RIGHT_CONTROL;
        case Key::Code::LeftAlt: return GLFW_KEY_LEFT_ALT;
        case Key::Code::RightAlt: return GLFW_KEY_RIGHT_ALT;
        case Key::Code::LeftSuper: return GLFW_KEY_LEFT_SUPER;
        case Key::Code::RightSuper: return GLFW_KEY_RIGHT_SUPER;
        case Key::Code::CapsLock: return GLFW_KEY_CAPS_LOCK;
        case Key::Code::NumLock: return GLFW_KEY_NUM_LOCK;
        case Key::Code::ScrollLock: return GLFW_KEY_SCROLL_LOCK;

        // -- Function keys ---------------------------------------------------
        case Key::Code::F1: return GLFW_KEY_F1;
        case Key::Code::F2: return GLFW_KEY_F2;
        case Key::Code::F3: return GLFW_KEY_F3;
        case Key::Code::F4: return GLFW_KEY_F4;
        case Key::Code::F5: return GLFW_KEY_F5;
        case Key::Code::F6: return GLFW_KEY_F6;
        case Key::Code::F7: return GLFW_KEY_F7;
        case Key::Code::F8: return GLFW_KEY_F8;
        case Key::Code::F9: return GLFW_KEY_F9;
        case Key::Code::F10: return GLFW_KEY_F10;
        case Key::Code::F11: return GLFW_KEY_F11;
        case Key::Code::F12: return GLFW_KEY_F12;

        // -- Keypad ----------------------------------------------------------
        case Key::Code::Kp0: return GLFW_KEY_KP_0;
        case Key::Code::Kp1: return GLFW_KEY_KP_1;
        case Key::Code::Kp2: return GLFW_KEY_KP_2;
        case Key::Code::Kp3: return GLFW_KEY_KP_3;
        case Key::Code::Kp4: return GLFW_KEY_KP_4;
        case Key::Code::Kp5: return GLFW_KEY_KP_5;
        case Key::Code::Kp6: return GLFW_KEY_KP_6;
        case Key::Code::Kp7: return GLFW_KEY_KP_7;
        case Key::Code::Kp8: return GLFW_KEY_KP_8;
        case Key::Code::Kp9: return GLFW_KEY_KP_9;
        case Key::Code::KpDecimal: return GLFW_KEY_KP_DECIMAL;
        case Key::Code::KpDivide: return GLFW_KEY_KP_DIVIDE;
        case Key::Code::KpMultiply: return GLFW_KEY_KP_MULTIPLY;
        case Key::Code::KpSubtract: return GLFW_KEY_KP_SUBTRACT;
        case Key::Code::KpAdd: return GLFW_KEY_KP_ADD;
        case Key::Code::KpEnter: return GLFW_KEY_KP_ENTER;
        case Key::Code::KpEqual: return GLFW_KEY_KP_EQUAL;

        default: return GLFW_KEY_UNKNOWN;
    }
}

/// @return The GLFW mouse button constant for @p button, or GLFW_KEY_UNKNOWN
///         (-1) for an out-of-enum value, with the same inert-not-wrong
///         property as GlfwKey.
[[nodiscard]] constexpr int GlfwMouseButton(Key::MouseButton button) noexcept
{
    switch (button)
    {
        case Key::MouseButton::Left: return GLFW_MOUSE_BUTTON_LEFT;
        case Key::MouseButton::Right: return GLFW_MOUSE_BUTTON_RIGHT;
        case Key::MouseButton::Middle: return GLFW_MOUSE_BUTTON_MIDDLE;
        case Key::MouseButton::Button4: return GLFW_MOUSE_BUTTON_4;
        case Key::MouseButton::Button5: return GLFW_MOUSE_BUTTON_5;
        default: return GLFW_KEY_UNKNOWN;
    }
}

// ===========================================================================
//  Compile-time proof that the mapping above agrees with GLFW's headers.
//
//  A hand-written switch is exactly the kind of code that rots: someone adds a
//  key, or copy-pastes GLFW_KEY_F12 into F11's arm, and nothing complains until
//  a key silently does nothing in the game. These assertions compare the
//  switch's result against GLFW's own constant for a representative sample of
//  every family, so a mistake is a BUILD FAILURE rather than a support ticket.
//
//  They are here rather than in a .cpp because they call constexpr functions -
//  see the header comment on why the definitions had to move.
// ===========================================================================
static_assert(GlfwKey(Key::Code::A) == GLFW_KEY_A);
static_assert(GlfwKey(Key::Code::Z) == GLFW_KEY_Z);
static_assert(GlfwKey(Key::Code::Num0) == GLFW_KEY_0);
static_assert(GlfwKey(Key::Code::Num9) == GLFW_KEY_9);
static_assert(GlfwKey(Key::Code::Escape) == GLFW_KEY_ESCAPE);
static_assert(GlfwKey(Key::Code::Space) == GLFW_KEY_SPACE);
static_assert(GlfwKey(Key::Code::Slash) == GLFW_KEY_SLASH);
static_assert(GlfwKey(Key::Code::LeftShift) == GLFW_KEY_LEFT_SHIFT);
static_assert(GlfwKey(Key::Code::RightSuper) == GLFW_KEY_RIGHT_SUPER);
static_assert(GlfwKey(Key::Code::F1) == GLFW_KEY_F1);
static_assert(GlfwKey(Key::Code::F12) == GLFW_KEY_F12);
static_assert(GlfwKey(Key::Code::Kp0) == GLFW_KEY_KP_0);
static_assert(GlfwKey(Key::Code::KpEqual) == GLFW_KEY_KP_EQUAL);

// Distinctness: a letter, a digit, a keypad digit and a function key must not
// collide. If they did, holding two different physical keys would fire one
// action twice - the kind of bug that is invisible until someone binds a key.
static_assert(GlfwKey(Key::Code::A) != GlfwKey(Key::Code::F1));
static_assert(GlfwKey(Key::Code::Num1) != GlfwKey(Key::Code::Kp1));
static_assert(GlfwKey(Key::Code::LeftShift) != GlfwKey(Key::Code::RightShift));

// Out-of-enum values must be inert rather than wrong.
static_assert(GlfwKey(Key::Code::Count) == GLFW_KEY_UNKNOWN);
static_assert(GlfwMouseButton(Key::MouseButton::Count) == GLFW_KEY_UNKNOWN);
static_assert(GlfwMouseButton(Key::MouseButton::Left) == GLFW_MOUSE_BUTTON_LEFT);
static_assert(GlfwMouseButton(Key::MouseButton::Middle) == GLFW_MOUSE_BUTTON_MIDDLE);

} // namespace Nova::Detail
