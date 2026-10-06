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

#include "FrameConstants.h"
#include "SceneObjects.h"

#include <Core/Application.h>
#include <Core/Input.h>
#include <Core/KeyCodes.h>
#include <Core/Log.h>
#include <Math/Math.h>
#include <Renderer/D3D12Context.h>
#include <Renderer/Material.h>
#include <Renderer/Pipeline.h>
#include <Renderer/Mesh.h>
#include <Renderer/Shader.h>
#include <Scene/Camera.h>
#include <Scene/FPSCameraController.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iterator>
#include <vector>

namespace
{
/// Path the demo scene tries to load a model from before falling back to a
/// procedural cube.
///
/// @note Relative to the working directory, which is the executable's folder
///       under F5 and the repository root from a command line. The asset
///       milestone replaces this with a manifest lookup; until then a constant
///       is better than an empty string, because an empty path would make
///       Assimp's error message useless.
constexpr const char* kModelPath = "Assets/cube.obj";

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

        CreateLitResources();

        // ---- camera + FPS controller --------------------------------------
        //
        // Pulled back and slightly above the cube, looking at it.
        //
        // z = -4 rather than -3 because the cube now has depth: at 3 units a
        // unit cube fills most of the frame, and orbiting it would push its own
        // faces off screen before the camera finished a quarter turn. The
        // y = 2.5 with a downward pitch is the same idea - without some elevation
        // the ground plane is edge-on and invisible, so the scene has no floor
        // to read the cube's height against.
        camera_.position = { 0.0F, 2.5F, -4.0F };
        camera_.yaw      = 0.0F;

        // Pitch is positive looking UP, so aiming down at the cube needs a
        // negative value. This is the sign that is wrong in every engine's first
        // hour, which is why it is written down here rather than left as a
        // number to discover by orbiting the wrong way.
        camera_.pitch    = Nova::Math::Radians(-25.0F);

        {
            const auto [w, h] = GetWindow().GetSize();
            camera_.UpdateAspectRatio(static_cast<float>(w), static_cast<float>(h));
        }

        controller_ = std::make_unique<Nova::Scene::FPSCameraController>(camera_, GetWindow());
        controller_->SetActive(true);

        // Camera projection assumes the window's aspect and the window resizes;
        // re-reading it inside the resize event means every HandleResize marks
        // the next frame's GetViewProjectionMatrix honest.
        resizeToken_ = GetWindow().GetEventDispatcher().Subscribe(Nova::EventType::WindowResize,
            [this](const Nova::Event& event)
            {
                const auto& size = event.As<Nova::WindowResizeEvent>();
                camera_.UpdateAspectRatio(static_cast<float>(size.width),
                                          static_cast<float>(size.height));
            });

        NOVA_CLIENT_INFO("Press Escape to close, WASD to move, mouse to look");
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
        //  Demo 2: camera controller driven by the frame delta.
        //
        //  Runs AFTER the Escape check to preserve the usual kill-switch-first
        //  ordering of a typical dep runtime loop. Update reads the mouse delta
        //  pool the MouseMoved events fed it since the last frame, applies
        //  sensitivity, and integrates WASD motion in seconds. So mouse look is
        //  even-rate regardless of how slow the main loop is, and movement
        //  speed is always in world units per second.
        // ---------------------------------------------------------------------
        if (controller_)
        {
            controller_->Update(deltaTime);
        }

