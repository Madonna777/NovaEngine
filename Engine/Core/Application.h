// ===========================================================================
//  Application.h
// ---------------------------------------------------------------------------
//  The engine's main loop and its owner.
//
//  WHY AN ABSTRACT BASE AND NOT A free-function loop
//  -------------------------------------------------
//  main() has no place to put per-frame logic that a test, the editor, or a
//  tool could not also run. A class with a virtual OnUpdate gives every
//  executable the same loop - window creation, event pumping, delta time, FPS
//  accounting, teardown - and the only thing each executable writes is the one
//  method that is actually its own.
//
//  WHY SINGLE-INSTANCE AND NOT A REFERENCE-COUNTED SINGLETON
//  ---------------------------------------------------------
//  An engine process runs one application: a second one has no window to draw
//  into and no main thread to pump it. So this is single-instance by
//  construction, not by design pattern:
//    - The Window is a MEMBER, not a global. Constructed, destroyed and
//      sequenced like any other object, so its lifetime is provable.
//    - Run() is a normal member. No Get() accessors, no initialisation order to
//      reason about, no null checks at 400 call sites.
//    - Single-instance is enforced by the fact that two Applications would want
//      two GLFW main loops on one thread, which GLFW documents as unsupported.
//  What is left is one guard inside Run() so a second Run() on the same object
//  is a loud error instead of a nested loop.
//
//  WHY Run() RETURNS int
//  ---------------------
//  Run() creates a window, and window creation can fail. With a void Run() the
//  only way to report that is to log and return, which makes the caller unable
//  to tell "exited cleanly" from "never started". Returning the process exit
//  code lets main() do the one thing it is uniquely good at, and keeps
//  Application from calling exit() from inside a library.
// ===========================================================================
#pragma once

#include <Core/Window.h>

#include <chrono>
#include <cstdint>

namespace Nova
{
/// How the application should behave before its first frame.
struct ApplicationProperties
{
    /// Passed straight to the Window. Defaults match WindowProperties so a
    /// caller who does not care about the window writes nothing.
    WindowProperties window;

    /// Longest delta time the loop will report, in seconds.
    ///
    /// WHY THIS EXISTS: the time between two frames is unbounded. Drag the
    /// window across a 4K monitor, minimise the app, or stop at a breakpoint and
    /// the next frame reports a delta of thirty seconds. Anything integrating
    /// that - velocity, a physics step, an animation clock - either explodes or
    /// fast-forwards, and the cause is a debugging artefact that never
    /// reproduces on a machine without the stall. Clamping turns a catastrophic
    /// frame into a large-but-survivable one.
    ///
    /// 0.1s is ten frames at 100Hz and six at 60Hz: slow enough that a single
    /// hitch is smoothed rather than dropped, fast enough that it is visibly a
    /// hitch and not a second-long freeze.
    float maxDeltaSeconds = 0.1F;

    /// Cap on frames per second. 0 disables the cap.
    ///
    /// WHY THE DEFAULT IS OFF: vsync already paces the frame rate against the
    /// display, so a software cap on top of it does nothing except break a
    /// clean 144 Hz monitor down to 60. Set it for a fixed-timestep simulation
    /// or a headless benchmark, not for the default windowed run.
    float targetFramesPerSecond = 0.0F;

    /// Seconds between automatic FPS log lines. 0 disables the logging.
    ///
    /// WHY LOG FPS AT ALL: it is the cheapest possible renderer health check.
    /// A CPU-side update that costs 40ms a frame is invisible until something is
    /// on screen moving, which is exactly when you want to know.
    float fpsLogIntervalSeconds = 1.0F;
};

/// Base class for an engine executable. Subclass, implement OnUpdate, call Run.
class Application
{
public:
    /// @throws std::runtime_error if the window cannot be created. See
    ///         Window's constructor - failure is fatal because there is nothing
    ///         to render into.
    explicit Application(ApplicationProperties properties = {});
    virtual ~Application();

    Application(const Application&)            = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&)                 = delete;
    Application& operator=(Application&&)      = delete;

    /// Runs the main loop until the window is closed or Shutdown() is called,
    /// then tears everything down.
    ///
    /// @return 0 on a normal exit, 1 if the loop was entered a second time, a
    ///         previous frame threw, or a frame threw something that is not a
    ///         std::exception. Never calls exit().
    ///
    /// @note Runs exactly once per Application. A nested call (from OnUpdate or
    ///       an event callback) and a second sequential call are both refused
    ///       with a logged error and a return of 1 - the second would
    ///       otherwise start a loop, run zero frames because shutdown is
    ///       sticky, and report success.
    int Run();

