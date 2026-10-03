// ===========================================================================
//  Input.cpp
// ---------------------------------------------------------------------------
//  Per-frame input snapshot and edge detection.
// ===========================================================================

#include <Core/Input.h>

#include <Core/Assert.h>
#include <Core/InputGlfw.h>
#include <Core/Log.h>
#include <Core/Window.h>

#include <array>

#include <GLFW/glfw3.h>

namespace Nova
{
namespace
{
constexpr std::size_t kKeyCount   = static_cast<std::size_t>(Key::Code::Count);
constexpr std::size_t kMouseCount = static_cast<std::size_t>(Key::MouseButton::Count);

/// @return True if @p value is a valid index into a state array.
template<typename T>
[[nodiscard]] constexpr bool InRange(T value, std::size_t count) noexcept
{
    return static_cast<std::size_t>(value) < count;
}
} // namespace

// ===========================================================================
//  State
//
//  WHY TWO ARRAYS RATHER THAN A SET OF FLAGS
//  -----------------------------------------
//  An edge is by definition a comparison of two consecutive frames, so the
//  previous frame has to be retained somewhere. Two flat arrays make every
//  query a single ANDNOT instead of a per-key state machine. Under 200 keys the
//  memory is irrelevant, and the version that cannot get its clear/reset order
//  wrong is worth more than the version that is smaller.
struct Input::State
{
    bool initialized = false;

    /// True once Update() has produced at least one frame. Edge queries return
    /// false before that: without a previous frame there is no edge to report,
    /// and returning true would fire a press for a key that was already down
    /// when the game started.
    bool hasFrame = false;

    std::array<std::uint8_t, kKeyCount>    keyCurrent{};
    std::array<std::uint8_t, kKeyCount>    keyPrevious{};
    std::array<std::uint8_t, kMouseCount> mouseCurrent{};
    std::array<std::uint8_t, kMouseCount> mousePrevious{};

    float mouseX = 0.0F;
    float mouseY = 0.0F;

    float previousMouseX = 0.0F;
    float previousMouseY = 0.0F;

