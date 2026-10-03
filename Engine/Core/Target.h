// ===========================================================================
//  Target.h
// ---------------------------------------------------------------------------
//  Compile-time description of the build target: which platform, and what
//  binary invariants the engine relies on.
//
//  WHY THIS IS A SEPARATE HEADER FROM Platform.h: the two answer different
//  questions. This file answers "what am I being compiled for?" - a property of
//  the preprocessor, resolved before any code exists. Platform.h answers "what
//  can I ask the OS?" - runtime queries against a live kernel. Mixing them
//  meant every file that only wanted the platform macros also had to parse the
//  Win32 wrapper documentation, and the invariants (which must be visible
//  everywhere) were buried in a file people open only when they need GetCpuInfo.
//
//  NOTHING HERE TOUCHES WINDOWS.H. That is the point. These invariants are
//  checked in every translation unit of the engine, so they must be expressible
//  with portable types only. The Windows ABI checks - LONG, DWORD, HRESULT -
//  need windows.h and therefore live in Platform.cpp, the single translation
//  unit in Core where that include is legitimate.
// ===========================================================================
#pragma once

// ===========================================================================
//  PLATFORM DETECTION
// ---------------------------------------------------------------------------
//  Exactly one NOVA_PLATFORM_* macro is defined to 1, so downstream code writes
//  `#if NOVA_PLATFORM_WINDOWS` instead of a chain of #ifdefs that silently
//  compiles the wrong branch.
//
//  WHY EVERY MACRO IS DEFINED TO 1 OR 0, AND NEVER LEFT UNDEFINED: an
//  undefined identifier in a #if evaluates as 0, so `#if NOVA_PLATFORM_MACOS`
//  is false on Windows whether or not the macro exists - the code is correct by
//  accident. Defining the inactive ones to an explicit 0 means every #if in the
//  engine reads the same way, and - the part that actually matters - a
//  translation unit that includes this header cannot be broken by a macro that
//  happens to be missing from its include chain.
//
//  WHY DETECT HERE RATHER THAN PASSING IT FROM CMake: these macros describe the
//  COMPILING toolchain, and the preprocessor is the only thing that can read
//  them. CMake also knows the target platform, so the answer could be injected
//  as a compile definition - and then the two could disagree, with the failure
//  mode being code that compiles for one platform and runs on another. A single
//  source of truth, derived from the compiler's own predefined macros, cannot
//  drift.
// ===========================================================================

#if defined(_WIN32) || defined(_WIN64)
    #define NOVA_PLATFORM_WINDOWS 1
#else
    #define NOVA_PLATFORM_WINDOWS 0
#endif

#if defined(__linux__)
    #define NOVA_PLATFORM_LINUX 1
#else
    #define NOVA_PLATFORM_LINUX 0
#endif

#if defined(__APPLE__)
    #define NOVA_PLATFORM_MACOS 1
#else
    #define NOVA_PLATFORM_MACOS 0
#endif

// Exactly one platform must be active. Two means the detection above is wrong;
// zero means this compiler targets something the engine does not support.
// This is an #error and not a static_assert because it must fire before the
// rest of the header is even parsed - if the detection is broken, every
// assertion below is meaningless too.
#if (NOVA_PLATFORM_WINDOWS + NOVA_PLATFORM_LINUX + NOVA_PLATFORM_MACOS) != 1
    #error "NovaEngine: exactly one NOVA_PLATFORM_* macro must be defined to 1"
#endif

#if NOVA_PLATFORM_WINDOWS
    #define NOVA_PLATFORM_NAME "Windows"
#elif NOVA_PLATFORM_LINUX
    #define NOVA_PLATFORM_NAME "Linux"
#else
    #define NOVA_PLATFORM_NAME "macOS"
#endif

#include <cstdint>

// ===========================================================================
//  COMPILE-TIME BINARY INVARIANTS
// ---------------------------------------------------------------------------
//  These assert properties of the BINARY FORMAT, not of the source. Each one,
//  if violated, produces code that compiles cleanly, links, and then corrupts
//  data at runtime - typically as garbled rendering or a GPU-side fault rather
//  than a crash anywhere near the mistake.
//
//  WHY STATIC_ASSERT AND NOT NOVA_ASSERT: these must fire at COMPILE time. A
//  32-bit build would not fail to build; it would fail to be CORRECT, and the
//  symptom would surface hundreds of lines away in the renderer.
//
//  WHY <cstdint> FIXED-WIDTH TYPES AND NOT THE ABI'S OWN NAMES: windows.h is
//  banned from headers, so LONG and HRESULT cannot be named here - they are
//  asserted in Platform.cpp instead. The std::intN_t types are the portable
//  half, and they are what actually pins the width: int32_t is *defined* as a
//  signed 32-bit type with no padding bits, so its size is exactly 4 and its
//  range is exactly [-2^31, 2^31-1] on every conforming implementation. No
//  separate range check is needed.
// ===========================================================================

// Pointer width. Every COM interface method, every D3D12 pointer, and every
// handle passed across a module boundary is pointer-width. On a 32-bit target
// COM would fault at a machine boundary we do not control.
static_assert(sizeof(void*) == 8,
              "NovaEngine requires a 64-bit target: pointer width is load-bearing "
              "for COM and D3D12 interfaces");

// D3D12 and COM field widths. Resource dimensions, view counts, descriptor
// handles and HRESULT are all built on exact 32-bit integers.
static_assert(sizeof(std::int32_t) == 4,
              "D3D12 and COM require an exact 32-bit signed integer");
static_assert(sizeof(std::uint32_t) == 4,
              "D3D12 descriptor handles and sizes are 32-bit unsigned");
static_assert(sizeof(std::uint64_t) == 8,
              "D3D12 GPU virtual addresses and buffer offsets are 64-bit");

// ===========================================================================
//  WHAT IS DELIBERATELY NOT ASSERTED HERE
// ---------------------------------------------------------------------------
//  The 16-byte alignment of constant buffers - the single most load-bearing
//  layout rule in a D3D12 renderer - cannot be checked from here, because it is
//  a property of DirectXMath's vector types, which this header deliberately does
//  not include. Asserting on a stand-in such as `alignof(double)` would prove
//  nothing while appearing to prove something. That check belongs in the Math
//  module, next to the types it constrains, and it will be written there.