    /// Asks the loop to finish after the current frame. Safe to call from
    /// OnUpdate, from a callback, or from another thread.
    ///
    /// @note Idempotent, and does not block. Cleanup happens in Run() so it
    ///       happens exactly once, in one place, rather than at every call site.
    void Shutdown();

    /// @return The application's window. Invalid after Run() returns.
    [[nodiscard]] Window& GetWindow() noexcept { return window_; }
    [[nodiscard]] const Window& GetWindow() const noexcept { return window_; }

    /// @return Seconds between the previous frame and this one, already clamped
    ///         to maxDeltaSeconds.
    ///
    /// @note Also passed to OnUpdate. Provided on the object so engine services
    ///       reached from a subclass do not have to be handed the value.
    [[nodiscard]] float GetDeltaTime() const noexcept { return deltaTime_; }

    /// @return Smoothed frames per second, or 0 before the first full second has
    ///         elapsed. Smoothed rather than instantaneous because a per-frame
    ///         value is dominated by one frame's jitter and reads as noise.
    [[nodiscard]] float GetFramesPerSecond() const noexcept { return framesPerSecond_; }

    [[nodiscard]] bool IsRunning() const noexcept { return running_; }

protected:
    /// Called once per frame with the clamped delta time, in seconds.
    ///
    /// @note Pure virtual: an Application with nothing to update is a mistake,
    ///       and the Sandbox is the cheapest possible example of one.
    virtual void OnUpdate(float deltaTime) = 0;

    /// Called once per frame, after OnUpdate, to draw and present the frame.
    ///
    /// @note Empty by default, and that default is load-bearing. Core does not
    ///       know whether a program renders, has a headless mode, or draws
    ///       through a completely different backend, and a hook it must
    ///       implement would force an answer. A subclass with no renderer
    ///       inherits the no-op and never mentions it again.
    ///
    /// WHY THE LOOP CALLS A HOOK INSTEAD OF OWNING A RENDERER
    /// -------------------------------------------------------
    /// The obvious implementation is for this class to hold a D3D12Context,
    /// create it in OnStartup and call BeginFrame / ClearRenderTarget / EndFrame
    /// itself. That is also a module dependency from Core to Renderer, and
    /// Engine/Renderer/CMakeLists.txt explains why the cycle it creates is not
    /// hypothetical. The hook produces the identical frame order - begin, clear,
    /// end, every frame - with the dependency running one way instead of two,
    /// and the subclass that actually renders is the one that already has to
    /// link Renderer.
    ///
    /// @note KNOWN SEAM, DELIBERATE: this is ONE hook, not a Begin/End pair, so
    ///       the command list is closed by the same call that opened it. When
    ///       gameplay needs to record into the command list from OnUpdate - and
    ///       it will, the first time anything is drawn - this splits into
    ///       OnBeginFrame() and OnEndFrame() around OnUpdate. That is a
    ///       mechanical change to this header, this loop and one Sandbox
    ///       override. Doing it now would mean every application writes an empty
    ///       override pair for a renderer feature that does not exist yet.
    virtual void OnRenderFrame() {}

    /// Called once after the window exists and before the first OnUpdate.
    ///
    /// @note The place to create GPU resources. Doing it here rather than in
    ///       the constructor means the window and its HWND already exist, which
    ///       is what the D3D12 swap chain needs.
    virtual void OnStartup() {}

    /// Called once after the final OnUpdate and before the window is destroyed.
    ///
    /// @note The place to destroy GPU resources, while the device and swap
    ///       chain are both still alive. The reverse order - freeing a buffer
    ///       after its device is gone - is a use-after-free that D3D12 debug
    ///       layers report as an unhelpful access violation.
    virtual void OnShutdown() {}

    ApplicationProperties properties_;

private:
    /// Invokes the virtual OnShutdown inside a try/catch so a throwing subclass
    /// cannot abort the rest of Run()'s teardown. Declared here because it is
    /// an implementation detail of Run(), not part of what a subclass may use.
    void OnShutdownSafely();

    /// Sleeps the remainder of the frame when targetFramesPerSecond is set.
    /// A no-op otherwise, which is the default - see
    /// ApplicationProperties::targetFramesPerSecond.
    void ApplyFrameCap();

    Window window_;

    bool  running_           = false;
    bool  hasRunBefore_      = false;
    bool  shutdownRequested_ = false;

    /// Start of the current Run(), and the frame counter used to compute how far
    /// into the frame budget we are. Held as members rather than locals because
    /// ApplyFrameCap() needs both and is a separate function.
    std::chrono::steady_clock::time_point start_{};
    std::uint64_t frameIndex_ = 0;

    float deltaTime_      = 0.0F;
    float framesPerSecond_ = 0.0F;
};
} // namespace Nova
