// ===========================================================================
//  CpuInfo.cpp
// ---------------------------------------------------------------------------
//  CPU capability and topology detection: the GetCpuInfo() half of Platform.h.
//
//  WHY THIS IS SPLIT OUT OF Platform.cpp
//  -------------------------------------
//  CPU detection is a self-contained concern with its own vocabulary - CPUID
//  leaves, XCR0 register bits, variable-length topology records - and it uses a
//  different set of headers (intrin.h, immintrin.h) than the OS queries do
//  (psapi.h). Keeping them apart means:
//    * the x86 intrinsics headers are parsed by one translation unit, not by
//      everything that merely wants to know how much RAM the machine has;
//    * a reader looking for "how does the engine count physical cores" finds one
//      short file rather than navigating past memory and timing queries;
//    * the genuinely risky code (see the AVX/XCR0 notes below) is isolated in a
//      file you can review on its own.
//
//  WHAT IS IN HERE: everything that answers "what can this CPU do". Memory,
//  timing and threading queries live in Platform.cpp.
// ===========================================================================

#include <Core/Platform.h>
#include <Core/Assert.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <intrin.h>    // __cpuid, __cpuidex
#include <immintrin.h>  // _xgetbv - lives here on MSVC, not in intrin.h

#include <array>
#include <cstring> // std::memcpy, for extracting the CPUID vendor string
#include <string_view>
#include <vector>

namespace Nova::Platform
{
namespace
{
/// Reads the CPUID vendor string into a caller-provided buffer.
///
/// @note MSVC fills the array as EAX, EBX, ECX, EDX. The vendor string is
///       published in EBX, EDX, ECX order - which is NOT the array order, and
///       is the single most common way to get this wrong. Indexing [1],[3],[2]
///       is correct for MSVC specifically.
///
/// @note The buffer must outlive every read of CpuInfo::vendor. Passing a
///       stack array here and storing its address in the result produces a
///       string_view that dangles the moment this function returns.
void ReadVendorString(std::array<char, 13>& out)
{
    int registers[4]{};
    __cpuid(registers, 0); // Leaf 0: highest supported leaf + vendor string

    if (registers[0] < 1)
    {
        out[0] = '\0';
        return;
    }

    std::memcpy(&out[0], &registers[1], 4); // EBX
    std::memcpy(&out[4], &registers[3], 4); // EDX
    std::memcpy(&out[8], &registers[2], 4); // ECX
    out[12] = '\0';
}

/// @return The CPU vendor string, backed by storage that outlives every caller.
///
/// WHY STATIC: CpuInfo::vendor is a string_view - a non-owning pointer. The
/// only way that is safe for a value returned by value is if the bytes live
/// somewhere with static storage duration. A local array would be destroyed on
/// return and every later read would be undefined behaviour.
std::string_view QueryVendorString()
{
    static const std::array<char, 13> vendor = [] {
        std::array<char, 13> buffer{};
        ReadVendorString(buffer);
        return buffer;
    }();

    // Length is 12: the terminator is not part of the string.
    return std::string_view{vendor.data(), 12};
}

/// Detects SIMD support that the CPU can ACTUALLY execute.
///
/// THE SUBTLETY: CPUID.1:ECX.AVX being set is necessary but not sufficient.
/// AVX instructions also require
///   (a) OSXSAVE - the CPU saved the XCR0 register, and
///   (b) XCR0 bits 1 and 2 set - the OS enabled saving SSE and YMM state.
/// This second condition is why AVX code crashes on machines that report AVX
/// support: a hypervisor or old OS can leave XCR0 with only the legacy x87/SSE
/// bits, so the very first AVX instruction faults. _xgetbv is the only way to
/// read XCR0 portably from user mode.
CpuInfo QueryCpuCapabilities()
{
    CpuInfo info{};

    info.vendor = QueryVendorString();

    SYSTEM_INFO systemInfo{};
    ::GetSystemInfo(&systemInfo);
    info.logicalCoreCount = systemInfo.dwNumberOfProcessors;

    int regs[4]{};
    __cpuid(regs, 1);

    const bool hasOsxsave = (regs[2] & (1 << 27)) != 0;  // ECX bit 27
    info.hasSse2 = (regs[3] & (1 << 26)) != 0;           // EDX bit 26
    info.hasAvx  = hasOsxsave && ((regs[2] & (1 << 28)) != 0); // ECX bit 28
    info.hasFma  = (regs[2] & (1 << 12)) != 0;           // ECX bit 12

    // x87 (bit 0), SSE (bit 1), AVX/YMM (bit 2)
    std::uint64_t xcr0 = 0;
    if (hasOsxsave)
    {
        xcr0 = _xgetbv(0);
    }
    const bool ymmStateSaved = (xcr0 & 0x6) == 0x6;
    info.hasAvx = info.hasAvx && ymmStateSaved;
    info.hasFma = info.hasFma && info.hasAvx;

    // Re-read the highest supported leaf: leaf 1 above clobbered the register.
    __cpuid(regs, 0);
    if (regs[0] >= 7)
    {
        // NOTE: leaf 7 needs the THREE-argument form, __cpuidex. Plain __cpuid
        // takes only a leaf and cannot request a subleaf - calling it as
        // __cpuid(regs, 7, 0) does not compile, and if a two-argument overload
        // were ever substituted it would silently report leaf 0's data.
        __cpuidex(regs, 7, 0); // Leaf 7, subleaf 0: extended feature flags
        info.hasAvx2 = (regs[1] & (1 << 5)) != 0;   // EBX bit 5

        const bool hasAvx512f = (regs[1] & (1 << 16)) != 0; // EBX bit 16
        // XCR0 bits 5,6,7: opmask, ZMM_Hi256, Hi16_ZMM state
        const bool zmmStateSaved = (xcr0 & 0xE0) == 0xE0;
        info.hasAvx2 = info.hasAvx2 && info.hasAvx;

        if (info.hasAvx && info.hasAvx2 && zmmStateSaved && hasAvx512f)
        {
            info.largestSimdWidthInBits = 512;
        }
        else if (info.hasAvx)
        {
            info.largestSimdWidthInBits = 256;
        }
    }

    return info;
}

/// Counts physical cores from the variable-length processor topology buffer.
///
/// WHY NOT JUST divide logical by 2: that is a guess, not a measurement. A 6
/// core / 12 thread part and a 12 core / 24 thread part are both "2x", but
/// AMD's asymmetric designs and hybrid P/E cores make the ratio meaningless.
std::uint32_t QueryPhysicalCoreCount()
{
    DWORD bufferSize = 0;
    // First call with a null buffer returns the required size
    // (ERROR_INSUFFICIENT_BUFFER).
    ::GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bufferSize);
    if (bufferSize == 0)
    {
        return 0;
    }

