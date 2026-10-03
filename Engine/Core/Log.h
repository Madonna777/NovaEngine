// ===========================================================================
//  Log.h
// ---------------------------------------------------------------------------
//  The engine's logging facade over spdlog: two channels, one sink set.
//
//  WHY A FACADE INSTEAD OF CALLING spdlog DIRECTLY EVERYWHERE
//  -------------------------------------------------------------
//  Three reasons, in order of importance:
//
//  1. THE DEPENDENCY STAYS IN ONE PLACE. Once Engine/Core calls spdlog
//     directly, spdlog's types leak into every signature in the engine. At
//     that point you can never swap the logger, and every consumer of Core now
//     transitively needs spdlog on its include path. Keeping it behind this
//     header means only Core/Log.cpp needs to change if we ever move to a
//     different logging library.
//
//  2. LIFETIME IS HANDLED ONCE. A logger is a global with a destruction order.
//     Initialise it once, before any other engine subsystem, and every later
//     caller can log unconditionally without checking "am I up yet?". The
//     same question asked 400 times in the engine is 400 chances to forget.
//
//  3. THE CALL SITES ARE SHORT AND READABLE. NOVA_WARN("GPU budget exceeded:
//     {} ms", ms) versus logger->warn(...) with a format_string_t cast.
//
//  THE TWO CHANNELS ARE "Core" (engine) AND "Client" (game/tool/sandbox) -
//  see <Core/LogTypes.h> for why that split is worth two loggers rather than a
//  category string. The short version: each channel has its own level filter,
//  so the engine can run at Trace while client code is Off, or vice versa,
//  without touching a single call site.
//
//  THE ONE HONEST COST: this header includes <spdlog/spdlog.h>, so every
//  translation unit that logs also parses spdlog's headers. We accept that for
//  now; the fix is a precompiled header for the module (a one-line CMake
//  change) and is deferred until profiling shows it matters.
//
//  THREAD SAFETY: spdlog's mutex-guarded loggers serialise every write, so
//  logging from worker threads is safe - including a Core and a Client record
//  racing, because the two channels share one sink instance and the mutex
//  lives on the sink. The trade-off is a lock per log call, which is why
//  nothing in the per-frame hot path may log at Info level.
// ===========================================================================
#pragma once

#include <Core/LogTypes.h>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace Nova
{
/// Static logging facade. All methods are static; there is deliberately no
/// instance - a logger's whole purpose is to be reachable from anywhere,
/// including from code that has no access to any object graph.
class Log final
{
public:
    Log(const Log&)            = delete;
    Log& operator=(const Log&) = delete;
    Log()                      = delete;

    /// Initialises both channel loggers.
    ///
    /// @param minLevel      Minimum level for the Core channel. Records below
    ///                      it are discarded before formatting, so a filtered
    ///                      Trace call costs almost nothing.
    /// @param logFile       If non-empty, both channels also append to this
    ///                      file. Parent directories are created as needed.
    /// @param consoleOutput Whether to mirror to a colour-coded console.
    /// @param flushOnError  Flush after every Warning and above. Slower, but it
    ///                      means the log survives a hard crash - worth having
    ///                      while bringing up the renderer.
    /// @param clientLevel   Minimum level for the Client channel. Defaults to
    ///                      minLevel, which is what most callers want.
    ///                      Pass LogLevel::Off to silence client logging
    ///                      entirely without removing its call sites.
    ///
    /// @return True on success. Returns false - never throws - if the file sink
    ///         could not be opened; in that case logging continues to console
    ///         only. A failed log file must not stop the engine from starting.
    ///
    /// @note Calling twice is safe: the previous loggers are shut down first.
    static bool Initialize(LogLevel                     minLevel      = LogLevel::Info,
                           const std::filesystem::path& logFile      = {},
                           bool                          consoleOutput = true,
                           bool                          flushOnError  = true,
                           LogLevel                      clientLevel   = LogLevel::Info);

    /// Flushes and releases both channel loggers.
    ///
    /// @note Optional - an automatic shutdown runs at process exit - but call it
    ///       explicitly so buffered records hit disk while you can still check
    ///       the exit code. Static destruction order is not something to trust
    ///       for log flushing.
    static void Shutdown();

    /// @return True once Initialize has succeeded.
    [[nodiscard]] static bool IsInitialized();

    /// Writes one formatted record to @p channel at @p level.
    ///
    /// @param channel Which logger receives the record.
    /// @param level   Severity.
    /// @param loc     __FILE__/__LINE__/__func__ of the call site. Passed
    ///                 explicitly (rather than relying on spdlog's internal
    ///                 default) so the recorded path is the real source file
    ///                 rather than the spdlog header that invoked it.
    /// @param fmt     spdlog/fmt format string. Compile-time checked when
    ///                 SPDLOG_USE_STD_FORMAT or external fmt is active.
    /// @param args    Format arguments.
    ///
    /// @note Safe before Initialize: falls back to a lazily created console
    ///       logger. Startup logging is when you most need output - refusing to
    ///       print because you "forgot to initialise" turns a trivial ordering
    ///       bug into a silent engine that appears to do nothing.
    template<typename... Args>
    static void Write(LogChannel                    channel,
                      LogLevel                      level,
                      const spdlog::source_loc&     loc,
                      spdlog::format_string_t<Args...> fmt,
                      Args&&...                     args)
    {
        // Channel() returns a REFERENCE, so the call goes through '.' not '->'.
        Channel(channel).log(spdlog::source_loc{loc.filename, loc.line, loc.funcname},
                             ToSpdlog(level), fmt, std::forward<Args>(args)...);
    }

    /// @return The Core channel's logger, creating a default console logger if
    ///         needed. Equivalent to Channel(LogChannel::Core).
    [[nodiscard]] static spdlog::logger& Get();

    /// @return The named channel's logger, creating default console loggers if
    ///         needed.
    [[nodiscard]] static spdlog::logger& Channel(LogChannel channel);

    /// Overrides @p channel's level filter at runtime.
    ///
    /// @note Not for per-call decisions. The log call site already chose a
    ///       level; this is for subsystems that raise or lower their own
    ///       verbosity (an asset streamer switching to per-object tracing when
    ///       something is selected), which is a debugging tool, not a feature.
    static void SetChannelLevel(LogChannel channel, LogLevel level);

    /// @return @p channel's current minimum level.
    [[nodiscard]] static LogLevel ChannelLevel(LogChannel channel);

    /// Flushes buffered records on both channels to all sinks.
    static void Flush();

    /// Translates a Nova level to the corresponding spdlog level.
    [[nodiscard]] static spdlog::level::level_enum ToSpdlog(LogLevel level) noexcept;
};

} // namespace Nova

// Call-site sugar. Included last: the macros reference Log::Write, so they must
// follow the declaration above. See <Core/LogMacros.h> for why they are macros
// and why the routing defaults to the Core channel.
#include <Core/LogMacros.h>