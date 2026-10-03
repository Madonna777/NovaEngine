// ===========================================================================
//  Platform.cpp
// ---------------------------------------------------------------------------
//  Win32 implementation of the OS-facing half of Platform.h: memory, timing,
//  thread identity and scheduling hints. CPU capability detection lives in
//  CpuInfo.cpp.
//
//  RULE ENFORCED HERE: <Windows.h> is confined to .cpp files across the whole
//  engine. It defines min/max macros that break std::numeric_limits and any
//  template using min(), plus TRUE/FALSE and a CreateFile macro that collide
//  with our own naming. Isolating it costs nothing and prevents an entire
//  class of build breakage.
//
//  ALSO NOTE THE FULLY-QUALIFIED CALLS below (::Sleep, ::GetCurrentThreadId).
//  Several of our Platform functions deliberately share a name with their Win32
//  counterpart. An unqualified call from inside namespace Nova::Platform would
//  resolve to OUR function, not the kernel's, and silently recurse until the
//  stack overflows. :: forces the global one.
//
//  NAMES MUST ALSO DIFFER FROM WIN32 MACROS, which is a separate hazard - one
//  the compiler reports at a place with no apparent connection to the cause.
//  See the note on Platform::CpuRelax.
// ===========================================================================

#include <Core/Platform.h>
#include <Core/Assert.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN  // Excludes winsock.h, mmsystem.h - speeds the build
#endif
#ifndef NOMINMAX
#define NOMINMAX            // Removes min()/max() macros
#endif

#include <Windows.h>
#include <psapi.h> // PROCESS_MEMORY_COUNTERS_EX
#include <string_view>

namespace Nova::Platform
{
MemoryInfo GetMemoryInfo()
{
    MemoryInfo memory{};

    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (::GlobalMemoryStatusEx(&status))
    {
        memory.totalBytes     = status.ullTotalPhys;
        memory.availableBytes = status.ullAvailPhys;
    }

    // NOTE: dwPageSize comes from SYSTEM_INFO, not MEMORYSTATUSEX. The older
    // MEMORYSTATUS struct has dwPageSize; MEMORYSTATUSEX does not - a detail
    // that is easy to assume wrongly.
    SYSTEM_INFO systemInfo{};
    ::GetSystemInfo(&systemInfo);
    memory.pageSize = systemInfo.dwPageSize;

    // The K32-prefixed export lives in Kernel32.dll. Using it avoids linking
    // psapi.lib purely for one call - K32 variants exist precisely so that
    // modern callers do not need the legacy psapi import library.
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (::K32GetProcessMemoryInfo(::GetCurrentProcess(),
                                  reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                                  sizeof(counters)))
    {
        // PrivateUsage is the commit charge: pages reserved AND committed.
        memory.processCommittedBytes  = counters.PrivateUsage;
        memory.processWorkingSetBytes = counters.WorkingSetSize;
    }

    return memory;
}

double GetHighResolutionSeconds()
{
    static const std::uint64_t frequency = [] {
        LARGE_INTEGER value{};
        ::QueryPerformanceFrequency(&value);
        return static_cast<std::uint64_t>(value.QuadPart);
    }();

    LARGE_INTEGER counter{};
    ::QueryPerformanceCounter(&counter);

    // Split the division: quotient for whole seconds, remainder for the
    // fraction. As one float expression the intermediate would exceed 2^53
    // and lose sub-microsecond precision for the first few seconds of uptime.
    const auto ticks = static_cast<std::uint64_t>(counter.QuadPart);
    return static_cast<double>(ticks / frequency) +
           static_cast<double>(ticks % frequency) / static_cast<double>(frequency);
}

std::uint64_t GetHighResolutionFrequency()
{
    LARGE_INTEGER value{};
    ::QueryPerformanceFrequency(&value);
    return static_cast<std::uint64_t>(value.QuadPart);
}

std::uint32_t GetCurrentThreadId()
{
    return static_cast<std::uint32_t>(::GetCurrentThreadId());
}

std::string_view GetPlatformName()
{
    return "Windows";
}

void CpuRelax()
{
    // Safe to spell YieldProcessor here even though winnt.h defines it as a
    // macro for _mm_pause: the qualified :: guarantees we get the intrinsic
    // expansion rather than accidentally recursing into our own function, which
    // is the shadowing trap described in the file header.
    ::YieldProcessor();
}

void Sleep(std::uint32_t milliseconds)
{
    ::Sleep(milliseconds);
}

} // namespace Nova::Platform