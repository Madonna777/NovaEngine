// ===========================================================================
//  D3D12SwapChain.cpp
// ---------------------------------------------------------------------------
//  The flip-model swap chain, its render target views, and resizing.
//
//  WHAT A SWAP CHAIN IS
//  -------------------
//  Not one buffer, but a set of buffers owned by the operating system's
//  compositor. The application renders into a buffer the compositor does not own
//  at that moment; Present hands it over and takes back a different one. Each
//  hand-over is a frame boundary the whole system agrees on, which is why
//  vsync, window activation and tearing are properties of the swap chain rather
//  than of the application.
//
//  WHY FLIP MODEL AND NOT THE ORIGINAL BITBLT MODEL
//  ------------------------------------------------
//  The original model copies the finished image into a front buffer, which the
//  compositor then blits to the screen. Two consequences: a full-screen copy
//  every frame, and - because the compositor owns the front buffer - tearing.
//  Flip model has the compositor swap a pre-composed back buffer instead, so
//  there is no copy, and DXGI can wait for the vertical blank before swapping.
//
//  FLIP_DISCARD versus FLIP_SEQUENCE is the choice between the two flip
//  variants: DISCARD destroys the contents after presentation and lets DXGI
//  recycle the memory, possibly with a matching decode engine - the entire basis
//  of hardware video playback and Variable-Rate shading in a game window.
//  SEQUENCE guarantees the contents survive, so the memory cannot be recycled,
//  which costs the optimisation and buys a game nothing. Code that writes into a
//  discarded buffer and reads it back later relies on behaviour that is
//  documented not to exist.
//
//  THE ONE API THAT CHANGES WITH FLIP MODEL
//  ----------------------------------------
//  IDXGISwapChain::GetBuffer - the original accessor - returns
//  DXGI_ERROR_NOT_CURRENTLY_AVAILABLE for a flip-model chain, because the whole
//  point is that the application does not choose which buffer it renders into.
//  IDXGISwapChain3::GetBuffer is the legal version, and asking for it is why
//  this context holds an IDXGISwapChain3. It also means
//  GetCurrentBackBufferIndex() is DXGI's answer and not a counter this class may
//  increment - the two are not required to agree.
// ===========================================================================

#include <Renderer/D3D12Context.h>

#include <Core/Log.h>

#include <algorithm>
#include <cstdint>

