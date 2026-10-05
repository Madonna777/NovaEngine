// ===========================================================================
//  Window.h
// ---------------------------------------------------------------------------
//  RAII wrapper around a GLFW window.
//
//  WHY A WRAPPER AND NOT CALLING GLFW DIRECTLY
//  -------------------------------------------
//  Three things, in order of how much they cost if ignored:
//
//  1. THE BACKEND CAN BE SWAPPED. Every window operation goes through this
//     class, so replacing GLFW with native Win32 touches Window.cpp and
//     nothing else. Gameplay code never names a GLFW type.
//
//  2. LIFETIME IS ENFORCED. A raw GLFWwindow* has no destructor: forget
//     glfwDestroyWindow and you leak a window handle plus its GL/Win32
//     resources, and the process may hang at exit. Here the window dies with
//     the object, including on an exception.
//
//  3. ONE PLACE TO ADD WINDOW STYLES. Fullscreen, borderless, high-DPI, and
//     icon are all GLFW window hints, and they must be set BEFORE creation.
//     Scattering them across the codebase means the same window is created
//     two different ways in two places.
//
//  LIFETIME ORDERING, WHICH IS THE PART THAT BITES
//  -----------------------------------------------
//  glfwInit() and glfwTerminate() are PROCESS-GLOBAL, not per-window. Two
//  windows must not each call glfwInit/glfwTerminate: the first one to be
//  destroyed would tear the library down underneath the second, and the next
//  call on it is undefined behaviour - typically a crash inside GLFW, on a
//  different thread, at a point that has nothing to do with the cause.
//
//  So global init is reference-counted in GlfwRuntime (see GlfwRuntime.h) and
//  the Window constructor/destructor take a reference. That file is the only
//  place in the engine that calls glfwInit or glfwTerminate.
//
//  A SECOND ORDERING HAZARD: the GLFW error callback is also process-global and
//  is NOT restored by glfwTerminate - it survives, still pointing at our
//  function. GlfwRuntime installs it on first init and clears it on final
//  teardown, so an error raised during shutdown cannot call into code that has
//  already been unloaded or into a half-destroyed logger.
// ===========================================================================
#pragma once

#include <Core/Assert.h>
#include <Core/EventSystem.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

// Forward declaration: the GLFW handle is opaque to every consumer, and
// including GLFW in a public header would put a third-party dependency into
// every file that merely wants to know the window size.
struct GLFWwindow;

namespace Nova
{
/// Everything needed to create a window. Passed by value, so a config file or
/// an editor panel can build one and hand it over without lifetime coupling.
struct WindowProperties
{
    /// Owned, not a view. A string_view would dangle the moment the title came
    /// from a config file's buffer, and SetTitle stores a copy back into this
    /// struct - so the view would be a view onto a temporary.
    std::string title = "NovaEngine";

    int width  = 1280;
    int height = 720;

    /// Requests vsync, via the display driver's vertical blank rather than a
    /// software frame cap. Costs up to one frame of latency and buys no
    /// tearing. On by default, because a visibly tearing window during
    /// renderer bring-up is a distraction.
    ///
    /// @note This is a REQUEST, applied by whoever presents the frame. See
    ///       Window::SetVSync for why GLFW cannot apply it and D3D12 must.
    bool vsync = true;

    /// Start hidden. Useful for tools that position the window before showing
    /// it, and it avoids a flash of a wrongly-sized window.
    bool startHidden = false;

    /// Ask the window manager to allow live resizing.
    bool resizable = true;
};

/// Owns one OS window. Non-copyable, non-movable: the handle is unique and
/// moved-from Window would have a null handle to destroy.
class Window final
{
public:
    /// @note Creation failure is fatal and throws std::runtime_error. A window
    ///       that could not be created leaves the engine with nothing to render
    ///       into, so there is no useful degraded mode to return to. The message
    ///       reaches the console via the GLFW error callback before the throw.
    explicit Window(WindowProperties properties);
    ~Window();

