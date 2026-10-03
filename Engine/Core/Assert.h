// ===========================================================================
//  Assert.h
// ---------------------------------------------------------------------------
//  Assertion and diagnostic-break macros.
//
//  WHY THESE ARE NOT <cassert>
//  ----------------------------
//  1. <cassert>'s assert() is COMPILED OUT when NDEBUG is defined, which
//     CMake defines for Release and RelWithDebInfo. An engine bug that only
//     manifests in an optimised build would then vanish silently. Nova's
//     asserts are ALWAYS ON: in a 60 fps shipping loop, an unrecoverable
//     state error is worth a hard stop with a diagnostic, because continuing
//     means uploading garbage to the GPU and corrupting memory.
//
//  2. <cassert> writes nothing. It cannot tell you WHICH engine subsystem
//     tripped, cannot carry a message, and does not reach the log file.
//
//  3. The standard macro requires a statement and has awkward semantics in
//     release builds (it can silently drop a side effect - see NOVA_VERIFY).
//
//  THROWING vs BREAKING: Nova asserts do NOT throw. An exception thrown from
// deep inside the render loop unwinds through COM vtables and GPU submission
// code, which is exactly where unwinding is least predictable. Asserts are
// unrecoverable, so they trap instead - the debugger attaches and you inspect
// real state.
// ===========================================================================
#pragma once

#include <cstdint>
#include <string_view>

namespace Nova::Platform
{
/// Reports a failed assertion to the debugger, stderr, and the Output Debug
/// window, then traps.
///
/// @param expression Text of the expression that failed, as written in source.
/// @param file       Source file of the failing expression.
/// @param line       1-based source line.
/// @param function   Enclosing function signature.
/// @param message    Optional extra context as a plain string. May be empty.
///
/// @note Never returns. Declared [[noreturn]] so the compiler can prove the
///       path ends and drop dead code after an assertion - which also means a
///       value-returning function may legitimately have no return statement
///       after an assert.
///
/// @note The message is a bare std::string_view and NOT a format string, on
///       purpose: Assert.h is included by nearly every file in the engine, so
///       pulling a formatting library into it would tax every translation unit
///       for the sake of diagnostics. To compose a message from values, build a
///       std::string at the call site and pass that. A variadic
///       NOVA_ASSERT_MSG_FMT using fmt is a deliberate later addition.
[[noreturn]] void ReportAssertionFailure(const char* expression,
                                         const char* file,
                                         int         line,
                                         const char* function,
                                         std::string_view message);

/// Triggers a debugger breakpoint (INT 3 / __debugbreak).
///
/// @note Only meaningful when a debugger is attached; otherwise it raises
///       EXCEPTION_BREAKPOINT, which the OS turns into a crash dialog.
[[noreturn]] void DebugBreak();

/// Switches the process console to UTF-8 for both input and output.
///
/// @note Call once, at the very top of an application's entry point, before
///       any logging. See Platform.h for why this is not automatic.
void EnableUtf8Console();
} // namespace Nova::Platform

// ---------------------------------------------------------------------------
//  NOVA_ASSERT - precondition. Fire-and-break, always enabled.
//
//  Use for conditions the engine cannot continue without: null device, invalid
//  handle, failed HRESULT that leaves an object unusable.
// ---------------------------------------------------------------------------
#define NOVA_ASSERT(expression)                                              \
    do                                                                       \
    {                                                                        \
        if (!(expression))                                                   \
        {                                                                    \
            ::Nova::Platform::ReportAssertionFailure(#expression, __FILE__,  \
                                                    __LINE__, __func__, {}); \
        }                                                                    \
    } while (false)

// ---------------------------------------------------------------------------
//  NOVA_ASSERT_MSG - precondition with context.
//
//  Prefer this over bare NOVA_ASSERT whenever you can say WHY the invariant
//  matters; the message is what you will be reading at 2am in a crash dump.
// ---------------------------------------------------------------------------
#define NOVA_ASSERT_MSG(expression, ...)                                     \
    do                                                                       \
    {                                                                        \
        if (!(expression))                                                   \
        {                                                                    \
            ::Nova::Platform::ReportAssertionFailure(#expression, __FILE__,  \
                                                    __LINE__, __func__,      \
                                                    __VA_ARGS__);            \
        }                                                                    \
    } while (false)

// ---------------------------------------------------------------------------
//  NOVA_VERIFY - postcondition / side-effecting expression.
//
//  THE DIFFERENCE: NOVA_VERIFY always evaluates its expression, NOVA_ASSERT
//  only evaluates it in an if(). If the expression has side effects (a call
//  that must run, an iterator increment), it MUST use NOVA_VERIFY - otherwise
//  an optimised build can delete the call entirely, a bug that does not
//  reproduce in Debug. This is the classic assert(printf("...")) trap.
// ---------------------------------------------------------------------------
#define NOVA_VERIFY(expression)                                              \
    do                                                                       \
    {                                                                        \
        if (!(expression))                                                   \
        {                                                                    \
            ::Nova::Platform::ReportAssertionFailure(#expression, __FILE__,  \
                                                    __LINE__, __func__, {}); \
        }                                                                    \
    } while (false)

#define NOVA_VERIFY_MSG(expression, ...)                                     \
    do                                                                       \
    {                                                                        \
        if (!(expression))                                                   \
        {                                                                    \
            ::Nova::Platform::ReportAssertionFailure(#expression, __FILE__,  \
                                                    __LINE__, __func__,      \
                                                    __VA_ARGS__);            \
        }                                                                    \
    } while (false)

// ---------------------------------------------------------------------------
//  NOVA_UNREACHABLE - marks code that must never execute.
//
//  The trailing __assume(0) is what makes this valuable: it tells MSVC the
//  branch is impossible, enabling real dead-code elimination AFTER the assert.
//  Without it, an unterminated switch in a hot loop keeps generating a jump
//  table to a trap.
//
//  EXAMPLE - exhaustively handled enum switch:
//      switch (state) {
//          case State::Idle:   ...
//          case State::Loading: ...
//          case State::Failed: ...
//      }
//      NOVA_UNREACHABLE();   // new enum value added and we forgot a case
// ---------------------------------------------------------------------------
#define NOVA_UNREACHABLE()                                                   \
    do                                                                       \
    {                                                                        \
        ::Nova::Platform::ReportAssertionFailure("unreachable code", __FILE__,\
                                                 __LINE__, __func__, {});    \
        __assume(0);                                                         \
    } while (false)

#define NOVA_UNREACHABLE_MSG(...)                                            \
    do                                                                       \
    {                                                                        \
        ::Nova::Platform::ReportAssertionFailure("unreachable code", __FILE__,\
                                                 __LINE__, __func__,      \
                                                 __VA_ARGS__);              \
        __assume(0);                                                         \
    } while (false)