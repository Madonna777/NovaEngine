// ===========================================================================
//  D3D12Device.cpp
// ---------------------------------------------------------------------------
//  DXGI factory, adapter selection, and ID3D12Device creation.
//
//  WHAT "CREATING A D3D12 DEVICE" ACTUALLY MEANS
//  ---------------------------------------------
//  Three steps, and only the middle one sounds like "device creation":
//
//    1. CreateDXGIFactory  - the entry point into the graphics stack. DXGI owns
//       the swap chain, adapter enumeration and monitor modes; D3D12 itself
//       knows nothing about any of it. The factory is also where a vendor
//       driver's overlay and validation hooks in, so it is created first and
//       released last.
//
//    2. Adapter selection  - a POLICY decision, not a lookup. Passing nullptr
//       to D3D12CreateDevice and taking whatever Windows picks is what every
//       sample does, and it is the reason a game sometimes opens on the
//       integrated GPU of a laptop that has a discrete one.
//
//    3. D3D12CreateDevice   - with a chosen adapter and a minimum feature
//       level. Windows may hand back a device for a DIFFERENT adapter than
//       requested: a driver can refuse an adapter and substitute its own
//       implementation. Nothing here can prevent that, which is why the
//       adapter is read back off the created device rather than assumed.
//
//  WHY THE FACTORY USES A DEBUG FLAG IN DEBUG BUILDS
//  -------------------------------------------------
//  DXGI_CREATE_FACTORY_DEBUG routes DXGI and D3D12 calls through a validating
//  implementation that reports misuse through the info queue. It is a
//  build-selected behaviour, not a runtime tax a shipping build pays - the
//  release factory contains no validation at all. The cost when it IS enabled
//  is real, though, which is why it is a separate define from NDEBUG: see the
//  comment on NOVA_D3D12_DEBUG_LAYER.
// ===========================================================================

#include <Renderer/D3D12Context.h>

#include <Core/Log.h>

#include <cstdint>
#include <iterator>

namespace Nova::Renderer
{
namespace
{
/// Creates the DXGI factory, enabling validation where this build wants it.
void CreateFactory(ComPtr<IDXGIFactory4>& factory)
{
#if NOVA_D3D12_DEBUG_LAYER
    // First attempt: with the debug factory flag.
    //
    // WHY A FALLBACK AND NOT A HARD FAILURE: the debug factory's DLL - and
    // ID3D12Debug itself - is the "Graphics Tools" Windows optional feature,
    // which is NOT installed on a clean Windows image and on most developer
    // machines. Passing DXGI_CREATE_FACTORY_DEBUG without it makes
    // CreateDXGIFactory2 fail outright with DXGI_ERROR_INVALID_CALL, so a Debug
    // build that assumes the flag works would not even start on a stock
    // install. Falling back to a non-debug factory keeps the engine running and
    // keeps the missing layer loud rather than catastrophic.
    if (SUCCEEDED(::CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&factory))))
    {
        // The factory being available does not mean the debug controller is:
        // EnableDebugLayer can still fail on a driver that has the factory DXGI
        // side but not the ID3D12Debug one. Both halves are logged so the log
        // answers "is validation on?" without reading the SDK.
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(::D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))) && debugController)
        {
            debugController->EnableDebugLayer();
            NOVA_INFO("D3D12 debug layer enabled");
        }
        else
        {
            NOVA_WARN("D3D12 debug controller unavailable (Graphics Tools may not be installed); "
                      "D3D12 calls will run unvalidated");
        }
        return;
    }

    NOVA_WARN("IDXGI debug factory creation failed; falling back to a non-debug factory. "
              "Install the Graphics Tools optional feature to get D3D12 validation.");
#endif

    // IDXGIFactory4 rather than an older revision: the adapter-enumeration and
    // swap-chain interfaces this module needs were added across several DXGI
    // revisions, and asking for the highest costs nothing - every adapter the
    // OS can enumerate implements it.
    NOVA_THROW_IF_FAILED(::CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)));
}

