// ===========================================================================
//  LogTypes.h
// ---------------------------------------------------------------------------
//  The vocabulary types of the logging subsystem: severity and routing channel.
//
//  WHY THESE LIVE IN THEIR OWN HEADER, SEPARATE FROM <Core/Log.h>
//  --------------------------------------------------------------
//  Two reasons, and the second is the real one.
//
//  1. NO THIRD-PARTY DEPENDENCY. LogLevel is named in the assertion handler,
//     in the Editor's severity filter dropdown, and eventually in the crash
//     reporter. None of those need spdlog - they need to know that "Warning" is
//     a thing. Including <Core/Log.h> for an enum drags <spdlog/spdlog.h> and
//     its sink headers into every one of those translation units, for no reason.
//
//  2. LOG.h IS ALREADY AT ITS SIZE LIMIT. The facade plus its macros fills the
//     budget on its own. Severity is a different concern from the logger that
//     consumes it, and a file that grows past its limit is a file whose next
//     change is hard to review.
//
//  This header is included BY <Core/Log.h>, so consumers keep writing
//  `#include <Core/Log.h>` and keep naming Nova::LogLevel exactly as before.
//  Nothing outside Log.cpp needs to include this directly.
// ===========================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Nova
{
/// Severity levels, ordered by increasing severity.
///
/// @note Deliberately mirrors - and does not depend on - spdlog's own enum, so
///       that engine code never names a third-party type. That independence is
///       what would let the logging backend be swapped without touching a
///       single call site.
enum class LogLevel : std::uint8_t
{
    Trace,    ///< Per-frame, per-object detail. Disabled in normal builds.
    Debug,    ///< Developer diagnostics; on in Debug and development builds.
    Info,     ///< Normal operational milestones: device created, scene loaded.
    Warning,  ///< Recoverable problems; something is wrong but the frame continues.
    Error,    ///< A subsystem failed. The engine may continue, degraded.
    Critical, ///< The engine cannot continue. Imminent shutdown.
    Off       ///< Suppresses all output.
};

/// Which logger a record is routed to.
///
/// WHY TWO CHANNELS RATHER THAN ONE LOGGER WITH A CATEGORY STRING: the value
/// is not the label in the output, it is that each channel has its OWN LEVEL
/// FILTER. That buys two independent controls without touching a call site:
///
///   * Shipping build.  Engine at Info, client at Warning. The player's bug
///     report contains the engine's lifecycle milestones and every error the
///     game code raised, without thousands of asset-loading traces burying them.
///   * Debugging the engine. Engine at Trace, client at Off. The engine's
///     per-frame trace is readable, uncontaminated by gameplay logging.
///
/// A category field on a single logger cannot do either: one filter applies to
/// everything at once, so turning the engine up also turns the game up.
///
/// @note "Client" means application/game code, not a network client. It is the
///       half of the program that is not the engine. Everything under
///       Engine/ logs to Core; everything under Sandbox/ and Editor/ logs to
///       Client.
enum class LogChannel : std::uint8_t
{
    Core,   ///< Engine internals. Every subsystem logs here by default.
    Client, ///< Game, tool and sandbox code.
};

/// @return Human-readable name of @p level ("Info", "Warning", ...).
///
/// @note constexpr and header-defined so that ChannelColumnWidth below can be
///       evaluated by the compiler. It is also why this is a free function in
///       this header rather than a member of Log: a constexpr member would drag
///       the whole spdlog-including facade into any caller that only wants to
///       measure a string.
[[nodiscard]] constexpr std::string_view LogLevelName(LogLevel level) noexcept
{
    switch (level)
    {
        case LogLevel::Trace:    return "Trace";
        case LogLevel::Debug:    return "Debug";
        case LogLevel::Info:     return "Info";
        case LogLevel::Warning:  return "Warning";
        case LogLevel::Error:    return "Error";
        case LogLevel::Critical: return "Critical";
        case LogLevel::Off:      return "Off";
    }
    return "Unknown";
}

/// @return Human-readable name of @p channel ("Core", "Client").
///
/// @note These names are not cosmetic. They become the spdlog logger names, and
///       the log pattern prints the logger name with %n, so this function is
///       literally what tags every line of every log file. Renaming a channel
///       here renames it in the output too, consistently.
[[nodiscard]] constexpr std::string_view LogChannelName(LogChannel channel) noexcept
{
    switch (channel)
    {
        case LogChannel::Core:   return "Core";
        case LogChannel::Client: return "Client";
    }
    return "Unknown";
}

} // namespace Nova