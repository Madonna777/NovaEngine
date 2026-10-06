// ===========================================================================
//  D3D12Context.cpp
// ---------------------------------------------------------------------------
//  Construction, teardown, and the frame lifecycle.
// ===========================================================================

#include <Renderer/D3D12Context.h>

#include <Core/Log.h>

#include <iterator>
#include <stdexcept>
#include <string>

namespace Nova::Renderer
{
namespace
{
/// Storage for one debug-layer message.
///
/// WHY A STRUCT WITH A TRAILING ARRAY RATHER THAN A std::string: the debug
/// layer's GetMessage writes the message text INTO THE CALLER'S BUFFER and
/// leaves pDescription pointing into it. There is no allocation to free and no
/// ownership to manage - which means the buffer must outlive the pointer, and a
/// heap string read after the call would be exactly the use-after-free the
/// memory system was chosen to avoid.
///
/// The array also fixes alignment: GetMessage writes a D3D12_MESSAGE through a
/// pointer, so the storage has to be aligned for it. A std::vector<char> is
/// aligned in practice and by no promise.
struct DebugMessageStorage
{
    D3D12_MESSAGE message{};
    char          description[2048]{};
};

/// Severities worth printing.
///
/// WHY NOT D3D12_MESSAGE_SEVERITY_CORRUPTION ALONE: corruption is the most
/// severe bucket and, on a driver that does not implement the full debug
/// layer, the one least likely to appear. Warnings are where a mismatched
/// descriptor heap or an unbound render target shows up first, and those are
/// the bugs worth catching before they become corruption.
///
/// Deliberately non-const: the filter takes a plain D3D12_MESSAGE_SEVERITY*
/// and the API has no const version, so a const array would need a const_cast
/// at every use site. The array lives in an anonymous namespace, so it has
/// internal linkage and no other translation unit can see it mutated.
D3D12_MESSAGE_SEVERITY kReportedSeverities[] = {
    D3D12_MESSAGE_SEVERITY_CORRUPTION,
    D3D12_MESSAGE_SEVERITY_ERROR,
    D3D12_MESSAGE_SEVERITY_WARNING,
};
} // namespace

// ===========================================================================
//  Construction
// ===========================================================================

D3D12Context::D3D12Context(HWND           windowHandle,
                           std::uint32_t backBufferWidth,
                           std::uint32_t backBufferHeight,
                           bool          vsyncEnabled)
    : windowHandle_(windowHandle),
      backBufferWidth_(backBufferWidth),
      backBufferHeight_(backBufferHeight),
      vsyncEnabled_(vsyncEnabled)
{
    // WHY THESE THREE STEPS AND NOT ONE CreateEverything(): each is a separate
    // translation unit because each is a genuinely separate concern that
    // changes for different reasons. A resize does not touch the queue, adding
    // a bundle or async compute queue does not touch the swap chain, and
    // swapping the vendor-selection policy does not touch either. When they
    // live in one function, any change means reading all three.
    //
    // The order is a dependency order, not a preference. The swap chain needs
    // a device to be its rendering target and a queue to be submitted to; the
    // queue needs a device; the device needs a factory. Nothing here has to
    // succeed before anything later can be attempted, so a failure at any point
    // unwinds cleanly and destroys whatever was already created.
    CreateFactoryAndDevice();
    CreateCommandInfrastructure();
    CreateRootSignature();
    CreateSwapChain(backBufferWidth, backBufferHeight);
}

// ===========================================================================
//  Destruction
// ===========================================================================

D3D12Context::~D3D12Context()
{
    // NO THROW. This destructor also runs during stack unwinding when the
    // constructor succeeded and a later frame threw - and a throwing destructor
    // in that state calls std::terminate, replacing a reportable exception with
    // a crash. Log and continue instead.
    //
    // WHY THE WAIT AT ALL, GIVEN THAT NOBODY HAS ASKED IT TO: releasing the
    // last reference to a resource the GPU is still reading is a use-after-free
    // that D3D12 does not detect and that often manifests much later, as a
    // crash in unrelated code. Waiting is not politeness here; it is the only
    // thing standing between a clean exit and a heisenbug.
    try
    {
        WaitForGPU();
    }
    catch (const std::exception& error)
    {
        NOVA_ERROR("D3D12Context: WaitForGPU during shutdown failed: {}", error.what());
    }
    catch (...)
    {
        NOVA_ERROR("D3D12Context: WaitForGPU during shutdown failed with a non-std::exception");
    }

    // Everything else is released by the members, in reverse declaration order.
    // That order is chosen so no interface outlives the device that created it -
    // see the member list in the header.
}

// ===========================================================================
//  Frame lifecycle
// ===========================================================================

void D3D12Context::BeginFrame()
{
    // ---- 1. wait for this slot's allocator -------------------------------
    //
    // The allocator we are about to Reset was last submitted two frames ago. The
    // GPU may still be executing it, and Reset on a busy allocator is a
    // use-after-free of GPU-visible state - the D3D12 debug layer reports it as
    // "RESET_ON_GPU", but the runtime does not.
    //
    // Zero means this slot has never been signalled, so there is nothing to wait
    // for. That is the whole reason frameFenceValues_ needs a sentinel: without
    // one, the first two frames would wait on fence value 0, which is the
    // fence's initial value and therefore already satisfied.
    const std::uint64_t pendingFence = frameFenceValues_[frameIndex_];
    if (pendingFence != 0)
    {
        // Cleared BEFORE the wait, so a throw here cannot leave the value set
        // and make every subsequent frame re-wait on the same fence.
        frameFenceValues_[frameIndex_] = 0;
        WaitForFence(pendingFence);
    }

    // ---- 2. resize before anything touches the back buffers --------------
    //
    // Order matters: ResizeBuffers invalidates every back buffer and every RTV
    // pointing at one, so it cannot run after the buffer has been acquired.
    ResizeIfClientAreaChanged();

    // ---- 3. reset the allocator ------------------------------------------
    //
    // Allocators are the GPU-exclusive resource in D3D12. Command lists and
    // back buffers can be reused freely once their contents have been consumed;
    // an allocator cannot, because the memory it hands out is still being read.
    NOVA_THROW_IF_FAILED(frameAllocators_[frameIndex_]->Reset());

    // ---- 4. reset the command list ---------------------------------------
    //
    // A command list may only be Reset while it is CLOSED, and Close is what
    // EndFrame did at the end of the previous use of this list. Skipping the
    // Close - or calling Close after instead of before - is the single most
    // common D3D12 mistake, and it fails on the very first frame.
    //
    // The second argument is the pipeline state the list starts bound to.
    // nullptr means "no pipeline state bound", which is also the state a list
    // is left in by Close, so passing the previous frame's PSO would be both
    // meaningless and a way to accidentally inherit stale state.
    NOVA_THROW_IF_FAILED(commandList_->Reset(frameAllocators_[frameIndex_].Get(), nullptr));

    // ---- 5. acquire the back buffer --------------------------------------
    //
    // The index is CHOSEN BY DXGI, not by us. Present rotates it, and it must be
    // read every frame: caching it would eventually present into the buffer the
    // compositor is reading, which is tearing at best and a driver crash at
    // worst.
    backBufferIndex_ = swapChain_->GetCurrentBackBufferIndex();

    // GetBuffer on IDXGISwapChain3, never on the original IDXGISwapChain. In
    // flip model the legacy accessor returns DXGI_ERROR_NOT_CURRENTLY_AVAILABLE
    // - the whole point of the flip model is that the compositor owns buffer
    // selection, and DXGI will not let an application pick its own.
    NOVA_THROW_IF_FAILED(swapChain_->GetBuffer(backBufferIndex_, IID_PPV_ARGS(&currentBackBuffer_)));

    // ---- 6. point at this buffer's render target view --------------------
    //
    // The RTV is not recreated per frame. The heap has one slot per back buffer,
    // created once in CreateDefaultRenderTarget, and the per-frame cost is one
    // pointer addition. Recreating views every frame would work and would be
    // pure waste - descriptor creation is one of the more expensive things the
    // render loop does.
    const D3D12_CPU_DESCRIPTOR_HANDLE heapStart = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    currentRtvDescriptor_ =
        OffsetCpu(heapStart, backBufferIndex_, rtvDescriptorStride_);

    // ---- 6b. transition the acquired buffer to a renderable state ----------
    //
    // DXGI hands back the buffer in the PRESENT state: read-only for the
    // compositor, unusable for a pipeline that writes to it. D3D12 does not
    // hide this. The RTV clear below is recorded against a buffer the GPU may
    // still be presenting, which is the de-facto "black screen because nothing
    // executed properly" bug. The barrier declares the state change to the GPU
    // before the first recorded command touches the buffer.
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type                     = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource     = currentBackBuffer_.Get();
    barrier.Transition.StateBefore   = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter    = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource   = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList_->ResourceBarrier(1, &barrier);
}

void D3D12Context::ClearRenderTarget(float red, float green, float blue, float alpha)
{
    const float colour[4] = { red, green, blue, alpha };

    // OMSetRenderTargets takes a POINTER to the handle array. The temporary
    // copy keeps the member read-only here and makes "the command list owns its
    // own copy once this returns" explicit to the reader.
    D3D12_CPU_DESCRIPTOR_HANDLE target = currentRtvDescriptor_;

    // Bind first. ClearRenderTargetView does not require the view to be bound -
    // but the debug layer reports clearing an unbound view as an error, because
    // the clear would be invisible to a later draw that binds something else.
    // Binding first also makes the recorded state self-consistent for any
    // subsequent command that assumes a target.
    commandList_->OMSetRenderTargets(1, &target, FALSE, nullptr);

    // NumRects 0 with pRects nullptr means the whole resource: D3D12 treats
    // "no rectangles" as "all of them", not as "none".
    //
    // NOTE THE SIGNATURE: the handle is BY VALUE, not a pointer. An earlier
    // version of this line passed &target, which the compiler rejected - and
    // the error message is worth trusting over the documentation habit of
    // treating descriptor handles as reference types.
    commandList_->ClearRenderTargetView(target, colour, 0, nullptr);
}

void D3D12Context::SetViewport(float width, float height) const
{
    // RSSetViewports is a recorded command, like the barrier: the command list
    // carries it through BeginFrame/EndFrame until something changes it. A
    // viewport without a matching scissor leaves a gap the rasteriser is free
    // to fill, and the symptom is pixels outside the intended box - hence the
    // note on SetScissorRect.
    const D3D12_VIEWPORT viewport{ 0.0F, 0.0F, width, height, 0.0F, 1.0F };
    commandList_->RSSetViewports(1, &viewport);
}

void D3D12Context::SetScissorRect(std::uint32_t x, std::uint32_t y, std::uint32_t width,
                                  std::uint32_t height) const
{
    // The rect is what the GPU is allowed to write. Anything outside it is
    // clipped before the rasteriser reaches a pixel, which makes it strictly
    // stronger than the viewport's NDC-to-pixel box for content that must
    // never spill: use both.
    const D3D12_RECT rect{ static_cast<LONG>(x), static_cast<LONG>(y),
                           static_cast<LONG>(x + width), static_cast<LONG>(y + height) };
    commandList_->RSSetScissorRects(1, &rect);
}

void D3D12Context::EndFrame()
{
    // ---- 6c. transition the buffer back to compositor-owned ---------------
    //
    // Present requires the buffer in the PRESENT state, which the clear and any
    // draw calls in between have ended the buffer in RENDER_TARGET. The GPU
    // queue preserves command order, so this barrier retires the render target
    // writes that BeginFrame and the draw section submitted before the next
    // Present takes ownership.
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource   = currentBackBuffer_.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList_->ResourceBarrier(1, &barrier);

    // ---- 7. close: recording -> executable -------------------------------
    //
    // A command list has two states, and every method above only works in
    // RECORDING. Close is what makes the recorded commands submittable, and it
    // is also what makes the next BeginFrame's Reset legal.
    NOVA_THROW_IF_FAILED(commandList_->Close());

    // ---- 8. submit -------------------------------------------------------
    //
    // ExecuteCommandLists does not block and does not wait. It appends to the
    // queue and returns, which is exactly what lets the CPU run ahead - the
    // only thing that stops it is the fence wait at the top of BeginFrame.
    ID3D12CommandList* lists[] = { commandList_.Get() };
    commandQueue_->ExecuteCommandLists(1, lists);

    // ---- 9. present ------------------------------------------------------
    //
    // Present is queue work too, so the fence signal below covers it. Sync
    // interval 1 blocks on the vertical blank; 0 presents immediately and
    // tears. The interval is read per frame rather than folded into the swap
    // chain, because it is per-Present state - not because it is cheap to
    // change, which it is not.
    const UINT syncInterval = vsyncEnabled_ ? 1U : 0U;
    HandlePresentResult(swapChain_->Present(syncInterval, 0));

    // ---- 10. signal ------------------------------------------------------
    //
    // A fresh value every submission, never a counter compared against a
    // previous one. That is what makes the wait in BeginFrame a single
    // comparison instead of the subtly wrong "keep waiting while the completed
    // value equals the last one I saw" pattern.
    const std::uint64_t signalValue = ++nextFenceValue_;
    frameFenceValues_[frameIndex_] = signalValue;
    NOVA_THROW_IF_FAILED(commandQueue_->Signal(fence_.Get(), signalValue));

    // Advance the slot LAST, so the fence value recorded above belongs to the
    // slot that actually ran this frame. Advancing first would file the
    // signal under the wrong allocator and the wait two frames later would be
    // against the wrong work.
    frameIndex_ = (frameIndex_ + 1) % kFramesInFlight;
}

// ===========================================================================
//  Synchronisation
// ===========================================================================

void D3D12Context::WaitForFence(std::uint64_t fenceValue)
{
    // The fast path is not a micro-optimisation: on a fast GPU presenting a
    // mostly-empty frame, the fence has usually already advanced past the value
    // by the time the CPU gets here, and skipping the SetEventOnCompletion plus
    // kernel wait removes a context switch from every frame.
    if (fence_->GetCompletedValue() >= fenceValue)
    {
        return;
    }

    NOVA_THROW_IF_FAILED(fence_->SetEventOnCompletion(fenceValue, fenceEvent_.Get()));

    // INFINITE, and that is the correct choice rather than the lazy one. A
    // timeout needs a policy for exceeding it, and every such policy - log and
    // continue, retry, tear down - is worse than waiting: the GPU is not hung,
    // it is busy, and the alternative to waiting is either drawing into memory
    // the GPU is still reading or exiting with the answer in a log the user may
    // never open. A genuinely hung GPU is recovered by the user with the same
    // three keys either way.
    const DWORD waitResult = ::WaitForSingleObject(fenceEvent_.Get(), INFINITE);
    if (waitResult != WAIT_OBJECT_0)
    {
        // WAIT_FAILED only. WAIT_ABANDONED and WAIT_TIMEOUT cannot occur for an
        // auto-reset event with an infinite timeout, and treating an impossible
        // result as success is how a shutdown becomes a hang.
        //
        // Thrown as a runtime_error rather than funnelled through
        // NOVA_THROW_IF_FAILED: HRESULT_FROM_WIN32 maps a last error of 0 to
        // S_OK, so the "throw if failed" macro would silently succeed on the
        // one failure it exists to report.
        throw std::runtime_error("Fence wait failed: Win32 wait result " + std::to_string(waitResult) +
                                 ", last error " + std::to_string(::GetLastError()));
    }
}

void D3D12Context::WaitForGPU()
{
    // Signal a value NO submission will ever use, then wait for it. Because the
    // queue executes in order, reaching that value means every command queued
    // before it - including every Present - has completed. This is why the value
    // must be fresh rather than "the last one signalled": the last one may
    // already have been reached, which proves only that an earlier frame
    // finished, not this one.
    const std::uint64_t signalValue = ++nextFenceValue_;
    NOVA_THROW_IF_FAILED(commandQueue_->Signal(fence_.Get(), signalValue));
    WaitForFence(signalValue);
}

// ===========================================================================
//  Present result
// ===========================================================================

void D3D12Context::HandlePresentResult(HRESULT result)
{
    // Two status codes are NOT failures and are not even warnings. Treating
    // either as an error is a bug that looks like a robustness feature, because
    // the app throws and exits on perfectly normal user behaviour.
    if (result == DXGI_STATUS_OCCLUDED)
    {
        // Minimised, or entirely behind another window. The GPU did its job;
        // there was simply nothing on screen to put it on.
        return;
    }

    if (result == DXGI_STATUS_MODE_CHANGED)
    {
        // The window was resized between our check in BeginFrame and this
        // Present, so DXGI has already resized the buffers behind our back.
        // Zeroing the cached size is the whole handling: the next BeginFrame
        // compares it against the client area, sees a mismatch, and resizes to
        // whatever is now correct. Reading DXGI's description here instead
        // would duplicate state DXGI already has.
        backBufferWidth_  = 0;
        backBufferHeight_ = 0;
        NOVA_INFO("Swap chain mode changed; back buffers will be resized next frame");
        return;
    }

    if (FAILED(result))
    {
        // Almost every failure here is a device that has gone away. The reason
        // code is the difference between "the driver was upgraded under a
        // running process" and "your shaders read out of bounds", and it is
        // only available at this one moment - after a device removal the object
        // stays queryable but its state is frozen, so nothing later will report
        // it.
        NOVA_ERROR("Present failed. Device removed reason: 0x{:08X}",
                   static_cast<unsigned>(device_->GetDeviceRemovedReason()));

        // The debug layer's queue explains WHY. A removed device without its
        // messages is one line of hexadecimal and no way forward.
        DumpDebugMessages();

        NOVA_THROW_IF_FAILED(result);
    }
}

// ===========================================================================
//  Debug layer
// ===========================================================================

void D3D12Context::DumpDebugMessages() const
{
#if NOVA_D3D12_DEBUG_LAYER
    ComPtr<ID3D12InfoQueue> infoQueue;
    if (FAILED(device_->QueryInterface(IID_PPV_ARGS(&infoQueue))) || !infoQueue)
    {
        // Expected on a driver that does not implement the debug layer, and on
        // a runtime where Graphics Tools is not installed. Not itself an error.
        NOVA_INFO("D3D12 info queue unavailable; no debug messages to report");
        return;
    }

    // Severity filter, applied as a RETRIEVAL filter rather than a storage
    // filter: a storage filter would also discard the Info and Verbose records,
    // which are how the debug layer describes what it just did and are the
    // context for the error that follows. Pushed and popped so the change does
    // not leak into unrelated GetMessage calls.
    D3D12_INFO_QUEUE_FILTER filter{};
    filter.AllowList.NumSeverities = static_cast<UINT>(std::size(kReportedSeverities));
    filter.AllowList.pSeverityList = kReportedSeverities;
    NOVA_THROW_IF_FAILED(infoQueue->PushRetrievalFilter(&filter));

    const UINT64 messageCount = infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();
    for (UINT64 index = 0; index < messageCount; ++index)
    {
        DebugMessageStorage storage{};
        SIZE_T              bytes = sizeof(storage);
        if (FAILED(infoQueue->GetMessage(index, &storage.message, &bytes)))
        {
            continue;
        }

        const char* text = storage.message.pDescription != nullptr ? storage.message.pDescription : "";
        NOVA_ERROR("D3D12 debug: severity={} id={} - {}", static_cast<unsigned>(storage.message.Severity),
                   static_cast<unsigned>(storage.message.ID), text);
    }

    infoQueue->PopRetrievalFilter();

    // Drained rather than left in place. The queue stores a bounded number of
    // messages, and a stale error from a frame that has since been fixed would
    // then be printed again at the next failure, attributed to the wrong code.
    infoQueue->ClearStoredMessages();
#else
    // Compiled out entirely in Release: the interface does not exist without the
    // debug layer, so the alternative is a query that always fails on every
    // device removal for no benefit.
    static_cast<void>(this);
#endif
}

} // namespace Nova::Renderer