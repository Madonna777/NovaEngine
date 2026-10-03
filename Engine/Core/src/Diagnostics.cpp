// ===========================================================================
//  Diagnostics.cpp
// ---------------------------------------------------------------------------
//  Assertion reporting and Win32 error formatting.
//
//  WHY THIS IS A SEPARATE TRANSLATION UNIT FROM Platform.cpp
//  -----------------------------------------------------------
//  Platform.cpp contains queries. Diagnostics contains the failure paths. The
//  split exists so that the assertion machinery - which is the only code in the
//  engine permitted to reach for <Windows.h> debugging primitives - can evolve
//  or be compiled out independently of the hardware queries, and so a reader
//  looking for "where does this engine break" finds one short file rather than
//  a needle in a 300-line query implementation.
// ===========================================================================

#include <Core/Assert.h>
#include <Core/Platform.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <array>
#include <cstdio>

namespace Nova::Platform
{
namespace
{
/// Writes a line to both the console and the Visual Studio Output window.
///
/// WHY NOT spdlog HERE: a logger is exactly what is often unavailable or broken
/// at the moment an assertion fires - during static init, after a failed
/// allocation, or while the log file handle is already closed. Depending on the
/// logger to report the logger's own failure is a circular dependency that
/// fails open. fprintf and OutputDebugStringA are always available.
///
/// @note OutputDebugStringA is what makes this reachable from a debugger
///       attached after a crash, when the console may already be gone.
void EmitDiagnostic(const char* text)
{
    std::fputs(text, stderr);
    std::fflush(stderr);
    ::OutputDebugStringA(text);
}
} // namespace

[[noreturn]] void ReportAssertionFailure(const char* expression,
                                          const char* file,
                                          int         line,
                                          const char* function,
                                          std::string_view message)
{
    // Composed into a stack buffer rather than std::string: if this runs from a
    // failed allocation or after malloc corruption, allocating to build the
    // message would fault inside the error handler. Fixed storage cannot fail.
    // The size is a deliberate tradeoff: long __FILE__ paths get truncated, but
    // a truncated diagnostic beats a second fault.
    std::array<char, 2048> buffer{};

    const char* basename = file != nullptr ? file : "<unknown>";
    for (const char* scan = basename; *scan != '\0'; ++scan)
    {
        if (*scan == '/' || *scan == '\\')
        {
            basename = scan + 1;
        }
    }

    if (message.empty())
    {
        std::snprintf(buffer.data(), buffer.size(),
                      "[NOVA ASSERT] %s\n  failed : %s\n  at     : %s:%d in %s\n",
                      "unrecoverable engine precondition violated",
                      expression != nullptr ? expression : "<expr>", basename, line,
                      function != nullptr ? function : "<fn>");
    }
    else
    {
        std::snprintf(buffer.data(), buffer.size(),
                      "[NOVA ASSERT] %s\n  failed : %s\n  at     : %s:%d in %s\n"
                      "  context : %.*s\n",
                      "unrecoverable engine precondition violated",
                      expression != nullptr ? expression : "<expr>", basename, line,
                      function != nullptr ? function : "<fn>",
                      static_cast<int>(message.size()), message.data());
    }

    EmitDiagnostic(buffer.data());
    DebugBreak();
}

[[noreturn]] void DebugBreak()
{
    __debugbreak();
}

std::string GetLastErrorMessage(std::uint32_t errorCode)
{
    // WIDE API, THEN EXPLICIT CONVERSION TO UTF-8.
    //
    // WHY NOT FormatMessageA: the 'A' variant returns text in the system's
    // ANSI code page (1251 on a Russian-locale machine). Both of our sinks -
    // console and log file - are UTF-8. Mixing encodings means every
    // non-ASCII error message arrives as mojibake, which defeats the entire
    // purpose of a function whose job is to make failures readable.
    LPWSTR systemMessage = nullptr;

    // FORMAT_MESSAGE_ALLOCATE_BUFFER returns a pointer to a buffer the function
    // allocates, pointed to BY systemMessage - hence the double indirection.
    // The size argument is ignored in that mode, and the result must be freed
    // with LocalFree, never delete[] or free.
    const DWORD length = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&systemMessage), 0, nullptr);

    if (length == 0 || systemMessage == nullptr)
    {
        // Unknown or unmapped code. Returning an empty string would print as a
        // bare "error 0x80070057" with no context, so synthesise one that
        // always carries the numeric value.
        char fallback[32]{};
        std::snprintf(fallback, sizeof(fallback), "0x%08X", errorCode);
        return std::string{fallback};
    }

    // -1 means "null-terminated input"; 0 on output reports the required size.
    const int required = ::WideCharToMultiByte(CP_UTF8, 0, systemMessage, -1, nullptr, 0,
                                               nullptr, nullptr);
    std::string result;
    if (required > 1)
    {
        result.resize(static_cast<std::size_t>(required - 1));
        ::WideCharToMultiByte(CP_UTF8, 0, systemMessage, -1, result.data(), required,
                              nullptr, nullptr);
    }

    ::LocalFree(systemMessage);

    // FormatMessage loves to append CRLF. Logging "path failed.\r\n" produces
    // double-spaced output and breaks single-line log parsers.
    while (!result.empty() &&
           (result.back() == '\r' || result.back() == '\n' || result.back() == ' '))
    {
        result.pop_back();
    }

    return result;
}

void EnableUtf8Console()
{
    // The console and the log file must agree on encoding, or every non-ASCII
    // character is corrupted in one of them. Set both directions: the output
    // code page governs what we print, the input code page governs what we read
    // from stdin.
    //
    // WHY THIS IS NOT DONE LAZILY INSIDE THE LOGGER: it changes process-wide
    // global state, and a library that silently reconfigures the console as a
    // side effect of logging is a bad neighbour. It is an explicit startup
    // call from the application entry point.
    ::SetConsoleOutputCP(CP_UTF8);
    ::SetConsoleCP(CP_UTF8);
}

} // namespace Nova::Platform