// ===========================================================================
//  main.cpp - Sandbox
// ---------------------------------------------------------------------------
//  Smoke test for the window / input / main-loop / D3D12 layer.
//
//  WHAT THIS PROVES, AND WHAT IT DOES NOT
//  --------------------------------------
//  Running this to completion demonstrates six things a unit test cannot:
//  GLFW initialises in this session, a real HWND is created and shown, the D3D12
//  device comes up on a chosen adapter, a flip-model swap chain presents a frame
//  that survives the fence round trip, input polling reaches the keyboard and
//  mouse, and the whole thing tears down without leaking a window or hanging at
//  exit.
//
//  The window is now BLUE rather than the white this milestone started with. A
//  PrintWindow capture of the previous Sandbox put 255,255,255 at 93% of sampled
//  pixels: with no swap chain nothing is cleared, so the client area showed the
//  Win32 window-class background. The blue is a real clear submitted through a
//  real command list, and it is what proves the whole path works rather than
//  that a brush was set.
//
//  It does NOT demonstrate anything drawn. The clear below is one command
//  recorded by the CPU and executed by the GPU; there is no pipeline state, no
//  vertex buffer, and no shader. A triangle is the next milestone, and the
//  interesting parts of it - root signatures, PSOs, the per-draw path - are not
//  exercised by anything here.
//
//  The demos below are ordered by how much they are worth reading.
// ===========================================================================

#include <Core/Application.h>
#include <Core/Input.h>
#include <Core/KeyCodes.h>
#include <Core/Log.h>
#include <Renderer/D3D12Context.h>
#include <Renderer/Pipeline.h>
#include <Renderer/VertexBuffer.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iterator>

namespace
{
/// The application's own behaviour. Window, loop, timing and teardown are all
/// inherited, which is the entire reason Application exists: a concrete
/// executable becomes two methods instead of a copy of the loop.
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
        NOVA_CLIENT_INFO("Window: {}x{} vsync={}", GetWindow().GetWidth(), GetWindow().GetHeight(),
                         GetWindow().IsVSyncEnabled());

        // ---------------------------------------------------------------------
        //  Device creation.
        //
        //  HERE, NOT IN THE CONSTRUCTOR, and not in OnRenderFrame either. Two
        //  reasons, both about ordering:
        //
        //    - The HWND must already exist. DXGI's CreateSwapChainForHwnd
        //      binds to a live window, and creating a device first and the
        //      window second would mean tearing the swap chain down and building
        //      it again. Application's contract puts OnStartup after window
        //      creation for exactly this.
        //
        //    - It must be BEFORE the first frame, not inside it. Device
        //      creation is the slowest thing the engine does that is not a
        //      shader compile, and doing it lazily turns a startup cost into a
        //      one-frame hitch that is very hard to attribute later.
        //
        //  The sizes come from the window, in PHYSICAL pixels. GetSize() is
        //  physical because GLFW sets PER_MONITOR_AWARE_V2 - see the DPI note in
        //  Core/Window.h. On this machine, which runs at 125% scaling, a logical
        //  pixel count here would produce a back buffer 1280x576 for a 1280x720
        //  window: stretched, and one fifth short on both axes.
        const auto [width, height] = GetWindow().GetSize();

        context_ = std::make_unique<Nova::Renderer::D3D12Context>(
            static_cast<HWND>(GetWindow().GetNativeHandle()),
            static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
            GetWindow().IsVSyncEnabled());

        NOVA_CLIENT_INFO("D3D12 initialized successfully");
        NOVA_CLIENT_INFO("  adapter: {}", context_->GetAdapterName());
        NOVA_CLIENT_INFO("  back buffer: {}x{} in {} slots, {} MB dedicated VRAM",
                         context_->GetBackBufferWidth(), context_->GetBackBufferHeight(),
                         Nova::Renderer::D3D12Context::kSwapBufferCount,
                         context_->GetDedicatedVideoMemoryBytes() / (1024ULL * 1024ULL));

