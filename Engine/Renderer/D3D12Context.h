// ===========================================================================
//  D3D12Context.h
// ---------------------------------------------------------------------------
//  Everything Direct3D 12 that a frame needs: the device, the command queue,
//  the swap chain, the fence, and the BeginFrame/Clear/EndFrame lifecycle.
//
//  WHY ONE CLASS AND NOT A BAG OF GLOBALS
//  --------------------------------------
//  D3D12's interfaces are reference-counted COM objects with no owner, so the
//  usual outcome of "let me just make these globals" is an Init that runs twice,
//  a device that outlives the window it presents to, and a fence whose event
//  handle was closed by a half-finished shutdown. Here every interface is a
//  member, so all of it is constructed in the constructor, destroyed in the
//  destructor, in reverse dependency order, and a compiler error if a method
//  is added that uses an uninitialised member.
//
//  WHY BeginFrame / EndFrame ARE EXPLICIT
//  -------------------------------------
//  D3D12 is not an immediate-mode API and cannot be driven by a bare "draw()".
//  Recording commands mutates state that must be valid at the moment of the
//  call, against a command list that belongs to a frame slot whose allocator the
//  GPU must already be done with. Those preconditions are invisible if the
//  renderer hides them, and the resulting bugs (writing to a recycled allocator,
//  presenting a back buffer the GPU is still reading) surface as corruption
//  hundreds of frames later.
//
//  So the frame boundary is explicit in the type system: a caller cannot
//  present without having begun, and the matching is checked by the debug
//  layer's usage warnings rather than by hoping.
//
//  THE FRAME LOOP IN FULL, AND WHY EACH PIECE IS WHERE IT IS
//  --------------------------------------------------------
//      BeginFrame
//        1. wait on the fence for THIS frame slot  - the allocator we are about
//           to reset may still be executing on the GPU
//        2. resize the swap chain if the window changed
//        3. allocator->Reset()      - legal only when the GPU is done with it
//        4. commandList->Reset()    - legal only while the list is CLOSED
//        5. acquire the back buffer  - GetCurrentBackBufferIndex() rotates, so
//           the index is not a counter we may choose
//        6. bind its render target view
//      ... the application records whatever it wants ...
//      EndFrame
//        7. commandList->Close()     - flips out of RECORDING into EXECUTABLE
//        8. ExecuteCommandLists()    - hands the list to the GPU
//        9. Present()                - flips the back buffer for the compositor
//       10. queue->Signal(fence)     - the only point at which this frame is
//                                      observably complete on the GPU
//      next BeginFrame advances the slot, so step 1 waits on the frame that
//      used the allocator two frames ago. With two slots in flight, the CPU can
//      be recording frame N+1 while the GPU renders frame N.
//
//  WHY TWO FRAMES IN FLIGHT
//  -----------------------
//  One frame in flight means the CPU waits for the GPU before it can start the
//  next frame, so the frame time is CPU time PLUS GPU time - pipelining never
//  happens and a scene that is 90% GPU-bound runs at less than half speed.
//  Two lets the CPU hide its work inside the GPU's. Beyond that the gain
//  flattens while latency and the memory cost of every per-frame allocation
//  double, so two is the standard choice, and kFramesInFlight exists as a
//  named constant so the value is one edit away if measurement ever disagrees.
//
//  The CPU/GPU split is what the fence is FOR: both sides are allowed to be
//  ahead, but never the CPU into a slot the GPU is still reading. Every
//  allocator, command list and back buffer is owned by exactly one slot, and
//  the fence is the only thing that says when a slot is free again.
// ===========================================================================
#pragma once

