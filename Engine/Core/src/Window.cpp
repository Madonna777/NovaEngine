// ===========================================================================
//  Window.cpp
// ---------------------------------------------------------------------------
//  GLFW-backed window. The only translation unit that includes <GLFW/glfw3.h>
//  for window management.
// ===========================================================================

#include <Core/Window.h>

#include <Core/GlfwRuntime.h>
#include <Core/Log.h>

#include <stdexcept>
#include <utility>

#include <GLFW/glfw3.h>

// Native Win32 access, for glfwGetWin32Window alone.
//
// WHY THE DEFINE AND A SECOND HEADER: GLFW deliberately keeps every platform
// type behind its own API. glfw3native.h declares glfwGetWin32Window only when
// GLFW_EXPOSE_NATIVE_WIN32 is defined first, because a program that never asks
// for native handles should not pay for <windows.h> and its macro definitions -
// the min/max problem being the one this engine cares about most. So the opt-in
// is explicit here rather than global.
//
// WHY THIS FILE MAY HAVE IT AT ALL, given that <windows.h> is confined to .cpp
// files (see Platform.h): the rule exists so that a HEADER can never drag
// windows.h into a translation unit that includes <algorithm>. This is a .cpp,
// it defines NOMINMAX and WIN32_LEAN_AND_MEAN already (both are also set by
// nova_configure_target for every target), and no engine header sees any of it.
// GetNativeHandle returns void* precisely so the HWND type stops here.
//
// WHY THE #ifndef GUARD: window handle types must agree across every
// translation unit, and GLFW's exposure macros are header-visible state. Setting
// it unconditionally is also fine, but a guarded define says "already decided
// somewhere above" instead of silently agreeing by luck.
#ifndef GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#endif
#include <GLFW/glfw3native.h>