    /// Function-local static rather than a namespace-scope object: it is
    /// constructed on first use, so no other translation unit's static
    /// initialiser can read it while it is still zero-initialised. That
    /// ordering bug is legal C++ and miserable to diagnose.
    static State& Instance()
    {
        static State instance;
        return instance;
    }
};

Input::State& Input::Get()
{
    return State::Instance();
}

// ===========================================================================
//  Lifecycle
// ===========================================================================

void Input::Initialize(const Window& window)
{
    State& state = Get();

    // glfwGetKey and friends take a GLFWwindow* and do not validate it. A null
    // handle makes them read the key state of a window that does not exist -
    // undefined behaviour whose crash points at this file with no hint about
    // the real cause.
    NOVA_VERIFY_MSG(window.GetHandle() != nullptr,
                    "Input::Initialize: Window has no GLFW handle");

    state = State{};
    state.initialized = true;

    NOVA_INFO("Input initialised ({} keys, {} mouse buttons tracked)",
              static_cast<int>(kKeyCount), static_cast<int>(kMouseCount));
}

void Input::Shutdown()
{
    State& state = Get();
    state.initialized = false;

    NOVA_INFO("Input shut down");
}

bool Input::IsInitialized()
{
    return Get().initialized;
}

void Input::Update(const Window& window)
{
    State& state = Get();

    GLFWwindow* handle = window.GetHandle();
    if (handle == nullptr)
    {
        // Not fatal. This is the normal case on the shutdown frame, where the
        // window has already been destroyed but the loop still runs once more.
        // Returning leaves the last complete frame's state intact, so every
        // query reports "nothing pressed" instead of reading a dead handle.
        return;
    }

    // Shift, THEN poll. In the other order the first frame's "previous" state
    // is copied from an empty array and every key looks newly pressed, firing
    // a burst of phantom inputs on the first frame.
    state.keyPrevious    = state.keyCurrent;
    state.mousePrevious = state.mouseCurrent;
    state.previousMouseX = state.mouseX;
    state.previousMouseY = state.mouseY;

    // Every key is polled every frame, not only those something asked about.
    //
    // WHY NOT QUERY glfwGetKey LAZILY FROM EACH CALL SITE: the snapshot gives
    // every consumer in a frame the same view. Polling lazily would let two
    // queries in one frame disagree if the user pressed a key between them, and
    // the disagreement would be invisible and unreproducible.
    for (std::size_t i = 0; i < kKeyCount; ++i)
    {
        const auto code = static_cast<Key::Code>(i);
        const int   glfwKey = Detail::GlfwKey(code);

        // Skip Key::Unknown, and anything that failed to map.
        //
        // WHY: GlfwKey returns GLFW_KEY_UNKNOWN (-1) for those, and glfwGetKey
        // rejects a negative key with GLFW_INVALID_ENUM - through the error
        // callback, once per key per frame. That is exactly what the first run
        // did: an unbroken stream of "GLFW error 65539: Invalid key -1" that
        // would have buried every other message in the log. The mapping's
        // contract is that unmapped keys are inert, and the way to honour that
        // is to not ask about them at all.
        if (glfwKey < 0)
        {
            continue;
        }

        state.keyCurrent[i] =
            static_cast<std::uint8_t>(glfwGetKey(handle, glfwKey) == GLFW_PRESS ? 1 : 0);
    }

    for (std::size_t i = 0; i < kMouseCount; ++i)
    {
        const auto button = static_cast<Key::MouseButton>(i);
        const int   glfwButton = Detail::GlfwMouseButton(button);

        // Same reason as the key loop above: -1 is an invalid button to GLFW.
        if (glfwButton < 0)
        {
            continue;
        }

        state.mouseCurrent[i] =
            static_cast<std::uint8_t>(glfwGetMouseButton(handle, glfwButton) == GLFW_PRESS ? 1 : 0);
    }

    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(handle, &x, &y);

    state.mouseX = static_cast<float>(x);
    state.mouseY = static_cast<float>(y);
    state.hasFrame = true;
}

// ===========================================================================
//  Key queries
// ===========================================================================

bool Input::IsKeyDown(Key::Code key)
{
    const State& state = Get();
    return InRange(key, kKeyCount) && state.keyCurrent[static_cast<std::size_t>(key)] != 0;
}

bool Input::IsKeyPressed(Key::Code key)
{
    const State& state = Get();

    if (!state.hasFrame || !InRange(key, kKeyCount))
    {
        return false;
    }

    const auto index = static_cast<std::size_t>(key);
    return state.keyCurrent[index] != 0 && state.keyPrevious[index] == 0;
}

bool Input::IsKeyReleased(Key::Code key)
{
    const State& state = Get();

    if (!state.hasFrame || !InRange(key, kKeyCount))
    {
        return false;
    }

    const auto index = static_cast<std::size_t>(key);
    return state.keyCurrent[index] == 0 && state.keyPrevious[index] != 0;
}

// ===========================================================================
//  Mouse queries
// ===========================================================================

bool Input::IsMouseButtonDown(Key::MouseButton button)
{
    const State& state = Get();
    return InRange(button, kMouseCount) && state.mouseCurrent[static_cast<std::size_t>(button)] != 0;
}

bool Input::IsMouseButtonPressed(Key::MouseButton button)
{
    const State& state = Get();

    if (!state.hasFrame || !InRange(button, kMouseCount))
    {
        return false;
    }

    const auto index = static_cast<std::size_t>(button);
    return state.mouseCurrent[index] != 0 && state.mousePrevious[index] == 0;
}

bool Input::IsMouseButtonReleased(Key::MouseButton button)
{
    const State& state = Get();

    if (!state.hasFrame || !InRange(button, kMouseCount))
    {
        return false;
    }

    const auto index = static_cast<std::size_t>(button);
    return state.mouseCurrent[index] == 0 && state.mousePrevious[index] != 0;
}

std::pair<float, float> Input::GetMousePosition()
{
    const State& state = Get();
    return {state.mouseX, state.mouseY};
}

float Input::GetMouseX()
{
    return Get().mouseX;
}

float Input::GetMouseY()
{
    return Get().mouseY;
}

std::pair<float, float> Input::GetMouseDelta()
{
    const State& state = Get();

    // No previous frame means no delta exists. Zero is the correct answer here;
    // falling back to the absolute position would teleport the camera to the
    // cursor on the very first frame, which is a spectacular bug to chase.
    if (!state.hasFrame)
    {
        return {0.0F, 0.0F};
    }

    return {state.mouseX - state.previousMouseX, state.mouseY - state.previousMouseY};
}

} // namespace Nova