    std::vector<BYTE> buffer(bufferSize);
    auto* records = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data());

    if (!::GetLogicalProcessorInformationEx(RelationProcessorCore, records, &bufferSize))
    {
        return 0;
    }

    // The buffer is an ARRAY of variable-length records linked by Size. Walking
    // it by arithmetic rather than pointer increment is mandatory: sizeof of
    // the struct is only the header, the CPU-mask tail is longer.
    std::uint32_t coreCount = 0;
    for (DWORD offset = 0; offset < bufferSize;)
    {
        const auto* record = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(
            buffer.data() + offset);
        ++coreCount;
        offset += record->Size;
    }
    return coreCount;
}

CpuInfo BuildCpuInfo()
{
    CpuInfo info = QueryCpuCapabilities();

    const std::uint32_t physical = QueryPhysicalCoreCount();
    // Never report more physical cores than logical ones - the API can return
    // odd topology on hybrid parts, and a physical count above the logical
    // count would corrupt any shard-count calculation downstream.
    info.physicalCoreCount = (physical > 0 && physical < info.logicalCoreCount)
                                 ? physical
                                 : info.logicalCoreCount;
    return info;
}
} // namespace

CpuInfo GetCpuInfo()
{
    // Function-local static: initialised once, on first call, thread-safely
    // (guaranteed since C++11). Cheaper than std::once_flag and no include.
    // CPUID is a serialising instruction (~100 cycles), so this must be cached.
    static const CpuInfo cached = BuildCpuInfo();
    return cached;
}

} // namespace Nova::Platform