    Window(const Window&)            = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&)                 = delete;
    Window& operator=(Window&&)      = delete;

    // -----------------------------------------------------------------------
    //  Frame services
    // -----------------------------------------------------------------------

    /// Processes pending OS events. Call exactly once per frame, at the top.
    ///
    /// WHY THIS MUST BE CALLED EVEN IF NOBODY LISTENS: on Windows this drains
    /// the Win32 message queue. Skip it and the window stops responding to
    /// close, minimise and resize, and the app appears to hang.
    void PumpEvents();

    /// @return True once the user asked to close - the title-bar X, Alt+F4, or
    ///         a call to RequestClose(). The main loop polls this.
    [[nodiscard]] bool ShouldClose() const;

    /// Asks the loop to exit at the end of the current frame.
    ///
    /// @note Sets a flag rather than calling glfwSetWindowShouldClose directly,
    ///       so the request survives even if the platform layer is mid-teardown.
    void RequestClose();

    // -----------------------------------------------------------------------
    //  Geometry
    // -----------------------------------------------------------------------

    /// @return Client-area width in pixels, excluding borders and title bar.
    [[nodiscard]] int GetWidth() const;
    [[nodiscard]] int GetHeight() const;

    /// @return Current client size as a pair. Read both dimensions from ONE
    ///         call: two separate calls can straddle a resize and return a
    ///         width and height that were never true at the same moment.
    [[nodiscard]] std::pair<int, int> GetSize() const;

    /// @return Aspect ratio (width / height). Returns 1.0 when height is 0,
    ///         which happens if the user drags the window to zero height.
    ///         Without the guard the result is an infinity that silently poisons
    ///         every projection matrix built from it.
    [[nodiscard]] float GetAspectRatio() const;

    // -----------------------------------------------------------------------
    //  DPI: WHAT "PIXELS" MEANS HERE
    // -----------------------------------------------------------------------
    //  Every size above is in PHYSICAL pixels, not logical ones.
    //
    //  GLFW 3.3+ calls SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2) on
    //  Win32 before creating any window, so its coordinate space is physical
    //  pixels and a 1280x720 request on a 125%-scaled display is a 1280x720
    //  window. Verified on this machine: glfwGetWindowSize reports 1280x720
    //  while an external DPI-UNAWARE caller sees the same window as 1024x576.
    //  Both numbers are correct; they are different units.
    //
    //  This matters to the renderer, and only the renderer. A D3D12 swap chain's
    //  back buffers must be PHYSICAL pixel dimensions, so GetSize() feeds
    //  DXGI_SWAP_CHAIN_DESC1.BufferDesc.Width directly with no scaling step.
    //  Sizing them in logical pixels instead is a bug that looks correct on a
    //  100% display and produces a window a fifth too small everywhere else.

    // -----------------------------------------------------------------------
    //  Events
    // -----------------------------------------------------------------------

    /// @return The dispatcher driving this window's event delivery. Subscribers
    ///         register, and PumpEvents() then fires their callbacks from the
    ///         GLFW callback machinery running on the main thread. Ownership
    ///         stays with the Window: the returned reference is valid for as
    ///         long as this Window is alive.
    ///
    /// WHY OWNED BY Window NOT BY a global: event delivery is tied to GLFW's
    /// window pointer and pumping pump, which are window-scoped. The callbacks
    /// dispatch INTO dispatcher_, and two windows must not share one handler bag.
    [[nodiscard]] EventDispatcher& GetEventDispatcher() noexcept { return dispatcher_; }
    [[nodiscard]] const EventDispatcher& GetEventDispatcher() const noexcept { return dispatcher_; }

    void SetTitle(std::string_view title);
    [[nodiscard]] std::string GetTitle() const;

    /// @note Ignored if the window is currently hidden or maximised; the window
    ///       manager owns size in those states and the change is silently lost.
    void SetSize(int width, int height);

    // -----------------------------------------------------------------------
    //  Presentation
    // -----------------------------------------------------------------------

