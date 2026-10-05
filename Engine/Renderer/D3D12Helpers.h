// ===========================================================================
//  D3D12Helpers.h
// ---------------------------------------------------------------------------
//  The small pieces of COM/D3D12 boilerplate every call site would otherwise
//  repeat: smart pointers, an HRESULT assertion, descriptor-handle arithmetic,
//  and an owning wrapper for the one Win32 handle the fence needs.
//
//  WHY NO d3dx12.h
//  ----------------
//  The Microsoft DirectX 12 samples lean on d3dx12.h for CD3D12_* helper
//  structs and descriptor arithmetic. That header is not part of the Windows
//  SDK - it ships in the DirectX SDK / Agility SDK - so depending on it means
//  vendoring ~20,000 lines of third-party header into the engine and slowing
//  every translation unit that includes it. The three things this milestone
//  needs from it are:
//
//      CD3D12_CPU_DESCRIPTOR_HANDLE   -> D3D12_CPU_DESCRIPTOR_HANDLE
//      CD3D12_GPU_DESCRIPTOR_HANDLE   -> D3D12_GPU_DESCRIPTOR_HANDLE
//      Offset(handle, i, size)        -> OffsetCpu / OffsetGpu below
//
//  Both handle types are plain structs with a single pointer field, so the
//  arithmetic is one addition. See DescriptorHandles for why the offset is
//  computed in BYTES and not in slots.
//
//  WHY ComPtr AND NOT _com_ptr_t OR a hand-rolled _variant
//  ------------------------------------------------------
//  Every COM object in this module is owned by a ComPtr member. Two properties
//  of that are load-bearing rather than stylistic:
//
//    - Copy is a reference-count bump, so the D3D12Context copy constructor
//      being deleted is not a workaround - it removes an entire class of
//      double-release bug.
//    - Assignment releases the previous object BEFORE storing the new one,
//      which is why "release the back buffer, then ResizeBuffers" is two
//      statements and not a dance.
//
//  _com_ptr_t has the same semantics but its error reporting is an assert in a
//  debug-only path, which in a Release build becomes a null pointer at an
//  unpredictable later point.
// ===========================================================================
#pragma once

#include <Core/Platform.h>

// The D3D12 and DXGI headers include <windows.h> themselves unless
// COM_NO_WINDOWS_H is defined. Nova configures WIN32_LEAN_AND_MEAN and NOMINMAX
// on every engine target (see nova_configure_target) precisely so that doing so
// here is safe: without NOMINMAX the min/max macros collide with <algorithm>'s
// std::min and the failure is a wall of template errors inside the engine's own
// headers rather than anything resembling a Windows problem.
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <string_view>

/// Whether this build enables the D3D12 debug layer.
///
/// WHY A BUILD DEFINE AND NOT #ifndef NDEBUG: the debug layer is not free. It
/// adds a per-API validation pass on the CPU and, on some drivers, a
/// synchronisation point, which makes a Debug build's frame times useless for
/// measuring performance. A developer who needs honest Debug-build timings
/// should be able to turn it off without changing source, and a developer who
/// validates on a machine with a different driver should be able to turn it on.
/// Engine/Renderer/CMakeLists.txt sets this per configuration.
///
/// @note The default matters more than it looks: an undefined macro in `#if`
///       silently becomes 0. Without this fallback the debug layer would be off
///       for anyone building without CMake, and the failure is invisible - the
///       engine runs perfectly and reports no errors at all.
#ifndef NOVA_D3D12_DEBUG_LAYER
#define NOVA_D3D12_DEBUG_LAYER 0
#endif