/// Reads the description off the adapter used to create the device.
///
/// WHY THE ADAPTER AND NOT AN IDXGIDevice QUERY: an ID3D12Device does NOT
/// implement IDXGIDevice and has no GetAdapterLUID, which means the obvious
/// route - QI to IDXGIDevice, then its GetAdapter - yields E_NOINTERFACE. (That
/// was the first version of this code, and it logged a warning on every start.)
/// The description of the adapter we handed D3D12CreateDevice is the one the
/// device will render on, which is the one anyone reading a bug report wants.
void ReadAdapterDescription(IDXGIAdapter1* adapter, DXGI_ADAPTER_DESC1& description)
{
    if (FAILED(adapter->GetDesc1(&description)))
    {
        NOVA_WARN("Could not query the adapter's description; adapter name and VRAM unknown");
        description = DXGI_ADAPTER_DESC1{};
    }
}
} // namespace

// ===========================================================================
//  Factory, adapter and device
// ===========================================================================

void D3D12Context::CreateFactoryAndDevice()
{
    CreateFactory(factory_);

    const std::uint32_t adapterIndex = SelectAdapter(factory_.Get());

    // WHY THE ADAPTER COM OBJECT IS NOT RETURNED FROM SelectAdapter: enumeration
    // produces a COM reference per adapter visited, and on a machine with a
    // stack of virtual display adapters - each one loading a kernel driver -
    // holding a dozen alive for the lifetime of the context is a real cost. The
    // index is the cheap thing to keep.
    DXGI_ADAPTER_DESC1 adapterDescription{};

    if (adapterIndex != kNoAdapter)
    {
        ComPtr<IDXGIAdapter1> adapter;
        NOVA_THROW_IF_FAILED(factory_->EnumAdapters1(adapterIndex, &adapter));
        NOVA_THROW_IF_FAILED(
            ::D3D12CreateDevice(adapter.Get(), kMinimumFeatureLevel, IID_PPV_ARGS(&device_)));
        ReadAdapterDescription(adapter.Get(), adapterDescription);
    }
    else
    {
        // No hardware adapter, or every adapter failed to enumerate. nullptr
        // means "the default adapter", which on such a machine is WARP - the
        // software rasteriser. Slow, and missing many features, but correct, so
        // a machine with no usable GPU gets a working window instead of a
        // startup failure. This is the better default than failing: an engine
        // that refuses to start without a discrete GPU also refuses to start on
        // every CI agent and every VM.
        NOVA_WARN("No suitable hardware adapter found; falling back to the default "
                  "(likely software) adapter");
        NOVA_THROW_IF_FAILED(
            ::D3D12CreateDevice(nullptr, kMinimumFeatureLevel, IID_PPV_ARGS(&device_)));

        // WARP's description is purely informational, but "adapter created"
        // without it would log a zeroed name on every headless run.
        for (UINT index = 0;; ++index)
        {
            ComPtr<IDXGIAdapter1> candidate;
            if (factory_->EnumAdapters1(index, &candidate) != S_OK)
            {
                break;
            }

            DXGI_ADAPTER_DESC1 candidateDescription{};
            if (SUCCEEDED(candidate->GetDesc1(&candidateDescription)) &&
                (candidateDescription.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
            {
                adapterDescription = candidateDescription;
                break;
            }
        }
    }

    adapterName_                = ToUtf8(adapterDescription.Description);
    dedicatedVideoMemoryBytes_ = static_cast<std::uint64_t>(adapterDescription.DedicatedVideoMemory);

    // Logging the adapter here rather than from the caller means every D3D12
    // context records which GPU it got. That single line answers the first
    // question asked of almost every rendering bug report - "which GPU?" - and
    // the two below it answer the ones that follow.
    NOVA_INFO("D3D12 device: {}", adapterName_.empty() ? "<unknown adapter>" : adapterName_);
    NOVA_INFO("  dedicated VRAM: {} MiB", dedicatedVideoMemoryBytes_ / (1024ULL * 1024ULL));
    NOVA_INFO("  software adapter: {}",
              (adapterDescription.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0 ? "yes" : "no");

    LogDeviceCapabilities();
}

// ===========================================================================
//  Capabilities
// ===========================================================================

void D3D12Context::LogDeviceCapabilities()
{
    // NOTE THE API SHAPE, which is the classic D3D12 trap: the struct is an
    // IN-out parameter and there is no out array. You pass the list of levels
    // you can accept, and the device answers with the single best level from
    // that list it supports - MaxSupportedFeatureLevel. An earlier version of
    // this code imagined an out array and filled in levels the device never
    // reported, which is exactly the bug the compiler caught by refusing to
    // compile it.
    const D3D_FEATURE_LEVEL requested[] = { D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1,
                                            D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1,
                                            D3D_FEATURE_LEVEL_11_0 };

    D3D12_FEATURE_DATA_FEATURE_LEVELS levels{};
    levels.NumFeatureLevels       = static_cast<UINT>(std::size(requested));
    levels.pFeatureLevelsRequested = requested;

    if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &levels,
                                               sizeof(levels))))
    {
        switch (levels.MaxSupportedFeatureLevel)
        {
        case D3D_FEATURE_LEVEL_12_2: NOVA_INFO("  feature level: 12.2"); break;
        case D3D_FEATURE_LEVEL_12_1: NOVA_INFO("  feature level: 12.1"); break;
        case D3D_FEATURE_LEVEL_12_0: NOVA_INFO("  feature level: 12.0"); break;
        case D3D_FEATURE_LEVEL_11_1: NOVA_INFO("  feature level: 11.1"); break;
        case D3D_FEATURE_LEVEL_11_0: NOVA_INFO("  feature level: 11.0"); break;
        default:
            NOVA_INFO("  feature level: unknown ({})",
                      static_cast<unsigned>(levels.MaxSupportedFeatureLevel));
            break;
        }
    }
    else
    {
        NOVA_WARN("Feature level query failed; the driver may be a preview build");
    }

    // Resource Binding Tier decides how large a shader-visible descriptor heap
    // may be, and therefore whether bindless rendering is possible at all. It
    // constrains several later architecture decisions, so having it in the log
    // before someone asks why a binding scheme fails on their machine is worth
    // one more query at startup.
    D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
    if (SUCCEEDED(
            device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))))
    {
        NOVA_INFO("  resource binding tier: {}", static_cast<unsigned>(options.ResourceBindingTier));
    }

    // GPU-based validation is deliberately NOT enabled. It replays every
    // submitted command list through a validation shader and costs on the order
    // of a millisecond per draw - more than everything else in a renderer
    // combined. It is the right tool for a bisecting session on a suspected
    // synchronisation bug, and the wrong default for anything measured.
}