        CreateTriangleResources();

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
            NOVA_CLIENT_INFO("Mouse at ({:.0f}, {:.0f}) - client-relative, +Y is down", mouseX,
                             mouseY);
            lastMouseX_ = mouseX;
            lastMouseY_ = mouseY;
        }

        // ---------------------------------------------------------------------
        //  Demo 4: the argument and the accessor are the same value.
        //
        //  OnUpdate's parameter and GetDeltaTime() must never disagree - an
        //  engine service reached from a subclass reads the accessor, and if the
        //  two were computed from different clocks the simulation would step by
        //  one value while the renderer animated by another. The branch below is
        //  unreachable by construction; the check is here so a future refactor
        //  that breaks the invariant fails loudly in the Sandbox rather than
        //  subtly in a physics bug.
        //
        //  This matters MORE now that a renderer exists. The frame rate is
        //  measured against real GPU work, so a delta time that disagreed with
        //  the one the simulation used would show up as animation running at the
        //  wrong speed rather than as a number that looks wrong.
        // ---------------------------------------------------------------------
        if (deltaTime != GetDeltaTime())
        {
            NOVA_CLIENT_ERROR("Delta time mismatch: argument {} vs accessor {}", deltaTime,
                              GetDeltaTime());
        }

        ++frames_;
    }

    void OnRenderFrame() override
    {
        if (!context_)
        {
            // Only reachable if device creation threw out of OnStartup, and
            // Application catches that, shuts down, and never enters the loop.
            // Checked anyway: a null dereference in the frame loop is a much
            // worse diagnostic than a one-line log.
            NOVA_CLIENT_ERROR("Render frame with no D3D12 context");
            return;
        }

        // ---------------------------------------------------------------------
        //  The whole D3D12 frame, in the order D3D12 requires.
        //
        //    BeginFrame   - waits for this frame's allocator to be free, resizes
        //                   if the window changed, acquires a back buffer, and
        //                   transitions it to render-target state.
        //    Clear        - paints the background into the acquired buffer.
        //    Viewport     - recorded command scoping pixel space to the swap
        //                   chain size; without it every draw lands in a 0-sized
        //                   box, which is why so many first-triangles are black.
        //    Scissor      - stronger clip than the viewport for pixels that must
        //                   never escape; paired with it here.
        //    Pipeline     - PSO and root signature are command-list state just
        //                   like the viewport; set before the vertex buffer and
        //                   draw because the draw consumes them as one package.
        //    VertexBuffer - bound on slot 0 of the IA stage.
        //    DrawInstanced(3, 1, 0, 0) - three vertices as one TRIANGLELIST.
        //    EndFrame     - closes the list, transitions back to PRESENT state,
        //                   submits, presents, and signals the fence so the next
        //                   BeginFrame knows when this allocator may be reused.
        //
        //  THE ORDER IS NOT A STYLE CHOICE. Resize after BeginFrame and the
        //  buffer is invalidated after it was acquired. Clear after EndFrame and
        //  the command list is closed, so the clear goes nowhere. And with one
        //  frame in flight instead of two, the CPU would wait for the GPU before
        //  starting the next frame and the frame rate would be CPU time plus
        //  GPU time with no overlap - which is why kFramesInFlight is 2.
        //
        //  The vertices are authored so that each output pixel's colour is the
        //  barycentric mix of red, green and blue - the interpolation is the
        //  rasteriser's job, this shader is only the endpoint.
        // ---------------------------------------------------------------------
        context_->BeginFrame();
        context_->ClearRenderTarget(0.08F, 0.08F, 0.10F, 1.0F);
        context_->SetViewport(static_cast<float>(context_->GetBackBufferWidth()),
                              static_cast<float>(context_->GetBackBufferHeight()));
        context_->SetScissorRect();

        auto* commandList = context_->GetCommandList();
        pipeline_->Bind(commandList);

        D3D12_VERTEX_BUFFER_VIEW vertexBufferView = vertexBuffer_->GetVertexBufferView();
        commandList->IASetVertexBuffers(0, 1, &vertexBufferView);

        commandList->DrawInstanced(3, 1, 0, 0);

        context_->EndFrame();
    }

    void OnShutdown() override
    {
        // Runs while the window AND the logger are both still alive. After the
        // window is destroyed this would be a use-after-free of the HWND - and
        // D3D12Context reads it every frame to check for a resize; after
        // Log::Shutdown it would be a null logger. Application guarantees this
        // ordering - see OnShutdown in Application.h.
        //
        // The device is released HERE rather than left to the destructor, and
        // that is not an optimisation. Application::OnShutdown runs while the
        // window still exists, so a context destroyed here still has a valid
        // HWND; one destroyed later, as a member, would have to survive past a
        // point where nothing guarantees that.
        if (context_)
        {
            NOVA_CLIENT_INFO("Destroying the D3D12 context after {} frames", frames_);
            context_.reset();
        }

        NOVA_CLIENT_INFO("Sandbox shut down cleanly after {} frames", frames_);
    }

