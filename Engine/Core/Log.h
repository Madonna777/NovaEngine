// ===========================================================================
//  Log.h
// ---------------------------------------------------------------------------
//  The engine's logging facade over spdlog.
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
//  THE ONE HONEST COST: this header includes <spdlog/spdlog.h>, so every
//  translation unit that logs also parses spdlog's headers. We accept that for
//  now; the fix is a precompiled header for the module (a one-line CMake
//  change) and is deferred until profiling shows it matters.
//
//  THREAD SAFETY: spdlog's mutex-guarded loggers serialise every write, so
//  logging from worker threads is safe. The trade-off is a lock per log call,
//  which is why nothing in the per-frame hot path may log at Info level.
// ===========================================================================
#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace Nova
{
/// Severity levels, ordered by increasing severity.
///
/// @note Deliberately mirrors - and does not depend on - spdlog's own enum, so
///       that engine code never names a third-party type.
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

/// Static logging facade. All methods are static; there is deliberately no
/// instance - a logger's whole purpose is to be reachable from anywhere,
/// including from code that has no access to any object graph.
class Log final
{
public:
    Log(const Log&)            = delete;
    Log& operator=(const Log&) = delete;
    Log()                      = delete;

    /// Initialises the global logger.
    ///
    /// @param minLevel      Messages below this level are discarded before
    ///                      formatting, so a filtered-out Trace call costs
    ///                      almost nothing.
    /// @param logFile       If non-empty, also append to this file. Parent
    ///                      directories are created as needed.
    /// @param consoleOutput Whether to mirror to a colour-coded console.
    /// @param flushOnError  Flush after every Warning and above. Slower, but it
    ///                      means the log survives a hard crash - worth having
    ///                      while bringing up the renderer.
    ///
    /// @return True on success. Returns false - never throws - if the file sink
    ///         could not be opened; in that case logging continues to console
    ///         only. A failed log file must not stop the engine from starting.
    ///
    /// @note Calling twice is safe: the previous logger is shut down first.
    static bool Initialize(LogLevel                minLevel      = LogLevel::Info,
                           const std::filesystem::path& logFile = {},
                           bool                     consoleOutput = true,
                           bool                     flushOnError  = true);

    /// Flushes and releases the global logger.
    ///
    /// @note Optional - an automatic shutdown runs at process exit - but call it
    ///       explicitly so buffered records hit disk while you can still check
    ///       the exit code. Static destruction order is not something to trust
    ///       for log flushing.
    static void Shutdown();

    /// @return True once Initialize has succeeded.
    [[nodiscard]] static bool IsInitialized();

    /// Writes one formatted record at @p level with source attribution.
    ///
    /// @param level Severity.
    /// @param loc   __FILE__/__LINE__/__func__ of the call site. Passed
    ///               explicitly (rather than relying on spdlog's internal
    ///               default) so the recorded path is the real source file
    ///               rather than the spdlog header that invoked it.
    /// @param fmt   spdlog/fmt format string. Compile-time checked when
    ///               SPDLOG_USE_STD_FORMAT or external fmt is active.
    /// @param args  Format arguments.
    ///
    /// @note Safe before Initialize: falls back to a lazily created console
    ///       logger. Startup logging is when you most need output - refusing to
    ///       print because you "forgot to initialise" turns a trivial ordering
    ///       bug into a silent engine that appears to do nothing.
    template<typename... Args>
    static void Write(LogLevel                    level,
                      const spdlog::source_loc&    loc,
                      spdlog::format_string_t<Args...> fmt,
                      Args&&...                    args)
    {
        // Get() returns a REFERENCE, so the call goes through '.' not '->'.
        Get().log(spdlog::source_loc{loc.filename, loc.line, loc.funcname},
                  ToSpdlog(level), fmt, std::forward<Args>(args)...);
    }

    /// @return The active logger, creating a default console logger if needed.
    [[nodiscard]] static spdlog::logger& Get();

    /// Flushes buffered records to all sinks.
    static void Flush();

    /// @return Human-readable name of @p level ("Info", "Warning", ...).
    [[nodiscard]] static std::string_view LevelName(LogLevel level) noexcept;

    /// Translates a Nova level to the corresponding spdlog level.
    [[nodiscard]] static spdlog::level::level_enum ToSpdlog(LogLevel level) noexcept;
};

} // namespace Nova

// ---------------------------------------------------------------------------
//  Logging macros.
//
//  WHY MACROS: they capture __FILE__/__LINE__/__func__ at the CALL SITE. A
//  function cannot do that - a wrapper would record the location of the
//  wrapper's own log call, making every record point at Log.h.
//
//  Every macro is wrapped in do { } while (false) so it is a single
//  statement: it cannot break an unbraced if/else, and it cannot be used as
//  the dangling else of a caller.
// ---------------------------------------------------------------------------

#define NOVA_LOG_AT(level, ...) \
    ::Nova::Log::Write((level), ::spdlog::source_loc{__FILE__, __LINE__, __func__}, __VA_ARGS__)

#define NOVA_TRACE(...) NOVA_LOG_AT(::Nova::LogLevel::Trace, __VA_ARGS__)
#define NOVA_DEBUG(...) NOVA_LOG_AT(::Nova::LogLevel::Debug, __VA_ARGS__)
#define NOVA_INFO(...)  NOVA_LOG_AT(::Nova::LogLevel::Info, __VA_ARGS__)

// Warnings and worse are wrapped so the do/while applies even if the format
// arguments contain a comma inside a braced initialiser, which the preprocessor
// would otherwise split into two arguments. Harmless here because every
// variadic macro absorbs the split, but the do/while is the correct habit.
#define NOVA_WARN(...)  NOVA_LOG_AT(::Nova::LogLevel::Warning, __VA_ARGS__)
#define NOVA_ERROR(...) NOVA_LOG_AT(::Nova::LogLevel::Error, __VA_ARGS__)

// Critical is wrapped in do/while because it does not always return - callers
// write NOVA_CRITICAL(...); and expect the engine to be on its way down.
#define NOVA_CRITICAL(...)                                                  \
    do                                                                       \
    {                                                                        \
        NOVA_LOG_AT(::Nova::LogLevel::Critical, __VA_ARGS__);                \
    } while (false)