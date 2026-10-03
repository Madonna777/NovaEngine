// ===========================================================================
//  Log.cpp
// ---------------------------------------------------------------------------
//  Implementation of the logging facade.
//
//  This is the ONLY file in Engine/Core that includes spdlog's sink headers.
//
//  WHY ANGLE-BRACKET INCLUDES FOR THE MODULE'S OWN HEADERS: the quotes form
//  resolves relative to this file, which would mean an implementation file can
//  only find headers that sit beside it. Using the same <Core/...> path that
//  consumers use means the public include root is exercised by the module
//  itself - if that path breaks, this file fails to compile rather than a
//  consumer failing mysteriously three modules away.
// ===========================================================================

#include <Core/Log.h>
#include <Core/Platform.h>
#include <Core/Assert.h>

#include <array>
#include <cstdio>
#include <system_error>
#include <utility>
#include <vector>

namespace Nova
{
namespace
{
/// Flushes and releases the logger during static destruction.
///
/// WHY THIS IS AN OBJECT RATHER THAN std::atexit: atexit callbacks run in
/// reverse registration order and are not interleaved with C++ static
/// destructors in a defined way. An object with a destructor is ordered by the
/// C++ runtime relative to the other statics in this file, which is the only
/// ordering we can actually rely on.
struct ExitFlush
{
    ~ExitFlush() { Log::Shutdown(); }
};
/// Owns the single global logger.
///
/// WHY A shared_ptr AND NOT A UNIQUE_ptr: spdlog's sink and registry machinery
/// hands the logger to internal machinery that only holds weak ownership, and
/// spdlog's own APIs return shared_ptr. Storing shared_ptr here matches the
/// type spdlog already expects, so there is no conversion and no accidental
/// double-free when the registry drops its last reference at shutdown.
struct LogState
{
    std::shared_ptr<spdlog::logger> logger;
    bool initialized = false;
};

/// @return Process-wide log state.
///
/// @note A function-local static rather than a namespace-scope variable: it is
///       constructed on first use, which sidesteps static initialisation order
///       fiasco entirely. Logging from another static object's constructor is
///       safe, which it would not be with a global.
LogState& State()
{
    static LogState state;
    return state;
}

/// Creates a console-only logger used before Initialize, and as the fallback
/// when a file sink cannot be opened.
std::shared_ptr<spdlog::logger> MakeConsoleLogger(std::string_view name)
{
    auto logger = std::make_shared<spdlog::logger>(
        std::string{name},
        std::make_shared<spdlog::sinks::stdout_color_sink_mt>());

    // Trace and Debug are off by default: enabling them by default turns an
    // unconfigured engine into log spam that hides the warnings that matter.
    logger->set_level(spdlog::level::info);
    logger->flush_on(spdlog::level::err);
    return logger;
}
} // namespace

bool Log::Initialize(LogLevel                    minLevel,
                     const std::filesystem::path& logFile,
                     bool                         consoleOutput,
                     bool                         flushOnError)
{
    // Tear down any previous logger first so repeated Initialise calls (common
    // when the Editor restarts the engine subsystem) cannot leave two live
    // sinks writing interleaved output to the same file handle.
    Shutdown();

    LogState& state = State();

    // NOTE: the abstract sink base class lives in spdlog::sinks, not spdlog.
    // Writing spdlog::sink does not compile.
    std::vector<std::shared_ptr<spdlog::sinks::sink>> sinks;

    if (consoleOutput)
    {
        sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    }

    if (!logFile.empty())
    {
        bool sinkReady = false;

        // std::filesystem throws filesystem_error on access-denied and on
        // invalid paths. Creating the log directory must never propagate an
        // exception out of initialisation - that would abort engine startup
        // over a diagnostic aid. Everything here is contained.
        try
        {
            if (logFile.has_parent_path())
            {
                std::filesystem::create_directories(logFile.parent_path());
            }

            auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
                logFile.string(), /*truncate=*/false);

            // NOTE ON ENCODING: this port is built with SPDLOG_WCHAR_FILENAMES
            // off, so the path arrives as a narrow std::string encoded in the
            // active ANSI code page. If the build path contains non-ANSI
            // characters (this machine's TEMP path does), the file open fails
            // and we fall through to console-only. The fix is to rebuild spdlog
            // with the wchar feature and pass a wide path; deliberately not
            // done yet, since losing the file is non-fatal.
            sinks.push_back(std::move(fileSink));
            sinkReady = true;
        }
        catch (const std::exception& error)
        {
            std::fprintf(stderr,
                         "[NOVA] log file unavailable (%s); continuing to console only\n"
                         "[NOVA]   path : %s\n",
                         error.what(), logFile.string().c_str());
        }

        if (!sinkReady && sinks.empty())
        {
            // Both sinks unavailable: guarantee the engine is never mute.
            sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
        }
    }

    if (sinks.empty())
    {
        sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    }

    try
    {
        auto logger = std::make_shared<spdlog::logger>(std::string{"nova"},
                                                       sinks.begin(), sinks.end());
        logger->set_level(ToSpdlog(minLevel));
        logger->flush_on(flushOnError ? spdlog::level::warn : spdlog::level::off);
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [t%-6T] [%s:%#] %v");

        // Registered as well as stored: third-party libraries (including
        // Assimp once it lands) log through spdlog::default_logger(). Registering
        // routes them through our sinks and level filter instead of letting them
        // silently discard messages.
        spdlog::register_logger(logger);

        state.logger     = std::move(logger);
        state.initialized = true;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "[NOVA] logger construction failed: %s\n", error.what());
        state.logger      = MakeConsoleLogger("nova_console");
        state.initialized = false;
        return false;
    }

    // Registered AFTER State() is first touched, so this object is destroyed
    // BEFORE the log state, meaning the final flush happens while the logger is
    // still alive. Reversing those two inits silently drops the last few
    // buffered lines - which are usually the ones describing the crash.
    [[maybe_unused]] static const ExitFlush exitFlush{};

    return true;
}

void Log::Shutdown()
{
    LogState& state = State();

    if (state.logger)
    {
        state.logger->flush();
    }

    // drop_all() rather than resetting our shared_ptr alone: sinks registered
    // from inside spdlog (pattern loggers, third-party default loggers) are not
    // reachable through our pointer and would otherwise be destroyed after
    // spdlog's own static teardown.
    spdlog::drop_all();

    state.logger      = nullptr;
    state.initialized = false;
}

bool Log::IsInitialized()
{
    return State().initialized;
}

spdlog::logger& Log::Get()
{
    LogState& state = State();
    if (!state.logger)
    {
        // Lazy fallback. Startup code logs before Initialize often enough that
        // treating it as an error would mean guarding every call site.
        state.logger = MakeConsoleLogger("nova_early");
    }
    return *state.logger;
}

void Log::Flush()
{
    if (State().logger)
    {
        State().logger->flush();
    }
}

std::string_view Log::LevelName(LogLevel level) noexcept
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

spdlog::level::level_enum Log::ToSpdlog(LogLevel level) noexcept
{
    switch (level)
    {
        case LogLevel::Trace:    return spdlog::level::trace;
        case LogLevel::Debug:    return spdlog::level::debug;
        case LogLevel::Info:     return spdlog::level::info;
        case LogLevel::Warning:  return spdlog::level::warn;
        case LogLevel::Error:    return spdlog::level::err;
        case LogLevel::Critical: return spdlog::level::critical;
        case LogLevel::Off:      return spdlog::level::off;
    }
    return spdlog::level::info;
}

} // namespace Nova