private:
    /// Compiles the shaders into the PSO and places the triangle in an upload
    /// heap buffer.
    ///
    /// @note Both are member-shaped rather than stack locals inside OnStartup:
    ///       the PSO and the buffer are needed on every frame, and the set
    ///       keeps the whole graphics side of the Sandbox declared in one
    ///       place.
    void CreateTriangleResources()
    {
        // ---- the vertex input layout ---------------------------------------
        //
        // The contract between the vertex buffer, the input assembler stage,
        // and the vertex shader. Every D3D12_INPUT_ELEMENT_DESC names one
        // attribute of one vertex row and where the assembler should read it
        // from: which semantic index the shader declares, whether it is a
        // PER_VERTEX_DATA or PER_INSTANCE_DATA field, its offset in the row,
        // and the row's stride. The order has to match Vertex field order -
        // this is the array the compiler validates against the HLSL Input
        // struct, and a layout whose POSITION offset disagrees with the HLSL
        // struct simply produces a triangle at nonsense coordinates.
        static const D3D12_INPUT_ELEMENT_DESC kInputLayout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,
              static_cast<UINT>(offsetof(Nova::Renderer::Vertex, position)),
              D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
              static_cast<UINT>(offsetof(Nova::Renderer::Vertex, color)),
              D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        pipeline_ = std::make_unique<Nova::Renderer::D3D12Pipeline>();
        pipeline_->Create(context_->GetDevice(), L"Shaders/triangle_vs.hlsl",
                          L"Shaders/triangle_ps.hlsl", kInputLayout,
                          static_cast<std::uint32_t>(std::size(kInputLayout)));

        // The triangle's three vertices, with one attribute the triangle can
        // not miss - the colour. Un-transformed clip-space positions placing
        // the triangle at the centre of the screen: a red apex up, green
        // bottom-right, blue bottom-left. Each interior pixel's colour is the
        // interpolation of those three corners, so the gradient is the
        // rasteriser's linear mix, not something the vertex shader varies per
        // frame.
        const std::vector<Nova::Renderer::Vertex> triangle = {
            { {  0.0F,  0.5F, 0.0F }, { 1.0F, 0.0F, 0.0F, 1.0F } }, // top:    red
            { {  0.5F, -0.5F, 0.0F }, { 0.0F, 1.0F, 0.0F, 1.0F } }, // right:  green
            { { -0.5F, -0.5F, 0.0F }, { 0.0F, 0.0F, 1.0F, 1.0F } }, // left:   blue
        };

        vertexBuffer_ = std::make_unique<Nova::Renderer::D3D12VertexBuffer>(context_->GetDevice(),
                                                                             triangle);
    }

private:
    /// Held by unique_ptr, not by value, and the distinction matters.
    ///
    /// A D3D12Context MEMBER would be destroyed after ~SandboxApp runs - and
    /// Application's destructor is default, so the base class's Window would be
    /// destroyed first, in between. OnShutdown above is what releases the
    /// device while the window is guaranteed alive; a member makes that ordering
    /// a property of the C++ object model instead of a property of a comment.
    ///
    /// unique_ptr and not optional: the context has a constructor that can throw
    /// and no default constructor, so it must be constructed at OnStartup time
    /// and it must be movable into place. That is exactly the contract
    /// unique_ptr was invented for.
    std::unique_ptr<Nova::Renderer::D3D12Context> context_;

    /// Shader pipeline: compiles the HLSL pair and owns the PSO it produces.
    std::unique_ptr<Nova::Renderer::D3D12Pipeline> pipeline_;

    /// The three vertices of the first triangle, in an upload-heap buffer.
    std::unique_ptr<Nova::Renderer::D3D12VertexBuffer> vertexBuffer_;

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
        // fail during startup - window creation, device creation - happens inside
        // the Application constructor or its OnStartup, so the try wraps both it
        // and Run().
        SandboxApp app;
        exitCode = app.Run();
    }
    catch (const std::exception& error)
    {
        // Almost always "GLFW could not be initialised", which on Windows means
        // no desktop session is attached - a service, a CI agent, or a shell
        // over SSH. Or a D3D12 HRESULT from device creation, which names the
        // failing call. Caught rather than allowed to escape so Log::Shutdown
        // below still runs; an escaping exception would skip it and lose the
        // reason.
        NOVA_CRITICAL("Sandbox failed to start: {}", error.what());
        exitCode = 1;
    }

    // Explicit rather than relying on the automatic shutdown at process exit:
    // static destruction order is not something to rely on for flushing a log
    // you are about to want to read.
    Nova::Log::Shutdown();
    return exitCode;
}