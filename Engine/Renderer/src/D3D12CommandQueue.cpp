// ===========================================================================
//  D3D12CommandQueue.cpp
// ---------------------------------------------------------------------------
//  The command queue, the fence, and the per-frame command objects.
//
//  WHAT THESE FOUR OBJECTS ARE FOR, AND WHY EACH EXISTS SEPARATELY
//  ---------------------------------------------------------------
//      ID3D12CommandQueue        A GPU-side scheduler. Commands are appended to
//                                it and executed in order. A DIRECT queue can
//                                do graphics AND compute; a COPY queue can only
//                                move memory, and moving uploads to one is how a
//                                renderer stops the graphics queue stalling on
//                                PCIe bandwidth.
//
//      ID3D12CommandAllocator    A block of GPU-visible memory that a command
//                                list writes into while recording. THE ONE
//                                OBJECT HERE THE GPU MAY NOT REUSE - see the
//                                wait at the top of BeginFrame.
//
//      ID3D12GraphicsCommandList The recorded commands themselves. One is
//                                enough, because only one can be recording at a
//                                time; it is reset against a different allocator
//                                each frame.
//
//      ID3D12Fence               A monotonically increasing 64-bit value the GPU
//                                writes and the CPU reads. This is the ONLY way
//                                the CPU learns the GPU finished, and so the only
//                                thing between "correct" and "reusing memory
//                                that is still being read".
//
//  WHY THERE IS A FENCE AT ALL, GIVEN THAT WAITING IS SLOW
//  -------------------------------------------------------
//  Because not waiting is not "faster", it is undefined. D3D12 is explicitly
//  synchronisation-free: nothing in the API blocks, nothing is implicitly
//  ordered against a previous submission, and every one of those must be
//  established by the application. A renderer with no fence is not a fast
//  renderer, it is one that has not corrupted memory yet - and it corrupts it
//  rarely, on a machine nobody is debugging, and never twice the same way.
// ===========================================================================

#include <Renderer/D3D12Context.h>

#include <Core/Log.h>

#include <stdexcept>
#include <string>

namespace Nova::Renderer
{
// ===========================================================================
//  Queue
// ===========================================================================

namespace
{
/// Creates the single DIRECT queue this context submits to.
void CreateQueue(ID3D12Device* device, ComPtr<ID3D12CommandQueue>& queue)
{
    // A queue describes what the GPU may be doing, and D3D12's rule is that the
    // type must be a SUPERSET of everything the queue will ever receive.
    //
    // DIRECT is that superset for a renderer: render targets, depth, and
    // compute. Choosing COPY or COMPUTE for speed is a false economy - the API
    // rejects a command the queue type does not cover, and it does so by
    // ignoring the command or removing the device, neither of which is faster.
    D3D12_COMMAND_QUEUE_DESC queueDescription{};
    queueDescription.Type     = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDescription.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;

    // No explicit node mask: 1 means "the adapter's single GPU node". It is
    // written explicitly rather than left zero-initialised because a zero mask
    // means NO node, which is not the same thing and fails at queue creation with
    // a message that does not mention the mask.
    queueDescription.NodeMask = 1;

    // No flags. D3D12_COMMAND_QUEUE_FLAG_DISABLE_GPU_TIMEOUT is for diagnostic
    // work - it removes the OS's few-second TDR timeout, turning a hang from a
    // device removal you can debug into a process you have to kill.
    NOVA_THROW_IF_FAILED(device->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(&queue)));
}

/// Creates the fence and the event used to wait on it.
void CreateFence(ID3D12Device*           device,
                 ComPtr<ID3D12Fence>&   fence,
                 WinHandle&             fenceEvent,
                 std::uint64_t&         nextFenceValue)
{
    // Initial value 0, meaning "the GPU has completed nothing yet". Every later
    // value comes from ++nextFenceValue, so the first wait in the first frame
    // finds nothing outstanding and returns immediately - which is correct, not
    // an off-by-one.
    constexpr UINT64 initialValue = 0;
    NOVA_THROW_IF_FAILED(device->CreateFence(initialValue, D3D12_FENCE_FLAG_NONE,
                                             IID_PPV_ARGS(&fence)));

    // An UNSIGNALED AUTO-RESET event.
    //
    // WHY AUTO-RESET RATHER THAN MANUAL-RESET: the fence's value is the source
    // of truth, not the event. Every waiter first checks GetCompletedValue and
    // only then waits, so two threads waiting for the same value cannot both be
    // released by one SetEventOnCompletion - the event only has to mean "it has
    // passed at least once since you last looked". A manual-reset event would
    // stay signalled afterwards and release every subsequent wait instantly,
    // which is a synchronisation bug that appears only on a slow frame.
    //
    // bInitialState FALSE: the fence has completed nothing at this point, and
    // an event claiming otherwise would make the first BeginFrame's wait return
    // before the GPU started.
    fenceEvent.Reset(::CreateEventW(nullptr, FALSE, FALSE, nullptr));
    if (!fenceEvent)
    {
        // CreateEventW failing means the process is out of kernel handles - a
        // leak elsewhere, or a very low process limit. Nothing here is
        // recoverable, and a context with no event cannot synchronise, so this
        // has to be fatal rather than a warning.
        throw std::runtime_error("CreateEventW failed for the D3D12 fence (Win32 error " +
                                 std::to_string(::GetLastError()) + ")");
    }

    nextFenceValue = initialValue;
}
} // namespace

