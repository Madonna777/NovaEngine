// ===========================================================================
//  Application.cpp
// ---------------------------------------------------------------------------
//  The main loop: event pump, delta time, frame pacing, teardown.
// ===========================================================================

#include <Core/Application.h>

#include <Core/Input.h>
#include <Core/Log.h>

#include <algorithm>
#include <chrono>
#include <exception>
#include <thread>

namespace Nova
{
namespace
{
using Clock = std::chrono::steady_clock;

/// Weight of the newest frame in the smoothed FPS average.
///
/// WHY 0.1: at 0.1 the reported value settles in about a second and ignores a
/// single 200ms hitch, but a genuinely halved frame rate is still visible
/// within a few frames. Smaller reacts faster but flickers; larger is stable and
/// slow enough to hide a real regression.
constexpr float kFpsSmoothing = 0.1F;

/// Weight applied to the very first FPS sample. The first frame includes
/// startup work - shader compilation, first-touch allocations - so averaging it
/// in at 0.1 would understate the frame rate for seconds. It is seeded at 1.0,
/// so the first reported value is the sample itself rather than a tenth of it.
constexpr float kFpsSmoothingFirstSample = 1.0F;

/// Smallest interval a frame cap may request, in seconds.
///
/// WHY: targetFramesPerSecond is a float and a user or config file can put 0
/// in it. Dividing by zero yields infinity, and thread::sleep_for(infinity)
/// blocks forever - a hang that looks like a crash but reports nothing. Clamping
/// the divisor turns that into a 1000 FPS cap, which is wrong-looking and
/// harmless.
constexpr float kMinFrameIntervalSeconds = 0.001F;
} // namespace

// ===========================================================================
//  Construction
// ===========================================================================

Application::Application(ApplicationProperties properties)
    : properties_(std::move(properties)), window_(properties_.window)
{
    NOVA_INFO("Application created: '{}'", properties_.window.title);
}

Application::~Application() = default;

// ===========================================================================
//  Run
// ===========================================================================

int Application::Run()
{
    // Two distinct misuse cases, and they need different messages because only
    // one of them is recoverable by the caller:
    //
    //   running_            - a NESTED call, from OnUpdate or an event callback.
    //                          Unrecoverable: two update paths against one
    //                          window, with no obvious origin for the resulting
    //                          state bugs. Refuse it.
    //
    //   hasRunBefore_       - a SEQUENTIAL call, after the first returned. Not
    //                          dangerous but almost certainly a mistake: the
    //                          window still exists, yet shutdownRequested_ is
    //                          sticky, so the loop would spin up, run zero
    //                          frames and return success. Silently doing nothing
    //                          is the worst outcome available, so it is refused
    //                          too - and says so.
    if (running_)
    {
        NOVA_ERROR("Application::Run called while already running - refusing to nest");
        return 1;
    }

    if (hasRunBefore_)
    {
        NOVA_ERROR("Application::Run called a second time - an Application runs once");
        return 1;
    }

    running_      = true;
    hasRunBefore_ = true;

    // steady_clock, NOT system_clock and NOT high_resolution_clock.
    //
    // system_clock is the wall clock: NTP steps it, the user changes it, and a
    // backwards jump produces a negative delta that propagates into every
    // integrator in the frame. high_resolution_clock is only "steady" by
    // accident - on MSVC it aliases steady_clock, but the standard permits it
    // to be system_clock, so code that depends on that is correct on one
    // compiler and broken on the next. steady_clock is guaranteed monotonic.
    start_ = Clock::now();
    Clock::time_point last = start_;

    float fpsAccumulator   = 0.0F;
    int   framesThisSecond = 0;

    NOVA_INFO("Entering main loop");

    try
    {
        // Input before OnStartup, so a subclass can query key state while
        // setting itself up - an editor restoring a layout from saved bindings,
        // a tool reacting to a held modifier at launch.
        Input::Initialize(window_);
        OnStartup();

        while (!shutdownRequested_ && !window_.ShouldClose())
        {
            const Clock::time_point now = Clock::now();
            const float elapsed = std::chrono::duration<float>(now - last).count();
            last = now;

            // Clamp before anything simulates with it. See ApplicationProperties
            // for why an unbounded delta is a correctness problem and not a
            // cosmetic one.
            deltaTime_ = std::min(elapsed, properties_.maxDeltaSeconds);

            // Order is fixed and load-bearing:
            //   1. Pump events  - delivers input state, and sets ShouldClose when
            //                      the user clicks the title-bar X.
            //   2. Update input - latches that state and computes the edges
            //                      against last frame. Must be after (1) or the
            //                      input is one frame stale.
            //   3. OnUpdate    - the application's own work.
            //   4. Present     - no-op until the renderer owns a swap chain, but
            //                      the call site exists now so adding the swap
            //                      chain is a one-line change, not a restructure.
            window_.PumpEvents();
            Input::Update(window_);
            OnUpdate(deltaTime_);
            window_.Present();

            ++frameIndex_;
            ++framesThisSecond;

            // Accumulate the RAW elapsed time, not deltaTime_.
            //
            // WHY THIS IS NOT A TYPO: the clamp exists to protect the
            // simulation, but this accumulator exists to measure how fast frames
            // actually are. Feeding it the clamped value means that once frames
            // exceed the clamp the reported rate is scaled by the clamp - at
            // 300ms frames with a 100ms clamp, a meter built on deltaTime_
            // reports 10 FPS for a process actually running at 3.3. The one
            // situation the FPS line exists to catch is the one it would
            // understate by 3x.
            fpsAccumulator += elapsed;

            if (properties_.fpsLogIntervalSeconds > 0.0F &&
                fpsAccumulator >= properties_.fpsLogIntervalSeconds)
            {
                const float measured = static_cast<float>(framesThisSecond) / fpsAccumulator;
                const float weight = framesPerSecond_ > 0.0F ? kFpsSmoothing
                                                             : kFpsSmoothingFirstSample;
                framesPerSecond_ = framesPerSecond_ * (1.0F - weight) + measured * weight;

                // The clamped delta is reported alongside, so a log line reading
                // "0.10 ms, 3.3 FPS" makes the clamp visible instead of leaving
                // the reader to reconcile two numbers that cannot both be true.
                NOVA_INFO("Frame: {:.2f} ms ({:.1f} FPS, {}x{})", deltaTime_ * 1000.0F,
                          framesPerSecond_, window_.GetWidth(), window_.GetHeight());

                fpsAccumulator = 0.0F;
                framesThisSecond = 0;
            }

            ApplyFrameCap();
        }
    }
    catch (const std::exception& error)
    {
        // Caught here rather than allowed to escape to main: an exception thrown
        // on a frame after the window exists would otherwise unwind past Run(),
        // and the Application destructor would run while the logger was in an
        // unknown state - losing the shutdown messages that explain the failure.
        NOVA_CRITICAL("Exception escaped the main loop: {}", error.what());
        OnShutdownSafely();
        Input::Shutdown();
        running_ = false;
        return 1;
    }
    catch (...)
    {
        // A non-std::exception is not hypothetical here. D3D12 code throws
        // HRESULT by convention, and SEH access violations are not translated
        // at all on MSVC - both arrive here as "something that is not
        // std::exception". Without this arm they escape Run() and skip every
        // line of teardown below, leaving GLFW initialised and the window open.
        NOVA_CRITICAL("Non-standard exception escaped the main loop");
        OnShutdownSafely();
        Input::Shutdown();
        running_ = false;
        return 1;
    }

    NOVA_INFO("Leaving main loop after {:.2f} s",
              std::chrono::duration<float>(Clock::now() - start_).count());

    OnShutdownSafely();
    Input::Shutdown();
    running_ = false;
    return 0;
}

// ===========================================================================
//  Shutdown
// ===========================================================================

void Application::Shutdown()
{
    if (shutdownRequested_)
    {
        // Idempotent by design: Shutdown may be called by the title-bar handler
        // and again by a subsystem that noticed the loop ending.
        return;
    }

    shutdownRequested_ = true;
    NOVA_INFO("Shutdown requested");
}

void Application::OnShutdownSafely()
{
    try
    {
        OnShutdown();
    }
    catch (const std::exception& error)
    {
        // A throwing OnShutdown must not stop the rest of the teardown, or the
        // Input shutdown below is skipped and the next run of the process in a
        // debugger starts with stale key state.
        NOVA_ERROR("OnShutdown threw: {}", error.what());
    }
    catch (...)
    {
        NOVA_ERROR("OnShutdown threw a non-std::exception");
    }
}

// ===========================================================================
//  Frame cap
// ===========================================================================

void Application::ApplyFrameCap()
{
    if (properties_.targetFramesPerSecond <= 0.0F)
    {
        return;
    }

    // Measured from the loop's own start, not from the previous frame's end, so
    // the cap cannot drift later every frame - a per-frame sleep is a cap with
    // no reference point and accumulates its own rounding error without bound.
    const float frameBudget = 1.0F / properties_.targetFramesPerSecond;
    if (frameBudget < kMinFrameIntervalSeconds)
    {
        return;
    }

    const float sinceStart = std::chrono::duration<float>(Clock::now() - start_).count();
    const float target    = static_cast<float>(frameIndex_) * frameBudget;

    if (sinceStart < target)
    {
        std::this_thread::sleep_for(std::chrono::duration<float>(target - sinceStart));
    }
}

} // namespace Nova
