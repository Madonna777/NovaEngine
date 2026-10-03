// ===========================================================================
//  Input.h
// ---------------------------------------------------------------------------
//  Polling-based input state.
//
//  WHY THIS IS A STATIC CLASS
//  --------------------------
//  Input is queried, never owned. There is exactly one keyboard and one mouse
//  per machine, "two Input instances" is not a meaningful configuration, and
//  threading an Input& through every function that reads a key would be noise
//  on the signature of code that has nothing to do with input. The same
//  reasoning applies to Log: a global query surface is the honest shape for
//  process-wide state.
//
//  THE COST, STATED PLAINLY: a static class cannot be handed a different
//  backend per test, so input logic is verified by driving the real window
//  rather than by a unit test with a mock. That is a genuine cost, accepted
//  because the alternative - injecting an interface through the entire scene
//  graph to test one boolean - costs more at every call site.
//
//  WHY POLLING AND NOT CALLBACKS
//  -----------------------------
//  Callbacks answer "a key was pressed"; polling answers "is this key down
//  right now". For a game the second question is the one actually asked, and it
//  is asked per frame: movement, holding to sprint, keeping a menu open.
//  Callback state has to be latched and cleared every frame, which is polling
//  with extra bookkeeping and one more place to get it wrong.
//
//  The case callbacks genuinely win is a discrete one-shot ("press E once"),
//  and that is handled here as an EDGE derived from the polled level - see
//  IsKeyPressed. Same information, no queue.
//
//  WHY INPUT OWNS NO WINDOW
//  -----------------------
//  GLFW tracks key state per window, so polling needs a handle. Input does not
//  store one; Application::Run() passes the window to Update() each frame. That
//  keeps Input from becoming a second owner of the window, and it means the
//  multi-window case needs no redesign - only a second call site.
//
//  WHY Key::Code IS NOT GLFW'S NUMBERING
//  -------------------------------------
//  See KeyCodes.h: engine-owned values keep gameplay bindings stable across a
//  change of windowing backend. The translation is done once, in this file.
// ===========================================================================
#pragma once

#include <Core/KeyCodes.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace Nova
{
class Window;

/// Process-wide input state, polled once per frame.
class Input final
{
public:
    Input(const Input&)            = delete;
    Input& operator=(const Input&) = delete;
    Input()                        = delete;

    /// Prepares input state for use. @p window is the window whose events are
    /// polled; it must outlive Input's use.
    ///
    /// @note Validates that the window handle is non-null. A null handle makes
    ///       GLFW poll a window that does not exist, which is undefined
    ///       behaviour rather than a clean failure - so it is rejected here.
    static void Initialize(const Window& window);

    /// Clears all state. The window's own key state belongs to the window's
    /// lifetime and is not touched here.
    static void Shutdown();

    [[nodiscard]] static bool IsInitialized();

    // -----------------------------------------------------------------------
    //  Keys
    // -----------------------------------------------------------------------

    /// @return True while @p key is held down right now.
    [[nodiscard]] static bool IsKeyDown(Key::Code key);

    /// @return True only on the frame @p key went down.
    ///
    /// @note Requires Update() to have run this frame. With no previous frame's
    ///       state there is no edge to detect, so this returns false rather
    ///       than guessing.
    [[nodiscard]] static bool IsKeyPressed(Key::Code key);

    /// @return True only on the frame @p key came up.
    [[nodiscard]] static bool IsKeyReleased(Key::Code key);

    /// True while any of @p keys is held. Takes a braced-init-list, so the call
    /// reads as the combination it expresses:
    ///
    ///     Input::IsAnyKeyDown({Key::LeftShift, Key::RightShift});
    template<std::size_t N>
    [[nodiscard]] static bool IsAnyKeyDown(const Key::Code (&keys)[N])
    {
        for (const Key::Code key : keys)
        {
            if (IsKeyDown(key))
            {
                return true;
            }
        }
        return false;
    }

    // -----------------------------------------------------------------------
    //  Mouse
    // -----------------------------------------------------------------------

    [[nodiscard]] static bool IsMouseButtonDown(Key::MouseButton button);
    [[nodiscard]] static bool IsMouseButtonPressed(Key::MouseButton button);
    [[nodiscard]] static bool IsMouseButtonReleased(Key::MouseButton button);

    /// @return Cursor position in pixels, origin at the CLIENT area's top-left.
    ///
    /// @note Client-relative, not screen-relative. The window can be moved
    ///       between two polled frames, so a screen-absolute position is not
    ///       stable for a windowed application. Anything needing screen
    ///       coordinates must add the window origin itself.
    [[nodiscard]] static std::pair<float, float> GetMousePosition();

    [[nodiscard]] static float GetMouseX();
    [[nodiscard]] static float GetMouseY();

    /// @return Cursor movement since the previous frame, in pixels. Positive X
    ///         is right and positive Y is DOWN, matching the client-area
    ///         convention. A renderer must flip Y when projecting to the GPU's
    ///         NDC, which has +Y up - see Math when it lands.
    [[nodiscard]] static std::pair<float, float> GetMouseDelta();

    // -----------------------------------------------------------------------
    //  Frame services
    // -----------------------------------------------------------------------

    /// Latches this frame's state from @p window and computes the edges against
    /// the previous frame.
    ///
    /// @note MUST be called exactly once per frame, AFTER the platform has
    ///       delivered events and BEFORE any query. Called twice in a frame it
    ///       destroys the edge it was supposed to compute; called zero times the
    ///       previous state goes stale and a press can be missed entirely.
    ///
    /// Application::Run() does this. It is public only so a test or a bespoke
    /// loop can drive input directly.
    static void Update(const Window& window);

private:
    /// Frame state lives in the .cpp, in one struct. Private accessors here
    /// would only forward to it, and forwarders in a header are a maintenance
    /// cost with no corresponding benefit.
    struct State;
    [[nodiscard]] static State& Get();
};
} // namespace Nova