void D3D12Context::CreateCommandInfrastructure()
{
    CreateQueue(device_.Get(), commandQueue_);
    CreateFence(device_.Get(), fence_, fenceEvent_, nextFenceValue_);

    // ---- per-frame command allocators ------------------------------------
    //
    // One per frame slot, allocated ONCE at startup and reused for the process
    // lifetime. This is not an oversight:
    //
    //  - Resetting an allocator is what makes reuse cheap; freeing and creating
    //    a new one every frame would make the GPU pipeline stall behind a
    //    fresh allocation each time.
    //  - Allocating from a descriptor heap or upload heap would fragment it
    //    continuously, and a GPU-visible heap that cannot grow is a renderer
    //    that stops loading levels after a few.
    //
    // Reserving exactly kFramesInFlight of them is the point of the whole
    // frame-slot design: the allocator a frame uses must still be reserved and
    // idle until that frame's fence completes, which is checked at the top of
    // BeginFrame.
    frameAllocators_.reserve(kFramesInFlight);
    for (std::uint32_t slot = 0; slot < kFramesInFlight; ++slot)
    {
        ComPtr<ID3D12CommandAllocator> frameAllocator;
        NOVA_THROW_IF_FAILED(
            device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                             IID_PPV_ARGS(&frameAllocator)));
        frameAllocators_.push_back(std::move(frameAllocator));
    }

    // ---- the single command list -----------------------------------------
    //
    // Created against slot 0's allocator purely because the API requires one;
    // every later Reset names the slot actually in use.
    //
    // NodeType 0 is the single-GPU case, matching the queue's node mask. A
    // machine with more than one GPU is a different adapter, not a different
    // node, and D3D12 nodes are for linked adapters on one board.
    NOVA_THROW_IF_FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                    frameAllocators_[0].Get(), nullptr,
                                                    IID_PPV_ARGS(&commandList_)));

    // THE STEP EVERY TUTORIAL OMITS. A newly created command list is in the
    // RECORDING state, and Reset is only legal on a CLOSED list. Without this
    // Close, the first BeginFrame's Reset fails with
    // D3D12_ERROR_INVALID_CALL - on the very first frame, with a message that
    // does not mention Close at all.
    NOVA_THROW_IF_FAILED(commandList_->Close());

    NOVA_INFO("D3D12 command queue ready: {} frames in flight, {} swap chain buffers",
              kFramesInFlight, kSwapBufferCount);
}

} // namespace Nova::Renderer