namespace Nova::Renderer
{
namespace
{
/// Builds the swap chain descriptor for a client-area-sized buffer.
DXGI_SWAP_CHAIN_DESC1 MakeSwapChainDescription(std::uint32_t width, std::uint32_t height)
{
    DXGI_SWAP_CHAIN_DESC1 description{};

    description.Width  = width;
    description.Height = height;
    description.Format = D3D12Context::kBackBufferFormat;

    // SampleDesc.Count of 1 means NO multisampling.
    //
    // WHY MSAA IS A SWAP-CHAIN PROPERTY AND NOT SOMETHING THE RENDERER TURNS
    // ON: in D3D12 there is no implicit MSAA. To multisample, the back buffer
    // itself is created with a SampleDesc.Count above 1, and it becomes a
    // MULTISAMPLED resource that the pipeline must resolve into a single-sample
    // one before anything can read it. A renderer that wants MSAA therefore
    // changes the swap chain and adds a resolve target, and cannot have it as
    // something switched on around draw calls. This is also why the window is
    // created without a GLFW_SAMPLES hint - see Core/Window.h.
    description.SampleDesc.Count = 1;

    // RENDER_TARGET_OUTPUT is not optional and not a default. Without this flag
    // the buffer is created without the storage a render target view requires,
    // and CreateRenderTargetView fails on a resource that looks correct.
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;

    description.BufferCount = D3D12Context::kSwapBufferCount;

    // NONE means DXGI will not scale the image to fit: the back buffer must
    // already match the window's client area, and Present simply fails with
    // DXGI_STATUS_MODE_CHANGED if it does not. That is the right trade here
    // because BeginFrame resizes the chain to the client area every frame it
    // detects a change - so scaling can never be the correct thing to do, and
    // leaving the option on would only hide the one case worth knowing about.
    description.Scaling = DXGI_SCALING_NONE;

    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    // IGNORE means the window is opaque and the compositor never blends the
    // frame with what is behind it. A window that genuinely needs per-pixel
    // transparency sets ALPHA_PREMULTIPLIED here, and then the clear colour's
    // alpha is meaningful - which is why ClearRenderTarget documents its alpha
    // argument as ignored for this configuration.
    description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    // No ALLOW_MODE_SWITCH: this window never becomes fullscreen, and the flag
    // changes which adapter owns the back buffers, which would silently move
    // the engine to a different GPU.
    description.Flags = 0;

    return description;
}
} // namespace

// ===========================================================================
//  Creation
// ===========================================================================

void D3D12Context::CreateSwapChain(std::uint32_t width, std::uint32_t height)
{
    const DXGI_SWAP_CHAIN_DESC1 description = MakeSwapChainDescription(width, height);

    // CreateSwapChainForHwnd returns the BASE IDXGISwapChain even though the
    // device was handed the newest interfaces - the signature predates them.
    // The upgrade has to be explicit, and it is what makes GetBuffer and
    // GetCurrentBackBufferIndex legal for a flip-model chain.
    // CreateSwapChainForHwnd returns an IDXGISwapChain1, not the plain
    // IDXGISwapChain its oldest overload produced - which also means the out
    // parameter's type has to match. The upgrade to IDXGISwapChain3 has to be
    // explicit, and it is what makes GetBuffer and GetCurrentBackBufferIndex
    // legal for a flip-model chain.
    ComPtr<IDXGISwapChain1> baseSwapChain;
    NOVA_THROW_IF_FAILED(factory_->CreateSwapChainForHwnd(commandQueue_.Get(), windowHandle_,
                                                           &description, nullptr, nullptr,
                                                           &baseSwapChain));
    NOVA_THROW_IF_FAILED(baseSwapChain.As(&swapChain_));

    // Cached so ResizeBuffers is told the same things the swap chain was created
    // with. ResizeBuffers REPLACES the buffers: a descriptor passed at creation
    // is not remembered for later calls, and resizing to a different count or
    // format is a different swap chain as far as DXGI is concerned. Getting this
    // wrong produces a swap chain whose buffers do not match its views, which
    // the debug layer reports as a use-after-resize.
    backBufferFormat_ = description.Format;
    swapChainFlags_   = description.Flags;

    // The heap size can only be read once the device exists, and it is stable
    // for the lifetime of the device - which is why it is cached rather than
    // queried per frame.
    rtvDescriptorStride_ =
        device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    CreateRenderTargetViews();

    NOVA_INFO("Swap chain: {}x{} {} ({} buffers, RTV stride {} bytes)", width, height,
              backBufferFormat_ == DXGI_FORMAT_R8G8B8A8_UNORM ? "R8G8B8A8" : "other",
              kSwapBufferCount, rtvDescriptorStride_);
}

void D3D12Context::CreateRenderTargetViews()
{
    // ---- the heap --------------------------------------------------------
    //
    // A descriptor heap is ONE CONTIGUOUS ARRAY of fixed-size slots. A render
    // target view is a slot saying "this resource is a render target with these
    // properties", and the shader or the fixed-function stage reaches it by
    // INDEX rather than by pointer - which is what allows a compute shader to
    // write descriptors into a heap that a graphics command list then consumes.
    //
    // The heap is created with the SHADER_VISIBLE flag OFF deliberately. This
    // is a bindless-hostile choice and the opposite of what most samples do,
    // but shader visibility is what forces a heap into the 64MB tier limit and
    // forces the descriptor heap's lifetime to match the resource's. A heap of
    // render target views is read by the command list on the CPU side only, so
    // the flag buys nothing and costs a restriction.
    D3D12_DESCRIPTOR_HEAP_DESC heapDescription{};
    heapDescription.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heapDescription.NumDescriptors = kSwapBufferCount;
    heapDescription.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    NOVA_THROW_IF_FAILED(device_->CreateDescriptorHeap(&heapDescription, IID_PPV_ARGS(&rtvHeap_)));

    // ---- one view per back buffer ----------------------------------------
    //
    // Created ONCE per swap chain lifetime, not once per frame. A view does not
    // depend on which frame is using it - it describes the buffer, and the
    // buffer only changes identity on ResizeBuffers. The per-frame cost is one
    // pointer addition in BeginFrame.
    const D3D12_CPU_DESCRIPTOR_HANDLE heapStart = rtvHeap_->GetCPUDescriptorHandleForHeapStart();

    for (std::uint32_t index = 0; index < kSwapBufferCount; ++index)
    {
        // Scoped so the buffer's reference is released before the next
        // iteration. Holding every buffer alive here is harmless on the create
        // path but is exactly the reference that must not be alive at
        // ResizeBuffers time, so the pattern is established here rather than
        // being fixed later.
        ComPtr<ID3D12Resource> backBuffer;
        NOVA_THROW_IF_FAILED(swapChain_->GetBuffer(index, IID_PPV_ARGS(&backBuffer)));

        // A null resource description means "derive the format from the
        // resource", which is correct and is what keeps the views valid across
        // a resize that changes the buffer format.
        device_->CreateRenderTargetView(backBuffer.Get(), nullptr,
                                        OffsetCpu(heapStart, index, rtvDescriptorStride_));
    }
}

// ===========================================================================
//  Resizing
// ===========================================================================

void D3D12Context::ResizeIfClientAreaChanged()
{
    // Read the window's client area, not its window rectangle: the latter
    // includes borders and title bar, so using it gives a back buffer that is
    // correct on a borderless window and wrong on every other one.
    //
    // Sizes are PHYSICAL pixels. GLFW sets PER_MONITOR_AWARE_V2 before creating
    // any window, so GetClientRect and Window::GetSize agree - see the DPI note
    // in Core/Window.h, where the two-units trap is spelled out.
    RECT clientArea{};
    if (::GetClientRect(windowHandle_, &clientArea) == FALSE)
    {
        // A destroyed window, or one on a thread that owns no input queue. Not
        // worth throwing over mid-frame: the next frame's Present will fail with
        // a message that says far more than this would.
        NOVA_WARN("GetClientRect failed while checking for a resize; skipping");
        return;
    }

    // A minimised window reports 0x0. Resizing a swap chain to zero is invalid,
    // so the size is clamped to 1x1 - which is legal, costs nothing, and means
    // that when the window is restored the same code path resizes it back.
    const LONG clientWidth  = std::max<LONG>(clientArea.right - clientArea.left, 1);
    const LONG clientHeight = std::max<LONG>(clientArea.bottom - clientArea.top, 1);

    const auto width  = static_cast<std::uint32_t>(clientWidth);
    const auto height = static_cast<std::uint32_t>(clientHeight);

    if (width == backBufferWidth_ && height == backBufferHeight_)
    {
        return;
    }

    NOVA_INFO("Resizing swap chain: {}x{} -> {}x{}", backBufferWidth_, backBufferHeight_, width,
              height);

    // DXGI_STATUS_MODE_CHANGED handling in HandlePresentResult sets the cached
    // size to zero, which arrives here as a mismatch. That is the intended path
    // for it, not a special case.

    // ---- ResizeBuffers' three preconditions ------------------------------
    //
    // 1. THE GPU MUST BE IDLE. ResizeBuffers destroys the buffers it replaces,
    //    and a buffer the GPU is still reading is a use-after-free. WaitForGPU
    //    signals a fresh fence value, so it waits for EVERYTHING submitted so
    //    far, not merely the frame that happens to be in flight.
    //
    // 2. NO BACK BUFFER REFERENCES MAY BE OUTSTANDING. currentBackBuffer_ holds
    //    the one this class acquired last frame; releasing it is the entire
    //    reason it is a member rather than a local in BeginFrame. A caller
    //    holding its own reference must release it before the window resizes,
    //    which is why the API does not hand one out.
    //
    // 3. NO PRESENT MAY BE PENDING. WaitForGPU covers this too, because Present
    //    is queue work.
    WaitForGPU();
    currentBackBuffer_.Reset();

    NOVA_THROW_IF_FAILED(
        swapChain_->ResizeBuffers(kSwapBufferCount, width, height, backBufferFormat_,
                                  swapChainFlags_));

    // The views referenced the old buffers, so they are rebuilt. The heap is
    // recreated rather than overwritten: its descriptor count is unchanged, but
    // creating it fresh leaves no stale view behind if the count ever differs.
    CreateRenderTargetViews();

    // Updated LAST, after every call that could throw. A failed resize leaves
    // the cached size describing what the swap chain still is, so the next frame
    // retries instead of believing a resize that did not happen.
    backBufferWidth_  = width;
    backBufferHeight_ = height;
}

} // namespace Nova::Renderer