#include <Renderer/D3D12Helpers.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Nova::Renderer
{
/// Owns the D3D12 device, swap chain and command submission path for one window.
///
/// @note Not copyable or movable: a D3D12 context is tied to the HWND it was
///       created for, and the COM objects inside it are thread-affine in a way
///       that makes an accidental copy a silent bug rather than a slow one.
class D3D12Context final
{
public:
    /// Command allocators, and therefore frames the CPU may run ahead by.
    ///
    /// @note Two is not a tuning knob with a default; see the header comment.
    ///       Changing it changes kSwapBufferCount's ideal value too, because a
    ///       swap chain wants at least as many buffers as there are frames in
    ///       flight - otherwise Present blocks waiting for a buffer the CPU has
    ///       not finished with, which silently reintroduces full serialisation.
    static constexpr std::uint32_t kFramesInFlight = 2;

    /// Back buffers in the swap chain.
    ///
    /// @note A flip-model swap chain MUST have at least two. With one, Present
    ///       must block until the compositor has consumed the only buffer, so
    ///       the GPU and CPU can never overlap and vsync doubles the cost.
    static constexpr std::uint32_t kSwapBufferCount = 2;

    /// Back buffer pixel format.
    ///
    /// WHY R8G8B8A8 AND NOT THE B8G8R8A8 EVERY D3D12 SAMPLE USES: a clear
    /// takes four floats, and D3D12 interprets them in the RENDER TARGET's
    /// channel order. DirectXMath's XMFloat4 is stored (x=r, y=g, z=b, w=a) in
    /// memory, so R8G8B8A8_UNORM lets ClearRenderTargetView read the float
    /// array directly with no swizzle. B8G8R8A8_UNORM means every clear colour
    /// - and later every vertex colour and texture sample - needs its red and
    /// blue swapped, and the resulting bug is a magenta sky that looks like a
    /// deliberate artistic choice.
    static constexpr DXGI_FORMAT kBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    /// Creates the device, command queue, swap chain and fence, and makes the
    /// swap chain's first back buffer match the window's client area.
    ///
    /// @param windowHandle        The window to present into. Must belong to a
    ///                            live window; DXGI holds its own reference to
    ///                            it, so the context may outlive the Window
    ///                            object but never a destroyed HWND.
    /// @param backBufferWidth     Client width in PHYSICAL pixels. See the DPI
    ///                            note in Core/Window.h - this is not a logical
    ///                            pixel count, and using one on a scaled display
    ///                            gives a back buffer a fifth too small.
    /// @param backBufferHeight    Client height in physical pixels.
    /// @param vsyncEnabled        Blocks Present on the vertical blank when true.
    ///                            Costs up to one frame of latency and buys the
    ///                            absence of tearing.
    ///
    /// @throws std::runtime_error if any D3D12 or DXGI call fails, including
    ///         when no suitable adapter exists.
    ///
    /// @note Expensive and not thread-safe against itself: D3D12 device
    ///       creation serialises internally, so this belongs on the main thread
    ///       during startup, which is where Application::OnStartup puts it.
    explicit D3D12Context(HWND                  windowHandle,
                          std::uint32_t        backBufferWidth,
                          std::uint32_t        backBufferHeight,
                          bool                 vsyncEnabled);

    /// Waits for all submitted GPU work, then releases every interface.
    ///
    /// @note Never throws. A destructor that propagates an exception during
    ///       stack unwinding calls std::terminate, so a shutdown failure is
    ///       logged and the rest of the teardown continues.
    ~D3D12Context();

    D3D12Context(const D3D12Context&)            = delete;
    D3D12Context& operator=(const D3D12Context&) = delete;
    D3D12Context(D3D12Context&&)                 = delete;
    D3D12Context& operator=(D3D12Context&&)      = delete;

    // -----------------------------------------------------------------------
    //  Frame lifecycle
    // -----------------------------------------------------------------------

    /// Waits for this frame's slot, then starts recording into it.
    ///
    /// Resizes the swap chain first if the window changed size since the last
    /// frame, so a caller never has to arrange a resize itself - and cannot
    /// forget to.
    ///
    /// @throws std::runtime_error on a failed wait, reset, or buffer acquire.
    void BeginFrame();

    /// Records a clear of the whole back buffer.
    ///
    /// @param red   Red,   0.0 to 1.0.
    /// @param green Green, 0.0 to 1.0.
    /// @param blue  Blue,  0.0 to 1.0.
    /// @param alpha Alpha, 0.0 to 1.0. Ignored by a windowed swap chain, whose
    ///              alpha mode is IGNORE - see the comment on the descriptor.
    ///
    /// @note Recorded, not executed. The GPU does not see this until EndFrame,
    ///       so calling it costs a command-list write and nothing else, and
    ///       calling it in a loop costs the same as calling it once.
    ///
    /// @pre BeginFrame has run for this frame.
    void ClearRenderTarget(float red, float green, float blue, float alpha = 1.0F);

    /// Closes and submits the command list, presents, and signals the fence.
    ///
    /// @throws std::runtime_error on a failed close, submit, or present. A
    ///         failed Present is almost always a removed or hung device, so the
    ///         message carries GetDeviceRemovedReason() and the debug layer's
    ///         message queue.
    void EndFrame();

    /// Blocks until every command submitted so far has completed on the GPU.
    ///
    /// @throws std::runtime_error if the fence cannot be signalled.
    ///
    /// @note A shutdown requirement, not a frame-time optimisation. Releasing
    ///       the last reference to a resource the GPU is still reading is a
    ///       use-after-free, and D3D12 does not detect it - the process dies
    ///       somewhere else, later. The API allows it, and it is the single
    ///       most common D3D12 teardown bug.
    void WaitForGPU();

    // -----------------------------------------------------------------------
    //  Introspection
    // -----------------------------------------------------------------------

    /// @return The adapter chosen at construction, as reported by DXGI.
    [[nodiscard]] std::string_view GetAdapterName() const noexcept { return adapterName_; }

    /// @return Dedicated video memory of the chosen adapter, in bytes. Zero on
    ///         an integrated or software adapter, where there is no such pool.
    [[nodiscard]] std::uint64_t GetDedicatedVideoMemoryBytes() const noexcept
    {
        return dedicatedVideoMemoryBytes_;
    }

    /// @return Which of the kSwapBufferCount buffers the current frame is
    ///         drawing into. Rotates per frame; not a stable identifier.
    [[nodiscard]] std::uint32_t GetCurrentBackBufferIndex() const noexcept
    {
        return backBufferIndex_;
    }

    /// @return Current back buffer size in physical pixels.
    [[nodiscard]] std::uint32_t GetBackBufferWidth() const noexcept { return backBufferWidth_; }
    [[nodiscard]] std::uint32_t GetBackBufferHeight() const noexcept { return backBufferHeight_; }

    /// @return Whether Present waits for the vertical blank.
    [[nodiscard]] bool IsVSyncEnabled() const noexcept { return vsyncEnabled_; }

private:
    // ---- construction steps, one translation unit each --------------------

    /// Creates the DXGI factory and the ID3D12Device on the best adapter.
    void CreateFactoryAndDevice();

    /// Logs the feature level and resource binding tier the device reports.
    void LogDeviceCapabilities();

    /// Creates the direct command queue, the fence, its event, and the per-slot
    /// command allocators plus the single command list.
    void CreateCommandInfrastructure();

    /// Creates the flip-model swap chain for the window at the given size.
    void CreateSwapChain(std::uint32_t width, std::uint32_t height);

    /// Creates the RTV descriptor heap and one render target view per back
    /// buffer. Rebuilt on resize, because a view describes a specific buffer and
    /// ResizeBuffers replaces them.
    void CreateRenderTargetViews();

    // ---- per-frame helpers ------------------------------------------------

    /// Blocks until the fence reports @p fenceValue complete.
    void WaitForFence(std::uint64_t fenceValue);

    /// Resizes the back buffers if the window's client area no longer matches.
    void ResizeIfClientAreaChanged();

    /// Interprets a Present result: accepts the non-error status codes, and
    /// reports device removal with its reason and the debug message queue.
    void HandlePresentResult(HRESULT result);

    /// Prints the D3D12 debug layer's queued messages. A no-op unless the debug
    /// layer was enabled at build time.
    void DumpDebugMessages() const;

    /// @return Index of the most suitable adapter for EnumAdapters1, or
    ///         kNoAdapter when the machine has no usable hardware adapter.
    ///
    /// @note Software adapters are excluded outright rather than ranked last.
    ///       See the definition for why ranking them is not good enough.
    [[nodiscard]] static std::uint32_t SelectAdapter(IDXGIFactory4* factory);

    /// Sentinel for "no hardware adapter found". The caller then falls back to
    /// D3D12CreateDevice's default adapter, which is WARP on such a machine.
    static constexpr std::uint32_t kNoAdapter = 0xFFFFFFFFU;

    /// Lowest feature level the engine will accept.
    ///
    /// WHY 11_0 AND NOT AN IMPLICIT DEFAULT: this is the baseline a machine
    /// without a discrete GPU is guaranteed to meet. Everything the engine
    /// does today - a direct queue, flip-model present, an RTV heap - is an
    /// 11_0-era API. Raising it is a one-line change and a deliberate one: a
    /// level that excludes a supported machine is worse than a missing
    /// optimisation.
    ///
    /// NOTE THE TYPE: D3D12CreateDevice takes D3D_FEATURE_LEVEL (the common
    /// D3D type), not a D3D12_* alias - there is no such alias, which is the
    /// error that silently masqueraded as working code in the first compile.
    static constexpr D3D_FEATURE_LEVEL kMinimumFeatureLevel = D3D_FEATURE_LEVEL_11_0;

    // ---- COM state --------------------------------------------------------
    //
    // DECLARATION ORDER IS DESTRUCTION ORDER, REVERSED. The destructor waits
    // for the GPU first, so this only has to guarantee that nothing is released
    // before something it depends on - which the order below does: the queue
    // outlives the swap chain it created, and the device outlives both.
    ComPtr<IDXGIFactory4> factory_;
    ComPtr<ID3D12Device>  device_;
    ComPtr<ID3D12CommandQueue> commandQueue_;
    ComPtr<IDXGISwapChain3>    swapChain_;
    ComPtr<ID3D12DescriptorHeap> rtvHeap_;

    /// The back buffer being rendered into this frame. Held as a member, not a
    /// local in BeginFrame, because ResizeBuffers requires that every reference
    /// to a back buffer be released first and the destructor must not run while
    /// one is outstanding.
    ComPtr<ID3D12Resource> currentBackBuffer_;

    ComPtr<ID3D12Fence> fence_;
    WinHandle           fenceEvent_;

    /// One allocator per frame slot - see kFramesInFlight.
    std::vector<ComPtr<ID3D12CommandAllocator>> frameAllocators_;

    /// ONE command list, reset against a different allocator each frame.
    ///
    /// WHY NOT ONE LIST PER SLOT: a command list holds ~1KB of recorded state
    /// plus its allocator pointer, and only one of them can be recording at a
    /// time regardless. A single list serialises on the CPU for free, and the
    /// GPU never sees the difference - it is the allocator that must be
    /// exclusive, and that is what the vector provides.
    ComPtr<ID3D12GraphicsCommandList> commandList_;

    // ---- fixed configuration ----------------------------------------------

    HWND         windowHandle_  = nullptr;
    DXGI_FORMAT  backBufferFormat_ = kBackBufferFormat;
    std::uint32_t swapChainFlags_   = 0;

    /// Slot size of an entry in the RTV heap. Cached from
    /// GetDescriptorHandleIncrementSize rather than recomputed per frame.
    std::uint32_t rtvDescriptorStride_ = 0;

    /// The render target view for the back buffer acquired by the current
    /// BeginFrame. Set once per frame and read by ClearRenderTarget, which is
    /// why it is a member and not something ClearRenderTarget recomputes - a
    /// caller that cleared before BeginFrame must get a stale handle, not a
    /// default-constructed one that silently clears the wrong memory.
    D3D12_CPU_DESCRIPTOR_HANDLE currentRtvDescriptor_{};

    // ---- per-frame state ---------------------------------------------------

    std::uint32_t backBufferWidth_  = 0;
    std::uint32_t backBufferHeight_ = 0;
    std::uint32_t backBufferIndex_  = 0;

    /// Which slot is in use: 0 to kFramesInFlight - 1. Advanced at the END of
    /// EndFrame so that BeginFrame and EndFrame always agree on the slot, and a
    /// caller cannot accidentally pair them across a frame boundary.
    std::uint32_t frameIndex_ = 0;

    /// Fence value signalled when the frame using that slot completed. 0 means
    /// "never signalled", which is how BeginFrame knows a slot needs no wait on
    /// its first use.
    std::array<std::uint64_t, kFramesInFlight> frameFenceValues_{};

    /// Monotonic source of fence values. Every Signal takes a fresh value, so
    /// no two submissions can complete in a way that confuses the wait.
    std::uint64_t nextFenceValue_ = 0;

    std::string   adapterName_;
    std::uint64_t dedicatedVideoMemoryBytes_ = 0;
    bool          vsyncEnabled_              = false;
};

} // namespace Nova::Renderer