    /// Records a vsync request for the renderer's swap chain.
    ///
    /// @note Does NOT call glfwSwapInterval. That function acts on an OpenGL
    ///       context, and this engine's window is created with
    ///       GLFW_NO_API because it renders through D3D12 - the call fails with
    ///       GLFW_ERROR_NO_CURRENT_CONTEXT and, worse, would be the wrong
    ///       control even if it succeeded. Read the result back with
    ///       IsVSyncEnabled() when creating the swap chain.
    void SetVSync(bool enabled);
    [[nodiscard]] bool IsVSyncEnabled() const;

    void SetVisible(bool visible);

    /// @return Seconds since the window was created, from GLFW's own monotonic
    ///         clock. Independent of wall-clock adjustments, unlike
    ///         std::chrono::system_clock.
    [[nodiscard]] double GetTimeSeconds() const;

    /// @return The underlying GLFW handle, for code that must call GLFW directly
    ///         (a tool, or an editor embedding the window in its own host).
    ///         Null only after the destructor has run, which cannot happen on a
    ///         live Window.
    [[nodiscard]] GLFWwindow* GetHandle() const noexcept { return handle_; }

    /// @return The native Win32 HWND for this window, or nullptr if the platform
    ///         has no such concept.
    ///
    /// WHY THIS EXISTS: DXGI presents into an HWND and nothing else. There is no
    /// flip-model swap chain for an abstract window handle, and no way to hand
    /// GLFW's window to D3D12 without unwrapping it - which is exactly what this
    /// does, once, in the one file that talks to both libraries.
    ///
    /// WHY void* AND NOT HWND: Core's rule is that <windows.h> never appears in
    /// an engine header (see Platform.h), and naming the type here would break
    /// it. The alternative - a renderer-specific accessor - would mean a module
    /// dependency to return one pointer. void* keeps the platform boundary in
    /// the signature, so a reader can see that this is an escape hatch rather
    /// than part of the window's own vocabulary, and the renderer casts it once
    /// at the call site.
    ///
    /// @note Ownership stays with this Window. Destroying the Window destroys
    ///       the HWND, and any D3D12Context created from this handle must be
    ///       destroyed first - which is why Application::OnShutdown, not the
    ///       destructor, is the documented place to tear a renderer down.
    [[nodiscard]] void* GetNativeHandle() const noexcept;

private:
    // GLFW callback wire-up. Private: only Window may route events into its own
    // dispatcher, and these are alias-typed with the equivalent GLFW callback
    // signatures, so exposing them would let callers hijack the fan-out midway
    // through a frame.
    static Window* FromGlfwWindow(GLFWwindow* window) noexcept;
    static void OnGlfwKey(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void OnGlfwCursorPosition(GLFWwindow* window, double xpos, double ypos);
    static void OnGlfwMouseButton(GLFWwindow* window, int button, int action, int mods);
    static void OnGlfwWindowSize(GLFWwindow* window, int width, int height);
    static void OnGlfwWindowClose(GLFWwindow* window);

    /// Installs the process-global error callback and asserts the initial
    /// creation actually succeeded.
    void VerifyCreated();

    /// Registers the five GLFW callbacks that fan into the EventDispatcher.
    void InstallEventCallbacks();

    GLFWwindow*    handle_ = nullptr;
    WindowProperties properties_;
    bool            vsyncRequested_ = true;

    /// Owns event delivery for this window. Callbacks written below read the
    /// user pointer and dispatch here; see GetEventDispatcher for why it is a
    /// member rather than a service.
    EventDispatcher dispatcher_;

    /// Previous cursor position used to compute raw deltas for MouseMoveEvent.
    /// hasMousePosition_ gates the first real span so the initial jump from
    /// (0,0) does not arrive as an 800-pixel lurch.
    float lastMouseX_ = 0.0F;
    float lastMouseY_ = 0.0F;
    bool  hasMousePosition_ = false;
};
} // namespace Nova