// ===========================================================================
//  Sandbox - Core foundation smoke test
// ---------------------------------------------------------------------------
//  Purpose: prove the toolchain, the logger, the platform layer and the handle
//  registry all work, before any of them are load-bearing for a renderer.
//
//  WHY THIS FILE IS DELIBERATELY DUMB
//  ----------------------------------
//  It exercises each subsystem in isolation and prints what it finds. No
//  abstractions, no clever helpers. When the first D3D12 swapchain lands, this
//  file becomes the control group: if SandboxConsole still prints correctly,
//  Core is healthy and any rendering failure is genuinely in the renderer.
// ===========================================================================

#include <Core/Log.h>
#include <Core/Platform.h>
#include <Core/Assert.h>
#include <Core/HandleRegistry.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace
{
// Distinct tag types. See below for why each entity kind needs its own.
struct EntityTag;
struct DebugTag;

/// Payload stored for an Entity. Deliberately trivial: the point is the handle
/// semantics, not the component data.
struct Entity
{
    std::string name;
    float       health = 100.0f;
};

/// Proves the type-safety half of the handle system.
///
/// @note Both registries are instantiated with the SAME template arguments
///       apart from the tag, so the only thing separating them is the tag type.
///       This is the check that a Handle<EntityTag> cannot be passed to a
///       registry of Handle<DebugTag> - the compiler rejects it outright, which
///       is the whole point.
void DemoHandleRegistry()
{
    NOVA_CLIENT_INFO("--- Handle registry ---");

    Nova::HandleRegistry<EntityTag, Entity> entities;
    entities.Reserve(1024); // One allocation for the whole batch.

    const Nova::Handle<EntityTag> player = entities.Emplace("Player", 100.0f);
    const Nova::Handle<EntityTag> orc    = entities.Emplace("Orc", 55.0f);

    NOVA_CLIENT_INFO("created {} entities, capacity {} slots",
              entities.AliveCount(), entities.Capacity());

    NOVA_VERIFY(entities.IsAlive(player));
    NOVA_VERIFY(entities.IsAlive(orc));

    // A stale-handle check that does not rely on asserts: if generation
    // tracking were broken, Destroy would report success and this lookup would
    // return nullptr - a detectable bug rather than a crash.
    const bool destroyed = entities.Destroy(player);
    NOVA_VERIFY_MSG(destroyed, "Destroy reported failure for a live handle");

    NOVA_CLIENT_INFO("destroyed '{}'; alive count now {}",
              player.ToString(), entities.AliveCount());

    // THE INTERESTING PART: 'player' is still a set, non-null, 64-bit value.
    // It is simply no longer valid, and the registry says so. Without
    // generation counters this is the exact use-after-free a game loop hits.
    NOVA_CLIENT_INFO("stale handle {} -> IsAlive={} (expected false), TryGet={}",
              player.ToString(),
              entities.IsAlive(player),
              entities.TryGet(player) == nullptr ? "nullptr (expected)" : "LEAKED");

    NOVA_VERIFY_MSG(!entities.IsAlive(player),
                    "generation tracking failed: destroyed handle still reports alive");
    NOVA_VERIFY_MSG(entities.TryGet(player) == nullptr,
                    "generation tracking failed: destroyed handle resolved to data");

    // Destroying twice must be safe: gameplay code and a deferred death timer
    // can both decide an entity is dead in the same frame.
    const bool doubleDestroy = entities.Destroy(player);
    NOVA_CLIENT_INFO("double destroy returned {} (expected false, must be idempotent)",
              doubleDestroy);

    // Slot reuse: the freed index should come back for the next allocation, and
    // the new entity must get a DIFFERENT generation so it cannot be confused
    // with the old one.
    const Nova::Handle<EntityTag> replacement = entities.Create();
    NOVA_CLIENT_INFO("recycled slot: old={} new={} sameIndex={} sameGeneration={}",
              player.ToString(), replacement.ToString(),
              player.GetIndex() == replacement.GetIndex(),
              player.GetGeneration() == replacement.GetGeneration());

    NOVA_VERIFY_MSG(player.GetIndex() == replacement.GetIndex(),
                    "free slot was not recycled - sparse array would grow unbounded");
    NOVA_VERIFY_MSG(player.GetGeneration() != replacement.GetGeneration(),
                    "recycled slot reused the generation - stale handles would alias");
    NOVA_VERIFY_MSG(!entities.IsAlive(player),
                    "old handle became valid again after slot reuse");

    // Dense iteration: only live instances, in storage order.
    NOVA_CLIENT_INFO("iterating live entities:");
    entities.ForEach([](Nova::Handle<EntityTag> handle, Entity& entity) {
        NOVA_CLIENT_INFO("  {} -> '{}' (health {:.0f})", handle.ToString(), entity.name, entity.health);
    });

    NOVA_CLIENT_INFO("alive count {} / capacity {}", entities.AliveCount(), entities.Capacity());
}

/// Proves the tag-based type separation compiles.
/// @note This function compiles only because Handle<EntityTag> and
///       Handle<DebugTag> are unrelated types. Remove the second registry and
///       the whole file still compiles; mix the two up and it does not.
void DemoTypeSeparation()
{
    Nova::HandleRegistry<EntityTag, Entity> entities;
    Nova::HandleRegistry<DebugTag, float>  debugValues;

    const auto entity = entities.Create();
    const auto debug  = debugValues.Create();

    // static_assert on the underlying values differing is not the interesting
    // part; the interesting part is that the following line is a COMPILE ERROR
    // and would be if we attempted it:
    //
    //     entities.IsAlive(debug);        // error C2679: no implicit conversion
    //
    // which is exactly the mistake we want a compiler to catch.
    NOVA_VERIFY(entities.IsAlive(entity));
    NOVA_VERIFY(debugValues.IsAlive(debug));
    NOVA_CLIENT_INFO("entity handle and debug handle are distinct types (compile-time guarantee)");
}

/// Reports host hardware. Timing accuracy is the part worth checking: if
/// GetHighResolutionSeconds were backed by GetTickCount64, these numbers would
/// read as 0.000 or 0.015, and frame profiling built on them would be worthless.
void DemoPlatform()
{
    NOVA_CLIENT_INFO("--- Platform ---");

    const auto cpu = Nova::Platform::GetCpuInfo();
    NOVA_CLIENT_INFO("CPU    : {} ({} physical / {} logical cores, widest SIMD {} bit)",
              cpu.vendor, cpu.physicalCoreCount, cpu.logicalCoreCount,
              cpu.largestSimdWidthInBits);
    NOVA_CLIENT_INFO("features: SSE2={} AVX={} FMA={} AVX2={}",
              cpu.hasSse2, cpu.hasAvx, cpu.hasFma, cpu.hasAvx2);

    const auto memory = Nova::Platform::GetMemoryInfo();
    NOVA_CLIENT_INFO("memory : {:.1f} GiB total, {:.1f} GiB available",
              static_cast<double>(memory.totalBytes) / (1024.0 * 1024.0 * 1024.0),
              static_cast<double>(memory.availableBytes) / (1024.0 * 1024.0 * 1024.0));
    NOVA_CLIENT_INFO("process: {:.2f} MiB committed, {:.2f} MiB working set, {}-byte pages",
              static_cast<double>(memory.processCommittedBytes) / (1024.0 * 1024.0),
              static_cast<double>(memory.processWorkingSetBytes) / (1024.0 * 1024.0),
              memory.pageSize);

    // Timer resolution probe.
    //
    // WHY SPIN UNTIL THE COUNTER TICKS: measuring two back-to-back reads tells
    // you nothing. Two QueryPerformanceCounter calls take ~50 ns, and this
    // machine's counter ticks far more slowly than that, so every sample
    // returns the same value and the naive "smallest non-zero delta" is never
    // populated. Instead we busy-wait for the counter to actually advance and
    // report how long that took - which IS the resolution.
    constexpr int kSamples       = 200;
    constexpr int kSpinLimit     = 8192; // Bounds the loop if the timer is broken.
    std::uint64_t smallestDeltaNs = ~std::uint64_t{0};

    const double start = Nova::Platform::GetHighResolutionSeconds();
    for (int sample = 0; sample < kSamples; ++sample)
    {
        const double t0 = Nova::Platform::GetHighResolutionSeconds();
        double       t1 = t0;

        for (int spin = 0; spin < kSpinLimit && t1 == t0; ++spin)
        {
            Nova::Platform::CpuRelax();
            t1 = Nova::Platform::GetHighResolutionSeconds();
        }

        if (t1 > t0)
        {
            smallestDeltaNs = std::min(smallestDeltaNs,
                                       static_cast<std::uint64_t>((t1 - t0) * 1'000'000'000.0));
        }
    }
    const double elapsed = Nova::Platform::GetHighResolutionSeconds() - start;

    if (smallestDeltaNs != ~std::uint64_t{0})
    {
        NOVA_CLIENT_INFO("timer  : {} Hz, resolution ~{} ns, {} samples in {:.3f} ms",
                  Nova::Platform::GetHighResolutionFrequency(), smallestDeltaNs, kSamples,
                  elapsed * 1000.0);
    }
    else
    {
        // Reported explicitly rather than printing a sentinel value: a bare
        // 18446744073709551615 in a log reads like a real measurement.
        NOVA_CLIENT_WARN("timer  : {} Hz - counter did not advance in {} samples; "
                  "frame timing will not be trustworthy",
                  Nova::Platform::GetHighResolutionFrequency(), kSamples);
    }
}

/// Demonstrates that a Win32 error message survives the round trip. Cheap, and
/// it catches the encoding problems that otherwise appear only as unreadable
/// text in a log file weeks later.
void DemoErrorFormatting()
{
    NOVA_CLIENT_INFO("--- Diagnostics ---");

    NOVA_CLIENT_INFO("GetLastErrorMessage(5)  = '{}'", Nova::Platform::GetLastErrorMessage(5));
    NOVA_CLIENT_INFO("GetLastErrorMessage(2)  = '{}'", Nova::Platform::GetLastErrorMessage(2));
    NOVA_CLIENT_INFO("bogus code 0x8000BEEF = '{}'",
              Nova::Platform::GetLastErrorMessage(0x8000BEEF));

    // A deliberately caught failure: proves the exception machinery and the
    // assert macros are wired up. In a release build this still runs - Nova
    // asserts are not compiled out.
    try
    {
        throw std::runtime_error("expected failure - exception plumbing is working");
    }
    catch (const std::exception& error)
    {
        NOVA_CLIENT_INFO("caught expected exception: {}", error.what());
    }

    // NOVA_VERIFY with a side effect: because it always evaluates, the counter
    // reaches 3. An assert() here would have compiled the increment away.
    int sideEffectCount = 0;
    NOVA_VERIFY(++sideEffectCount == 1);
    NOVA_VERIFY(++sideEffectCount == 2);
    NOVA_VERIFY(++sideEffectCount == 3);
    NOVA_CLIENT_INFO("NOVA_VERIFY evaluated a side effect {} times (expected 3)", sideEffectCount);
}

/// Demonstrates that the two log channels route independently.
///
/// WHY THIS NEEDS A RUNTIME CHECK RATHER THAN A COMMENT: the channels share a
/// single sink instance, and the channel tag is produced by the sink's pattern
/// from the logger's NAME. A bug in this area does not crash, does not lose a
/// line, and does not fail to build - it produces a log that looks perfectly
/// healthy while attributing every engine message to the game. The only way to
/// catch that is to emit on both channels and read the tags back.
void DemoLogChannels()
{
    NOVA_CLIENT_INFO("--- Log channels ---");

    // One record per channel. Applied correctly these carry different tags;
    // with logger->set_pattern used instead of sink->set_pattern both carry the
    // SAME tag, which is exactly the bug this demo exists to catch.
    NOVA_INFO("engine-side record, emitted with NOVA_INFO");
    NOVA_CLIENT_INFO("client-side record, emitted with NOVA_CLIENT_INFO");

    NOVA_CLIENT_INFO("Core logger   : name='{}' minLevel={}",
                     Nova::Log::Channel(Nova::LogChannel::Core).name(),
                     Nova::LogLevelName(
                         Nova::Log::ChannelLevel(Nova::LogChannel::Core)));
    NOVA_CLIENT_INFO("Client logger : name='{}' minLevel={}",
                     Nova::Log::Channel(Nova::LogChannel::Client).name(),
                     Nova::LogLevelName(
                         Nova::Log::ChannelLevel(Nova::LogChannel::Client)));

    // The level filters are per channel, so silencing one must not silence the
    // other. That independence is the entire reason for two loggers instead of
    // one logger with a category field, so it is worth proving, not asserting.
    Nova::Log::SetChannelLevel(Nova::LogChannel::Core, Nova::LogLevel::Off);
    NOVA_INFO("this Core record is suppressed and must NOT appear");
    NOVA_CLIENT_INFO("Client is unaffected by the Core filter (as expected)");

    Nova::Log::SetChannelLevel(Nova::LogChannel::Core, Nova::LogLevel::Debug);
    NOVA_INFO("Core is visible again after restoring its level");
}
} // namespace

int main()
{
    // Console encoding is configured before anything can log. On a non-English
    // locale the default console code page mangles any non-ASCII text, which
    // would turn every localised error message into noise - exactly when you
    // most need to read one.
    Nova::Platform::EnableUtf8Console();

    // Logging is initialised first, before any other engine subsystem, so that
    // failures during the rest of startup are recorded rather than lost. The
    // log file goes next to the executable; if it cannot be opened, Initialize
    // returns false and logging continues to the console.
    const bool loggingReady =
        Nova::Log::Initialize(
            Nova::LogLevel::Debug,
            std::filesystem::path{"nova_sandbox.log"},
            /*consoleOutput=*/true,
            /*flushOnError=*/true);

    if (!loggingReady)
    {
        std::printf("[Sandbox] logging fell back to console only\n");
    }

    NOVA_CLIENT_CRITICAL("NovaEngine Sandbox - Core foundation check");
    NOVA_CLIENT_INFO("platform={} thread={} loggingInitialised={}",
              Nova::Platform::GetPlatformName(),
              Nova::Platform::GetCurrentThreadId(),
              Nova::Log::IsInitialized());

    DemoPlatform();
    DemoHandleRegistry();
    DemoTypeSeparation();
    DemoErrorFormatting();
    DemoLogChannels();

    // Spaced box so the final summary is greppable in a log file.
    NOVA_CLIENT_INFO("============================================================");
    NOVA_CLIENT_INFO(" Core foundation OK");
    NOVA_CLIENT_INFO("============================================================");

    // Explicit shutdown so buffered records reach disk while the exit code is
    // still ours. The ExitFlush guard in Log.cpp is a backstop, not the plan.
    Nova::Log::Shutdown();
    return 0;
}