        // Periodic console heartbeat so a headless dev can tell the camera is
        // hearing live input, rather than rendering the last accumulated pose.
        cameraLogAccumulator_ += deltaTime;
        if (cameraLogAccumulator_ >= 2.0F)
        {
            cameraLogAccumulator_ = 0.0F;
            NOVA_CLIENT_INFO(
                "Camera: pos=({:.2f}, {:.2f}, {:.2f}) yaw={:.1f}deg pitch={:.1f}deg",
                camera_.position.x, camera_.position.y, camera_.position.z,
                Nova::Math::Degrees(camera_.yaw), Nova::Math::Degrees(camera_.pitch));
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
        if (!context_ || !pipeline_)
        {
            // Only reachable if startup threw part-way through; Application
            // catches that and never enters the loop. Checked anyway: a null
            // dereference in the frame loop is a much worse diagnostic than a
            // one-line log.
            NOVA_CLIENT_ERROR("Render frame with incomplete GPU resources");
            return;
        }

        // ---------------------------------------------------------------------
        //  The whole D3D12 frame, in the order D3D12 requires.
        //
        //    BeginFrame   - waits for this frame's allocator, resizes if the
        //                   window changed, acquires a back buffer, and
        //                   transitions it to render-target state.
        //    Clear        - paints the background into the acquired buffer.
        //    Viewport     - pixel space for the projection; a 0-sized viewport
        //                   is why so many first-triangles render black.
        //    Scissor      - the clip that must not be escaped.
        //    Root sig + PSO - command-list state, like the viewport; bound
        //                   before the draw that consumes it.
        //    Constant CBs - camera every frame, object every frame, light once.
        //    Vertex buffer + DrawInstanced.
        //    EndFrame     - closes, transitions back to PRESENT, submits,
        //                   presents, signals the fence.
        //
        //  THE ORDER IS NOT A STYLE CHOICE. Resize after BeginFrame and the
        //  buffer is invalidated after acquisition; clear after EndFrame and the
        //  list is already closed; and with one frame in flight the CPU would
        //  wait for the GPU every frame, which is why kFramesInFlight is 2.
        // ---------------------------------------------------------------------
        context_->BeginFrame();
        context_->ClearRenderTarget(0.02F, 0.02F, 0.03F, 1.0F);
        context_->SetViewport(static_cast<float>(context_->GetBackBufferWidth()),
                              static_cast<float>(context_->GetBackBufferHeight()));
        context_->SetScissorRect();

        auto* commandList = context_->GetCommandList();

        // Per-frame slot for the constant buffers. Separate from the renderer's
        // allocator slot on purpose: the two count the same thing (frames in
        // flight) but they are not required to advance together, and coupling
        // them would make every constant write depend on swap-chain rotation
        // order rather than on the frame count.
        frameSlot_ = (frameSlot_ + 1) % Nova::Renderer::D3D12Context::kFramesInFlight;

        // Camera and light. The object's own matrix is NOT written here: one b1
        // slot holds one matrix and this frame has two objects, so it is per-draw
        // state and gets written inside Draw, immediately before each draw.
        Nova::Sandbox::UpdateFrameConstants(material_, camera_, frameSlot_);

        // Positions the spinning object. The elapsed time comes from the render
        // loop's own clock rather than a delta accumulated here, so the animation
        // and the camera cannot disagree about what time it is.
        scene_.Update(material_, frameSlot_, GetElapsedTime());

        // The root signature is bound explicitly even though Pipeline::Bind also
        // sets it: this is the call that says "these root parameters are the
        // ones the constants above were written for", and having it next to the
        // writes is what makes a register mismatch readable.
        commandList->SetGraphicsRootSignature(context_->GetRootSignature());
        pipeline_->Bind(commandList);

        material_.Bind(commandList, frameSlot_);

        // Both objects in one call. Each writes b1 for itself immediately
        // before its own draw, which is the arrangement a single object
        // constant forces.
        scene_.Draw(commandList, material_, frameSlot_);

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

        // Releases the FPS controller before the window itself goes. Its
        // destructor unsubscribes from the dispatcher, and resetting the
        // cursor mode happens here so the OS cursor is not trapped in Disabled
        // after the process drops the window.
        if (controller_)
        {
            controller_->SetActive(false);
            controller_.reset();
        }

        if (resizeToken_ != 0)
        {
            GetWindow().GetEventDispatcher().Unsubscribe(Nova::EventType::WindowResize, resizeToken_);
            resizeToken_ = 0;
        }

        NOVA_CLIENT_INFO("Sandbox shut down cleanly after {} frames", frames_);
    }

private:
    /// Compiles the lit shader pair, reflects it, and prepares the per-frame
    /// constant buffers and the triangle's vertex data.
    ///
    /// @note Every artefact is a member rather than a stack local in OnStartup:
    ///       all of them are needed on every frame, and the set keeps the whole
    ///       graphics side of the Sandbox declared in one place.
    void CreateLitResources()
    {
        material_.SetDevice(context_->GetDevice());
        // Two slots, matching the renderer's frames in flight: the slot being
        // written this frame is not the one the GPU is reading.
        material_.SetFrameCount(Nova::Renderer::D3D12Context::kFramesInFlight);

        // ---- compile and reflect -------------------------------------------
        //
        // CompileFromFile compiles BOTH stages and reflects them in one call,
        // so the pipeline below builds its input layout out of the shader's own
        // reflection. That is the point of reflecting at all: the layout the
        // pipeline declares and the layout the shader was written for cannot
        // disagree, because there is only one of them.
        material_.SetShader(Nova::Renderer::Shader::CompileFromFile(L"Shaders/lit_vs.hlsl",
                                                                    L"Shaders/lit_ps.hlsl"));

        const auto& shader = material_.GetShader();

        // Logged because the next question is always "which registers does this
        // shader actually read", and here the answer comes from the engine's own
        // reflection rather than from re-reading the HLSL.
        for (const auto& element : shader.GetInputLayoutElements())
        {
            NOVA_CLIENT_INFO("  input {} ({} components) at offset {}",
                             element.semanticName, element.componentCount, element.byteOffset);
        }
        for (const auto& constant : shader.GetConstantBuffers())
        {
            NOVA_CLIENT_INFO("  cbuffer b{} '{}' ({} bytes)", constant.registerIndex,
                             constant.name, constant.sizeInBytes);
        }

        // ---- constant buffers, sized once ----------------------------------
        //
        // Created explicitly rather than on first use: the size comes from the
        // struct, and a buffer sized by whichever struct happened to be written
        // first would be sized by accident.
        Nova::Sandbox::CreateFrameConstants(material_);

        // ---- pipeline state ------------------------------------------------
        //
        // Built against the CONTEXT'S root signature, not one of its own. Two
        // root signatures in one renderer is the failure mode that surfaces at
        // draw time as "root signature mismatch" naming neither signature.
        pipeline_ = std::make_unique<Nova::Renderer::D3D12Pipeline>();
        pipeline_->Create(context_->GetDevice(), shader, context_->GetRootSignature());

        // ---- the scene ------------------------------------------------------
        //
        // A cube and a ground plane, or a loaded model file when one is present.
        // The scene is a separate translation unit: what is IN the scene is not
        // application behaviour, and this is the part most likely to be replaced
        // wholesale by the next milestone.
        //
        // A missing model file is expected here and produces the procedural cube,
        // so the renderer can be exercised with no content pipeline at all. That
        // ordering matters: a pipeline that treats the hard-coded mesh as the
        // primary path never gets its loader tested.
        if (!scene_.Initialize(context_->GetDevice(), std::string(kModelPath)))
        {
            // Both the file and the procedural fallback failed. Not recoverable
            // from inside the frame loop, and continuing would draw an empty
            // scene that looks like a renderer bug.
            throw std::runtime_error("Sandbox: could not create the demo scene");
        }

        NOVA_CLIENT_INFO("Scene ready: reflected stride {}", shader.GetVertexStride());
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
    /// By value, not by pointer: a Camera is a 40-byte POD with no resources
    /// and no lifetime of its own, so indirection would buy nothing and would
    /// make the common "where am I" read a pointer chase.
    Nova::Scene::Camera camera_;

    /// Heap-allocated because FPSCameraController is non-movable: it holds a
    /// Window reference and an event subscription token, both tied to the
    /// Application's own lifetime.
    std::unique_ptr<Nova::Scene::FPSCameraController> controller_;

    /// Subscription id for the WindowResize handler above, released in
    /// OnShutdown while the Window is still alive.
    Nova::EventDispatcher::Token resizeToken_ = 0;

    /// Seconds since the last camera heartbeat line.
    float cameraLogAccumulator_ = 0.0F;

    std::unique_ptr<Nova::Renderer::D3D12Context> context_;

    /// Shader pipeline: owns the PSO built from the reflected lit shader.
    std::unique_ptr<Nova::Renderer::D3D12Pipeline> pipeline_;

    /// Shader plus the constant buffers bound to b0/b1/b2. A member rather than
    /// a local because the buffers are GPU resources whose lifetime must outlive
    /// the frame that filled them.
    Nova::Renderer::Material material_;

    /// The demo scene: a cube or loaded model plus a ground plane.
    Nova::Sandbox::DemoScene scene_;

    int   frames_     = 0;

    /// Rotating per-frame constant buffer slot, 0 to frames-in-flight minus one.
    std::uint32_t frameSlot_ = 0;

    /// Yaw of the object's model matrix, accumulated per frame.
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