namespace Nova::Renderer
{
/// Owning smart pointer for a COM interface.
///
/// @note An alias rather than a `using` inside each file, so a signature in a
///       renderer header reads the same as one in a renderer source and the
///       choice of smart pointer is visible in exactly one place.
template <typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

// ===========================================================================
//  Error checking
// ===========================================================================

/// Returns the final path component of a source path.
///
/// @param path Usually __FILE__, which is an ABSOLUTE path here because the
///             build adds /FC. An absolute path in an exception message forces
///             the reader to scroll; the basename is what identifies the file.
///
/// @note constexpr and header-inline rather than a .cpp function because it
///       runs only on the error path, and because a header-only helper here
///       costs nothing when it is never called.
[[nodiscard]] constexpr const char* SourceBasename(const char* path) noexcept
{
    // Windows accepts both separators in every path API, and __FILE__ has
    // carried both depending on the toolchain that produced it.
    for (const char* character = path; *character != '\0'; ++character)
    {
        if (*character == '\\' || *character == '/')
        {
            path = character + 1;
        }
    }
    return path;
}

/// Throws std::runtime_error describing a failed HRESULT, then does not return.
///
/// @param result The failing HRESULT. Only call this for a code that actually
///               failed - use NOVA_THROW_IF_FAILED, which tests first.
/// @param file   __FILE__ of the call site.
/// @param line   __LINE__ of the call site.
///
/// @note The message carries the numeric code AND the system's text for it.
///       The text matters because D3D12's HRESULTs are specific to the point of
///       failure - DXGI_ERROR_DEVICE_REMOVED and DXGI_ERROR_DEVICE_HUNG both
///       mean "this GPU is gone" but imply completely different next steps -
///       while the code is what has to be searchable in the headers.
///       Formatting reuses Platform::GetLastErrorMessage rather than a second
///       FormatMessage implementation; that function already documents that it
///       accepts a HRESULT cast to its parameter type.
[[noreturn]] void ThrowIfFailed(HRESULT result, const char* file, int line);

/// Throws if @p result indicates failure; otherwise evaluates to nothing.
///
/// @note The do/while(false) wrapper is not decoration. Without it this macro
///       is a single `if` statement, so `NOVA_THROW_IF_FAILED(a); else ...`
///       compiles and silently binds the else to the macro. The user's code is
///       then wrong in a way no warning reports.
///
/// @note The HRESULT is evaluated exactly once and stored in a local, because
///       a macro that repeats its argument evaluates a call such as
///       queue->Signal(...) twice - which for D3D12 means submitting the work
///       and then signalling twice.
#define NOVA_THROW_IF_FAILED(result)                                                             \
    do                                                                                           \
    {                                                                                            \
        const ::HRESULT novaResult = (result);                                                    \
        if (FAILED(novaResult))                                                                   \
        {                                                                                        \
            ::Nova::Renderer::ThrowIfFailed(novaResult, __FILE__, __LINE__);                      \
        }                                                                                        \
    } while (false)

// ===========================================================================
//  Descriptor handles
// ===========================================================================
//
//  A descriptor is a slot in a flat, contiguous array on the CPU or on the GPU.
//  A handle is a pointer to one slot, and advancing to the Nth slot is pointer
//  arithmetic - but in BYTES, not in slots.
//
//  WHY THE BYTE STRIDE IS A PARAMETER AND NOT A HIDDEN CONSTANT:
//  descriptor heaps allocate slots whose size varies by heap type (render
//  target views, root signatures, samplers all differ) AND by driver. The
//  authoritative value comes from
//  ID3D12Device::GetDescriptorHandleIncrementSize(heapType), and hard-coding any
//  of them produces an offset that is correct only on the machine it was
//  written on - a corruption bug that reads as a driver bug, not as arithmetic.
//
//  Handles are opaque by design: the GPU writes into a descriptor heap by
//  index, never by dereferencing the pointer. These are addresses, and the
//  arithmetic is the supported way to move one.

/// @return @p base advanced by @p index slots of @p strideInBytes bytes each.
///
/// @param base          A heap's start handle (e.g. from
///                      GetCPUDescriptorHandleForHeapStart).
/// @param index         Slot offset. 0 returns @p base unchanged.
/// @param strideInBytes Slot size, from GetDescriptorHandleIncrementSize.
[[nodiscard]] constexpr D3D12_CPU_DESCRIPTOR_HANDLE OffsetCpu(
    D3D12_CPU_DESCRIPTOR_HANDLE base, std::uint32_t index, std::uint32_t strideInBytes) noexcept
{
    return D3D12_CPU_DESCRIPTOR_HANDLE{base.ptr + static_cast<SIZE_T>(index) * strideInBytes};
}

/// @return @p base advanced by @p index slots of @p strideInBytes bytes each.
///
/// @note The GPU handle is a UINT64 while the CPU handle is a SIZE_T, because
///       descriptor heaps may be placed in 64-bit GPU virtual address space.
///       Conflating the two is not a stylistic difference.
[[nodiscard]] constexpr D3D12_GPU_DESCRIPTOR_HANDLE OffsetGpu(
    D3D12_GPU_DESCRIPTOR_HANDLE base, std::uint32_t index, std::uint32_t strideInBytes) noexcept
{
    return D3D12_GPU_DESCRIPTOR_HANDLE{base.ptr + static_cast<UINT64>(index) * strideInBytes};
}

// ===========================================================================
//  WinHandle
// ===========================================================================

/// RAII owner for a Win32 kernel HANDLE.
///
/// @note The fence event is the only non-COM resource this module owns, and it
///       is exactly the kind that leaks. CreateEvent followed by an unchecked
///       early return - or a throw from the next line - leaks a handle that the
///       process will hold until it exits, once per failed device creation. In
///       a tool that retries device creation in a loop that is a hard handle
///       leak; in a game that is a bug report months later.
///
/// @note Move-only and not copyable, for the same reason ComPtr is used
///       everywhere else: a copied handle has two owners and one CloseHandle.
class WinHandle final
{
public:
    WinHandle() noexcept = default;

