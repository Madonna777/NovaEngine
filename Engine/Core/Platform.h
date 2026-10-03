// ===========================================================================
//  Platform.h
// ---------------------------------------------------------------------------
//  Thin, stateless wrappers over the Win32 APIs the engine needs.
//
//  WHY A PLATFORM LAYER AT ALL
//  ----------------------------
//  These wrappers are thin on purpose. The value is not abstraction for its
//  own sake - it is that:
//    * <windows.h> is included in .cpp files ONLY, never in a header. This is
//      non-negotiable: windows.h defines min/max, TRUE/FALSE, and macros like
//      CreateFile that collide with our own names, and every translation unit
//      that includes it pays ~200ms of parse time.
//    * The set of Win32 APIs the engine may call is small, greppable, and
//      reviewable in one place.
//    * When a Linux/macOS backend is added (or a test runs headless), only this
//      file changes - and the stubs can be honest fakes.
//
//  NOTHING HERE OWNS A RESOURCE. No handles, no lifetimes, no state. Every
//  function is a pure query or an immediate action. That is deliberate: state
//  in a platform layer is where uninitialisation-order bugs go to hide.
//
//  Platform detection macros (NOVA_PLATFORM_*) and the portable compile-time
//  binary invariants live in <Core/Target.h>. They are included rather than
//  repeated here so that a consumer of this header gets both without having to
//  know that only one of them is about the OS.
// ===========================================================================
#pragma once

#include <Core/Target.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace Nova::Platform
{
/// CPU capabilities discovered once at startup.
///
/// WHY THIS MATTERS FOR A D3D12 ENGINE: the GPU is not your performance floor
/// - the CPU is. A renderer that submits draws from a single thread can be
/// CPU-bound long before the GPU is busy. Knowing physical vs logical core
/// count determines whether you shard work across cores or across threads on
/// one core (SMT siblings are NOT independent execution units; two jobs on one
/// physical core's two hyperthreads contend for the same vector units).
struct CpuInfo
{
    /// Physical cores. Equal to logical count when SMT is off or unavailable.
    std::uint32_t physicalCoreCount = 0;

    /// Logical processors (hardware threads) visible to the OS.
    std::uint32_t logicalCoreCount = 0;

    /// Widest SIMD register width the CPU actually executes: 128, 256, or 512.
    ///
    /// IMPORTANT: this describes the CPU. HLSL shaders compile to 128-bit
    /// float4 lanes on D3D12 regardless of this value, so a 512-bit CPU does
    /// not imply any change to your shader math.
    std::uint32_t largestSimdWidthInBits = 128;

    bool hasSse2 = false; ///< Always true on x64; present for symmetry.
    bool hasAvx  = false; ///< 256-bit float/integer ops.
    bool hasFma  = false; ///< Fused multiply-add - 3-operand, no rounding step.
    bool hasAvx2 = false; ///< 256-bit integer ops.

    /// Vendor string from CPUID ("GenuineIntel", "AuthenticAMD", ...).
    /// Points at static storage; do not free.
    std::string_view vendor;
};

/// System and process memory in bytes.
struct MemoryInfo
{
    std::uint64_t totalBytes          = 0; ///< Physical RAM installed.
    std::uint64_t availableBytes      = 0; ///< Physical RAM not in use.
    std::uint64_t pageSize            = 0; ///< Virtual memory page granularity.

    /// Commit charge for the current process, including reserved-but-uncommitted
    /// pages. This is the number that grows when you leak GPU upload staging
    /// buffers, so it is the one worth watching in Tracy.
    std::uint64_t processCommittedBytes = 0;

    /// Resident physical pages of the current process. Compare against the
    /// previous value to detect growth after a frame.
    std::uint64_t processWorkingSetBytes = 0;
};

/// Queries logical/physical core counts and SIMD capabilities.
///
/// @note Results are cached after the first call: CPUID is a serialising
///       instruction (~100+ cycles) and none of this changes at runtime.
CpuInfo GetCpuInfo();

/// Queries current system and process memory usage. Not cached.
MemoryInfo GetMemoryInfo();

/// @return Monotonic timer in seconds, relative to an unspecified epoch.
///
/// WHY NOT GetTickCount64: its resolution is the system timer period, typically
/// 10-16 ms on a machine with a 60 Hz timer. Frame times are 1-16 ms, so
/// GetTickCount64 cannot even represent a single frame - profiling built on it
/// is useless. QueryPerformanceCounter on modern Windows is backed by the TSC
/// and resolves to sub-microsecond.
///
/// @note Monotonic and high-resolution, but NOT guaranteed to be the time
///       since boot. Only differences between two samples are meaningful -
///       never treat the absolute value as a wall-clock timestamp.
double GetHighResolutionSeconds();

/// @return Ticks per second of the high-resolution timer (constant).
std::uint64_t GetHighResolutionFrequency();

/// @return The calling thread's OS id, as assigned by Windows.
std::uint32_t GetCurrentThreadId();

/// Formats a Win32 error code into a human-readable message.
///
/// @param errorCode Value from GetLastError(), or a HRESULT cast to uint32.
///
/// @note GetLastError is only meaningful immediately after the failing call:
///       any intervening Win32 call may reset it. When checking HRESULTs,
///       format the HRESULT itself - that is why this accepts a raw code.
///
/// @return Never empty. Returns the code in hex if the system knows no
///         message for it, rather than an empty string.
std::string GetLastErrorMessage(std::uint32_t errorCode);

/// @return "Windows" - for logging and bug reports.
std::string_view GetPlatformName();

/// Relinquishes the remainder of the current thread's time slice, hinting to
/// the CPU that the caller is in a spin-wait loop.
///
/// WHY NOT std::this_thread::yield(): this is the platform primitive yield()
/// maps to, and it exists on every target. Calling it inside a spin-wait loop
/// that has already observed a shared atomic flag saves ~200 cycles per
/// iteration versus a scheduler round-trip.
///
/// @note Named CpuRelax, and the name is load-bearing. Two obvious alternatives
///       are Win32 MACROS, and both fail differently and quietly:
///
///         winnt.h:   #define YieldProcessor _mm_pause
///         WinBase.h: #define Yield()
///
///       With the first, a wrapper declared as YieldProcessor is silently
///       rewritten by the preprocessor, so the engine never emits its own
///       symbol and the caller fails with an LNK2019 pointing nowhere near the
///       cause. With the second, the declaration expands to nothing and the
///       build dies on a bare `void` at the definition. Neither names the
///       preprocessor. Naming our wrappers differently from their Win32
///       counterparts costs nothing; debugging this twice costs an afternoon.
void CpuRelax();

/// Blocks the calling thread for at least @p milliseconds.
///
/// WHY THIS IS A DELIBERATE TRAP: sleeping on the main thread stalls the whole
/// engine. In the render loop, prefer waiting on an event or on fence value.
void Sleep(std::uint32_t milliseconds);

} // namespace Nova::Platform