namespace Nova
{
Window::Window(WindowProperties properties)
    : properties_(std::move(properties)), vsyncRequested_(properties_.vsync)
{
    // Takes a reference to the process-global GLFW runtime. Held for this
    // object's lifetime, which is what guarantees the library outlives every
    // window in the process. See GlfwRuntime.h for why this is counted.
    if (!Detail::AcquireGlfw())
    {
        // Fatal, and reported through the message rather than a NOVA_CRITICAL
        // that might be missed: an application with no window cannot do
        // anything useful, and returning a half-built Window would push this
        // failure to whoever called a method on it.
        throw std::runtime_error("Window: GLFW could not be initialised");
    }

    // GLFW window hints are process-global state consumed by the next
    // glfwCreateWindow call. Every hint that affects creation is set here and
    // nowhere else, so there is exactly one description of how Nova windows
    // look.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    // Why we are not asking for an OpenGL context: the renderer is D3D12 and
    // will create a DXGI swap chain bound to this HWND. Requesting a GL
    // context would load opengl32.dll and create one we would never use.
    glfwWindowHint(GLFW_RESIZABLE, properties_.resizable ? GLFW_TRUE : GLFW_FALSE);

    if (properties_.startHidden)
    {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    }

    // WHY NO GLFW_SAMPLES (MSAA) HERE: this hint asks the windowing system for
    // a multisampled default framebuffer, which only has meaning for OpenGL.
    // Under D3D12 multisampling is a property of the swap chain's
    // DXGI_SAMPLE_DESC, set at creation by the renderer. Setting this hint
    // would imply an anti-aliasing guarantee the engine cannot honour.

    handle_ = glfwCreateWindow(properties_.width, properties_.height,
                               properties_.title.c_str(), nullptr, nullptr);

    VerifyCreated();

    SetVSync(properties_.vsync);

    // The user pointer ties a GLFWwindow* back to this Window during callback
    // delivery. Set it BEFORE any callback can fire so an event never hits a
    // null pointer. The engine also polls via Input, but the callbacks below
    // carry every event the dispatcher delivers.
    glfwSetWindowUserPointer(handle_, this);

    // Callbacks are registered after the user pointer is installed: every
    // GLFW callback resolves the Window from it, so ordering is load-bearing.
    InstallEventCallbacks();

    NOVA_INFO("Window created: '{}' {}x{} vsync={}", properties_.title,
              properties_.width, properties_.height, properties_.vsync);
}

Window::~Window()
{
    // glfwDestroyWindow is safe on null, so a window whose creation failed
    // destructs cleanly. It MUST run before ReleaseGlfw: destroying a window
    // after the library is gone is undefined behaviour inside GLFW.
    if (handle_ != nullptr)
    {
        glfwDestroyWindow(handle_);
        handle_ = nullptr;
    }

    Detail::ReleaseGlfw();
}

void Window::VerifyCreated()
{
    if (handle_ != nullptr)
    {
        return;
    }

    // The GLFW error callback has already logged the reason. This message is
    // the one a crash dump or a build-server log will show, so it states the
    // consequence rather than repeating the cause.
    NOVA_CRITICAL("Window creation failed for '{}' {}x{}",
                  properties_.title, properties_.width, properties_.height);

    // Releases the reference taken in the constructor. Without this the
    // exception would unwind past the destructor - a constructor that throws
    // never runs its own destructor, so the count would leak by one and GLFW
    // would never be terminated.
    Detail::ReleaseGlfw();

    throw std::runtime_error("Window: glfwCreateWindow failed");
}

void Window::PumpEvents()
{
    // No-op if the window is gone. Makes the call safe in a shutdown path that
    // may run after the window was reset.
    if (handle_ == nullptr)
    {
        return;
    }

    glfwPollEvents();
}

void* Window::GetNativeHandle() const noexcept
{
    if (handle_ == nullptr)
    {
        return nullptr;
    }

    // glfwGetWin32Window is the single place the engine unwraps GLFW's Win32
    // window, which is the only reason this method can return void* safely.
    //
    // WHY NOT glfwGetX11Window / wl_display / whatever: this engine targets
    // Windows x64 and compiles nowhere else. A platform #ifdef here would be
    // dead code that still has to compile on a platform with no GLFW build,
    // which is the usual reason platform #ifdefs cost more than they save. If a
    // second platform ever lands, this returns nullptr there and every caller
    // already has to check - because the return type says "there might not be
    // one".
    //
    // @note The returned HWND is owned by GLFW and dies with the window. DXGI
    //       takes its own reference to it when a swap chain is created, but
    //       D3D12Context also reads it per frame for the resize check - which is
    //       why a context must be destroyed before the Window.
    return glfwGetWin32Window(handle_);
}

bool Window::ShouldClose() const
{
    return handle_ != nullptr && glfwWindowShouldClose(handle_) == GLFW_TRUE;
}

void Window::RequestClose()
{
    if (handle_ != nullptr)
    {
        glfwSetWindowShouldClose(handle_, GLFW_TRUE);
    }
}

int Window::GetWidth() const
{
    if (handle_ == nullptr)
    {
        return properties_.width;
    }

    int width  = 0;
    int height = 0;
    glfwGetWindowSize(handle_, &width, &height);
    return width;
}

int Window::GetHeight() const
{
    if (handle_ == nullptr)
    {
        return properties_.height;
    }

    int width  = 0;
    int height = 0;
    glfwGetWindowSize(handle_, &width, &height);
    return height;
}

std::pair<int, int> Window::GetSize() const
{
    if (handle_ == nullptr)
    {
        return {properties_.width, properties_.height};
    }

    int width  = 0;
    int height = 0;
    // ONE GLFW call for both dimensions. Two separate GetWidth/GetHeight calls
    // could straddle a user resize and return a pairing that never existed.
    glfwGetWindowSize(handle_, &width, &height);
    return {width, height};
}

float Window::GetAspectRatio() const
{
    const auto [width, height] = GetSize();
    return height != 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0F;
}

void Window::SetTitle(std::string_view title)
{
    if (handle_ != nullptr)
    {
        // std::string_view::data() is NOT guaranteed null-terminated, and
        // glfwSetWindowTitle takes a C string. The copy is made here rather
        // than once in the header because this is the only place it is needed.
        glfwSetWindowTitle(handle_, std::string{title}.c_str());
    }
    properties_.title = std::string{title};
}

std::string Window::GetTitle() const
{
    if (handle_ == nullptr)
    {
        return std::string{properties_.title};
    }

    const char* current = glfwGetWindowTitle(handle_);
    return current != nullptr ? std::string{current} : std::string{properties_.title};
}

void Window::SetSize(int width, int height)
{
    if (handle_ != nullptr)
    {
        glfwSetWindowSize(handle_, width, height);
    }
    properties_.width  = width;
    properties_.height = height;
}

void Window::SetVSync(bool enabled)
{
    // The request is RECORDED, not applied. There is deliberately no
    // glfwSwapInterval call here:
    //
    //   glfwSwapInterval sets the interval on the CURRENT OpenGL context. Our
    //   window is created with GLFW_CLIENT_API = GLFW_NO_API, so there is no
    //   context and the call fails with GLFW_ERROR_NO_CURRENT_CONTEXT on every
    //   invocation. It was tried; the Sandbox logged one error per call.
    //
    //   More importantly it would be the WRONG control even if it worked. The
    //   engine renders with D3D12, so the frame is presented by
    //   IDXGISwapChain::Present, and the vertical blank is that swap chain's
    //   business - DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL with a sync interval of 1,
    //   or swap chain v1's BufferCount of 2. Pacing a D3D12 frame from an OpenGL
    //   context that does not exist is not a shortcut, it is a category error.
    //
    // So this is a request the renderer consumes. IsVSyncEnabled() is how it
    // asks. Until a swap chain exists the value is simply inert, which is why
    // Sandbox logs "vsync=true" and then runs without any pacing at all - that
    // line is honest, not a bug.
    vsyncRequested_ = enabled;
}

bool Window::IsVSyncEnabled() const
{
    return vsyncRequested_;
}

void Window::SetVisible(bool visible)
{
    if (handle_ != nullptr)
    {
        if (visible)
        {
            glfwShowWindow(handle_);
        }
        else
        {
            glfwHideWindow(handle_);
        }
    }
}

double Window::GetTimeSeconds() const
{
    // GLFW's clock is monotonic and starts at library init, so the absolute
    // value is meaningless - only differences are. Documented at the call site
    // because "seconds since the window opened" is the correct reading and
    // "time of day" is the tempting wrong one.
    return glfwGetTime();
}

// ===========================================================================
//  GLFW callback bridge - the fan-out into EventDispatcher
// ===========================================================================

Window* Window::FromGlfwWindow(GLFWwindow* window) noexcept
{
    return static_cast<Window*>(glfwGetWindowUserPointer(window));
}

void Window::InstallEventCallbacks()
{
    // WHY THE REPEAT IS KEYPRESSED: GLFW fires GLFW_REPEAT for held keys. A
    // polled Input::IsKeyDown covers the held state; the bus only needs edge events, so
    // PRESS and REPEAT are the same KeyPressed. Notifying on every key repeat
    // would either double-trigger Shutdown() or force every subscriber to
    // throttle itself. The keyboard state for "is it held" remains the polled
    // Input facade - events are edges, Input is levels.
    glfwSetKeyCallback(handle_, &Window::OnGlfwKey);

    // Moves are reported regardless of whether the cursor is visible: the FPS
    // controller hides it and keeps consuming deltas, so both paths need the
    // same event data.
    glfwSetCursorPosCallback(handle_, &Window::OnGlfwCursorPosition);
    glfwSetMouseButtonCallback(handle_, &Window::OnGlfwMouseButton);
    glfwSetWindowSizeCallback(handle_, &Window::OnGlfwWindowSize);
    glfwSetWindowCloseCallback(handle_, &Window::OnGlfwWindowClose);
}

void Window::OnGlfwKey(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (Window* self = FromGlfwWindow(window); self != nullptr)
    {
        Event event;
        event.type = (action == GLFW_RELEASE) ? EventType::KeyReleased : EventType::KeyPressed;
        event.data = KeyEvent{ key, scancode, mods };
        self->dispatcher_.Dispatch(event);
    }
}

void Window::OnGlfwCursorPosition(GLFWwindow* window, double xpos, double ypos)
{
    if (Window* self = FromGlfwWindow(window); self != nullptr)
    {
        // delta = current - last, and the first move after construction or
        // re-acquire has no last - treating (0,0) as "last" would produce a
        // cursor-frozen-at-corner-followed-by-a-huge-delta artifact. Gating on
        // the flag means the first event reports a zero delta, which is the
        // honest answer for a line that has not moved yet.
        const float x = static_cast<float>(xpos);
        const float y = static_cast<float>(ypos);

        float deltaX = 0.0F;
        float deltaY = 0.0F;
        if (self->hasMousePosition_)
        {
            deltaX = x - self->lastMouseX_;
            deltaY = y - self->lastMouseY_;
        }

        self->lastMouseX_      = x;
        self->lastMouseY_      = y;
        self->hasMousePosition_ = true;

        Event event;
        event.type = EventType::MouseMoved;
        event.data = MouseMoveEvent{ x, y, deltaX, deltaY };
        self->dispatcher_.Dispatch(event);
    }
}

void Window::OnGlfwMouseButton(GLFWwindow* window, int button, int action, int mods)
{
    if (Window* self = FromGlfwWindow(window); self != nullptr)
    {
        Event event;
        event.type =
            (action == GLFW_RELEASE) ? EventType::MouseButtonReleased : EventType::MouseButtonPressed;
        event.data = MouseButtonEvent{ button, mods };
        self->dispatcher_.Dispatch(event);
    }
}

void Window::OnGlfwWindowSize(GLFWwindow* window, int width, int height)
{
    if (Window* self = FromGlfwWindow(window); self != nullptr)
    {
        Event event;
        event.type = EventType::WindowResize;
        event.data = WindowResizeEvent{ width, height };
        self->dispatcher_.Dispatch(event);
    }
}

void Window::OnGlfwWindowClose(GLFWwindow* window)
{
    if (Window* self = FromGlfwWindow(window); self != nullptr)
    {
        Event event;
        event.type = EventType::WindowClose;
        event.data = WindowCloseEvent{};
        self->dispatcher_.Dispatch(event);
    }
}

} // namespace Nova