// ===========================================================================
//  Adapter selection
// ===========================================================================

std::uint32_t D3D12Context::SelectAdapter(IDXGIFactory4* factory)
{
    DXGI_ADAPTER_DESC1 best{};
    std::uint32_t      bestIndex = kNoAdapter;

    for (UINT index = 0; factory != nullptr; ++index)
    {
        ComPtr<IDXGIAdapter1> adapter;

        // DXGI_ERROR_NOT_FOUND ends the list. Testing for S_OK rather than "not
        // FAILED" also rejects a genuine failure partway through, which is the
        // right call - a partial adapter list is not a safe basis for choosing
        // a GPU, and silently ranking what is left would hide the problem.
        if (factory->EnumAdapters1(index, &adapter) != S_OK)
        {
            break;
        }

        DXGI_ADAPTER_DESC1 description{};
        if (FAILED(adapter->GetDesc1(&description)))
        {
            continue;
        }

        // Software adapters are EXCLUDED rather than ranked last. WARP reports
        // zero dedicated video memory so a "highest VRAM wins" rule would skip
        // it anyway - but the Microsoft Basic Render Driver installed on a
        // machine with no vendor driver reports a large system-memory figure
        // and CAN win a naive comparison, turning the engine into a slideshow.
        // Excluding the whole DXGI_ADAPTER_FLAG_SOFTWARE class states the intent
        // directly instead of relying on a coincidence.
        if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
        {
            continue;
        }

        // DedicatedVideoMemory is the ranking key, which is the "highest VRAM"
        // heuristic and it is deliberate.
        //
        // WHY NOT "IS IT DISCRETE": an integrated adapter has no dedicated pool
        // and reports 0, so every discrete GPU outranks it - which is the right
        // answer for a game engine, and the only one expressible without
        // knowing a laptop's display wiring.
        //
        // THE CAVEAT IS REAL: on a hybrid laptop whose integrated GPU drives the
        // internal panel, presenting on the discrete GPU costs an inter-adapter
        // copy per frame, so "most VRAM" is not always "best to render on".
        // IDXGIFactory1::IsCurrent answers the better question - which adapter
        // owns the output the window is on - and is deliberately left as the
        // next refinement rather than half-implemented here, where a wrong
        // answer would look like a bug rather than a policy.
        if (bestIndex == kNoAdapter ||
            description.DedicatedVideoMemory > best.DedicatedVideoMemory)
        {
            best      = description;
            bestIndex = index;
        }
    }

    return bestIndex;
}

} // namespace Nova::Renderer