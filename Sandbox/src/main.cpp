// ===========================================================================
//  main.cpp - Sandbox
// ---------------------------------------------------------------------------
//  Smoke test for the window / input / main-loop layer.
//
//  WHAT THIS PROVES, AND WHAT IT DOES NOT
//  --------------------------------------
//  Running this to completion demonstrates five things a unit test cannot:
//  GLFW initialises in this session, a real HWND is created and shown, the
//  frame loop runs at a sane rate, input polling reaches the keyboard and
//  mouse, and the whole thing tears down without leaking a window or hanging
//  at exit.
//
//  It does NOT demonstrate rendering. Nothing is drawn: there is no swap chain,
//  so the client area shows whatever the window manager paints there - and a
//  PrintWindow capture of a running Sandbox put 255,255,255 at 93% of sampled
//  pixels, so that is white, not the black this was originally specified to
//  show. It is the renderer's job, and faking it with a Win32 black class brush
//  would be a hack to delete in a day. The D3D12 swap chain makes it moot.
//
//  The demos below are ordered by how much they are worth reading.
// ===========================================================================

#include <Core/Application.h>
#include <Core/Input.h>
#include <Core/KeyCodes.h>
#include <Core/Log.h>

#include <cmath>
#include <exception>

namespace
{
/// The application's own behaviour. Window, loop, timing and teardown are all
/// inherited, which is the entire reason Application exists: a concrete
/// executable becomes one method instead of a copy of the loop.
///
/// @note An automatic object on main()'s stack, not a static. The base class
///       holds a Window, and a static would construct it during static
///       initialisation - before the log system it logs into has been created,
///       and with a start order nobody wrote down.
class SandboxApp final : public Nova::Application
{
public:
    SandboxApp() = default;

protected:
    void OnStartup() override
    {
        NOVA_CLIENT_INFO("Sandbox startup");
        NOVA_CLIENT_INFO("Window: {}x{} vsync={}", GetWindow().GetWidth(),
                         GetWindow().GetHeight(), GetWindow().IsVSyncEnabled());
        NOVA_CLIENT_INFO("Press Escape to close");
    }

    void OnUpdate(float deltaTime) override
    {
        // ---------------------------------------------------------------------
        //  Demo 1: close on Escape.
        //
        //  IsKeyPressed reports the EDGE - the one frame the key went down - not
        //  the level. IsKeyDown here would call Shutdown() on every frame while
        //  Escape is held. That is harmless only because Shutdown() happens to
        //  be idempotent, which is exactly the kind of accident that turns into
        //  a bug the moment the exit path grows a side effect.
        //
        //  This is also the correct pattern for ANY key that closes a mode or a
        //  menu: an edge query, so one press does one thing.
        // ---------------------------------------------------------------------
        if (Nova::Input::IsKeyPressed(Nova::Key::Code::Escape))
        {
            NOVA_CLIENT_INFO("Escape pressed - requesting shutdown");
            Shutdown();
        }

        // ---------------------------------------------------------------------
        //  Demo 2: keys held, read as a LEVEL.
        //
        //  Gameplay asks "am I moving right" and the answer must be true on
        //  every frame the key is down, not only the first - so gameplay code
        //  calls IsKeyDown. Here the log line is emitted on the press edge
        //  instead, purely so the console shows one line per press rather than
        //  sixty per second.
        // ---------------------------------------------------------------------
        if (Nova::Input::IsKeyPressed(Nova::Key::Code::W))
        {
            NOVA_CLIENT_INFO("W is down ({})", Nova::Key::Name(Nova::Key::Code::W));
        }
        if (Nova::Input::IsKeyPressed(Nova::Key::Code::Space))
        {
            NOVA_CLIENT_INFO("Space is down ({})", Nova::Key::Name(Nova::Key::Code::Space));
        }

        // ---------------------------------------------------------------------
        //  Demo 3: the mouse, polled live.
        //
        //  Logged only when it actually moves, for the same reason as above.
        //  Two things worth noticing: the position is CLIENT-relative, and +Y
        //  points DOWN. D3D12's normalised device coordinate Y points UP, so
        //  every projection built from this has to flip it - Math will own that
        //  and this comment is the reminder of why.
        // ---------------------------------------------------------------------
        const auto [mouseX, mouseY] = Nova::Input::GetMousePosition();
        if (std::fabs(mouseX - lastMouseX_) > 1.0F || std::fabs(mouseY - lastMouseY_) > 1.0F)
        {
            NOVA_CLIENT_INFO("Mouse at ({:.0f}, {:.0f}) - client-relative, +Y is down",
                             mouseX, mouseY);
            lastMouseX_ = mouseX;
            lastMouseY_ = mouseY;
        }

        // ---------------------------------------------------------------------
        //  Demo 4: the argument and the accessor are the same value.
        //
        //  OnUpdate's parameter and GetDeltaTime() must never disagree - an
        //  engine service reached from a subclass reads the accessor, and if
        //  the two were computed from different clocks the simulation would
        //  step by one value while the renderer animated by another. The
        //  branch below is unreachable by construction; the check is here so a
        //  future refactor that breaks the invariant fails loudly in the
        //  Sandbox rather than subtly in a physics bug.
        // ---------------------------------------------------------------------
        if (deltaTime != GetDeltaTime())
        {
            NOVA_CLIENT_ERROR("Delta time mismatch: argument {} vs accessor {}",
                              deltaTime, GetDeltaTime());
        }

        ++frames_;
    }

    void OnShutdown() override
    {
        // Runs while the window AND the logger are both still alive. After the
        // window is destroyed this would be a use-after-free of the HWND; after
        // Log::Shutdown it would be a null logger. Application guarantees this
        // ordering - see OnShutdown in Application.h.
        NOVA_CLIENT_INFO("Sandbox shut down cleanly after {} frames", frames_);
    }

private:
    int   frames_     = 0;
    float lastMouseX_ = 0.0F;
    float lastMouseY_ = 0.0F;
};
} // namespace

int main()
{
    // Trace on both channels. Application logs GLFW's version string, the window
    // creation line and the loop entry/exit; the timing of those is exactly what
    // you want when a start is slow. A shipping client would use Info.
    Nova::Log::Initialize(Nova::LogLevel::Trace);

    int exitCode = 0;

    try
    {
        // Two statements, and that is the whole executable. Everything that can
        // fail during startup - window creation - is inside the Application
        // constructor, so the try wraps both it and Run().
        SandboxApp app;
        exitCode = app.Run();
    }
    catch (const std::exception& error)
    {
        // Almost always "GLFW could not be initialised", which on Windows means
        // no desktop session is attached - a service, a CI agent, or a shell
        // over SSH. Caught rather than allowed to escape so Log::Shutdown below
        // still runs; an escaping exception would skip it and lose the reason.
        NOVA_CRITICAL("Sandbox failed to start: {}", error.what());
        exitCode = 1;
    }

    // Explicit rather than relying on the automatic shutdown at process exit:
    // static destruction order is not something to rely on for flushing a log
    // you are about to want to read.
    Nova::Log::Shutdown();
    return exitCode;
}
