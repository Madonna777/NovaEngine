// ===========================================================================
//  EventSystem.h
// ---------------------------------------------------------------------------
//  A minimal event bus. Subscribers filter by event type and get invoked with
//  an immutable event record; the dispatcher owns no state about what the
//  listeners are - it only ferries messages at the moment their source fires.
//
//  WHY MAIN-THREAD ONLY
//  ---------------------
//  GLFW callbacks run inside glfwPollEvents on the thread that owns the
//  window. If a listener posts an event from a worker thread via a queue, that
//  is fine, but Dispatch is not safe from a listener re-entrantly touching the
//  same list being iterated: handlers_ vectors are iterated in place. The
//  pragmatic rule for the engine so far is that events are DISPATCHED on the
//  main thread only, during a defined window of the frame.
// ===========================================================================
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <variant>
#include <vector>

namespace Nova
{
/// The flavours of events this engine currently understands. None is the
/// variant zero-state and Count sizes the handler array; neither is a real
/// event type.
enum class EventType : std::uint8_t
{
    None = 0,
    KeyPressed,
    KeyReleased,
    MouseMoved,
    MouseButtonPressed,
    MouseButtonReleased,
    WindowResize,
    WindowClose,
    Count
};

struct KeyEvent
{
    /// GLFW-style key code (not the engine-owned Key::Code), see the Window
    /// header's note: the event carries the backend's code so the bus
    /// does not need the mapping table. Map through InputGlfw if a Key::Code
    /// is needed inside a subscriber.
    int key;
    int scancode;
    int mods; ///< GLFW modifier bitmask (GLFW_MOD_SHIFT etc.).
};

struct MouseMoveEvent
{
    float x;   ///< Client-area coordinates of the cursor.
    float y;
    float deltaX; ///< Raw movement observed since the previous move event.
    float deltaY;
};

struct MouseButtonEvent
{
    int button;
    int mods;
};

struct WindowResizeEvent
{
    int width;
    int height;
};

/// Empty payload: the window asked to close.
struct WindowCloseEvent
{
};

/// Tagged-union parameter for EventHandler. A std::variant means subscribers
/// match on types with std::get_if rather than needing to type-pun through a
/// union manually.
using EventData = std::variant<std::monostate, KeyEvent, MouseMoveEvent, MouseButtonEvent,
                               WindowResizeEvent, WindowCloseEvent>;

/// One event on the bus. Moved by value through Dispatch; copying is cheap
/// because the variant payloads are all trivially copyable structs.
struct Event
{
    EventType type = EventType::None;
    EventData data{};

    /// Convenience accessor: throws std::bad_variant_access on a type mismatch,
    /// which is preferable to UB when a subscriber reads the wrong arm.
    template<typename T>
    const T& As() const
    {
        return std::get<T>(data);
    }
};

/// Fan-out hub keyed by EventType. Copying is intentionally not allowed: a
/// Window's EventDispatcher *is* the live instance, and copying it would
/// dispatch to the wrong handler set.
///
/// Subscription removal is explicit: a long-lived object that subscribes in its
/// constructor must unsubscribe in its destructor, or the handler list holds a
/// lambda whose captured `this` has become dangling. Every Subscribe returns a
/// Token that doubles as that object's registration id.
class EventDispatcher final
{
public:
    using Handler = std::function<void(const Event&)>;

    /// Registration id for a later Unsubscribe. Always nonzero; zero means
    /// "no subscription".
    using Token = std::uint64_t;

    [[nodiscard]] Token Subscribe(EventType type, Handler handler)
    {
        Token token = ++nextToken_;
        handlers_[ToIndex(type)].push_back({ token, std::move(handler) });
        return token;
    }

    /// Removes one subscription. No-op if the token is gone or zeroed, so
    /// destructors can call it unconditionally (e.g. when SetActive toggles).
    void Unsubscribe(EventType type, Token token)
    {
        auto& list = handlers_[ToIndex(type)];
        list.erase(std::remove_if(list.begin(), list.end(),
                                  [token](const Entry& entry) { return entry.token == token; }),
                   list.end());
    }

    void Dispatch(const Event& event)
    {
        for (auto& entry : handlers_[ToIndex(event.type)])
        {
            entry.handler(event);
        }
    }

    /// Removes all subscribers. Rare: a dispatcher reset, used on shutdown.
    void Clear()
    {
        for (auto& list : handlers_)
        {
            list.clear();
        }
    }

private:
    // An Entry carries the token and the handler for one subscription.
    struct Entry
    {
        Token   token;
        Handler handler;
    };

    static constexpr std::size_t ToIndex(EventType type) noexcept
    {
        return static_cast<std::size_t>(type);
    }

    std::array<std::vector<Entry>, static_cast<std::size_t>(EventType::Count)> handlers_{};
    Token nextToken_ = 0;
};

} // namespace Nova