    /// Takes ownership of an existing handle.
    ///
    /// @note explicit, so a raw HANDLE is never converted by accident on the
    ///       way into a parameter that does not own it.
    explicit WinHandle(HANDLE handle) noexcept : handle_(handle) {}

    ~WinHandle() { Reset(); }

    WinHandle(const WinHandle&)            = delete;
    WinHandle& operator=(const WinHandle&) = delete;

    WinHandle(WinHandle&& other) noexcept : handle_(other.Release()) {}

    WinHandle& operator=(WinHandle&& other) noexcept
    {
        if (this != &other)
        {
            Reset(other.Release());
        }
        return *this;
    }

    /// @return The raw handle, still owned by this object.
    [[nodiscard]] HANDLE Get() const noexcept { return handle_; }

    [[nodiscard]] explicit operator bool() const noexcept { return handle_ != nullptr; }

    /// Closes the current handle and adopts @p handle.
    ///
    /// @param handle New handle to own, or nullptr to only close.
    ///
    /// @note Closing is skipped when @p handle equals the current one, so
    ///       Reset(Get()) is a no-op rather than a close-then-use-of-a-closed
    ///       handle. A self-assignment check alone would not catch that case.
    void Reset(HANDLE handle = nullptr) noexcept
    {
        if (handle_ != nullptr && handle_ != handle)
        {
            ::CloseHandle(handle_);
        }
        handle_ = handle;
    }

    /// @return The handle and gives up ownership. The caller must CloseHandle it.
    [[nodiscard]] HANDLE Release() noexcept
    {
        HANDLE released = handle_;
        handle_         = nullptr;
        return released;
    }

private:
    HANDLE handle_ = nullptr;
};

// ===========================================================================
//  Text
// ===========================================================================

/// Converts a null-terminated UTF-16 string to UTF-8.
///
/// @param wide Text to convert. May be nullptr, which yields an empty string.
///
/// @note DXGI reports adapter names as WCHAR[128]. The engine logs UTF-8 - the
///       build sets /utf-8 and the console output is UTF-8 - so every one of
///       those strings has to be converted before it reaches a format argument.
///       Passing a wchar_t* to a "{}" placeholder compiles on MSVC and prints
///       four bytes of pointer instead of the name, because the char and wchar
///       format paths differ. This function exists to make that mistake
///       impossible rather than merely unlikely.
///
/// @return Never throws. Returns an empty string on failure, because a GPU that
///         cannot name itself is not a reason to fail device creation.
[[nodiscard]] inline std::string ToUtf8(const wchar_t* wide) noexcept
{
    if (wide == nullptr || wide[0] == L'\0')
    {
        return {};
    }

    // -1 asks for the length INCLUDING the terminator, which makes the buffer
    // allocation and the conversion identical code paths - no size to be wrong.
    const int required = ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (required <= 0)
    {
        return {};
    }

    std::string result(static_cast<std::size_t>(required), '\0');
    const int written =
        ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, result.data(), required, nullptr, nullptr);
    if (written <= 0)
    {
        return {};
    }

    // required counts the null terminator, which std::string does not want.
    result.resize(static_cast<std::size_t>(written - 1));
    return result;
}

} // namespace Nova::Renderer