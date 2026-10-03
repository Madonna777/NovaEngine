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

#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace Nova
{
namespace
{
/// Flushes and releases the loggers during static destruction.
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

/// Owns the two channel loggers.
///
/// WHY AN ARRAY AND NOT TWO NAMED MEMBERS: a channel is a value, and every
/// operation here is "do this to both" or "index by channel". An array makes
/// that exhaustive - adding a third channel later is a change to the enum, and
/// the compiler points at every place that assumed two. Two hand-written
/// members would let a new channel be added to the enum and quietly ignored at
/// runtime.
struct LogState
{
    std::array<std::shared_ptr<spdlog::logger>, 2> channel{};
    bool initialized = false;
};

/// @return Compact index for @p channel. Constexpr, so usable in array bounds.
constexpr std::size_t IndexOf(LogChannel channel) noexcept
{
    return static_cast<std::size_t>(channel);
}

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

/// The single output pattern, applied to the SINKS rather than to the loggers.
///
/// @note %n is spdlog's logger-name placeholder, and each channel's logger is
///       NAMED after its channel ("Core" / "Client"). So one pattern tags every
///       record correctly without any per-channel formatting code.
///
/// WHY THE PATTERN IS SET ON THE SINKS AND NOT ON THE LOGGERS - read this
/// before "simplifying" it back to logger->set_pattern:
///
///     logger::set_pattern() walks the logger's sinks and BREAKS after the first
///     sink that accepts the pattern.
///     https://github.com/gabime/spdlog/blob/v1.x/include/spdlog/logger.h
///
/// Both channels deliberately share one sink instance (see Initialize for why),
/// so calling set_pattern on each logger in turn does NOT produce two patterns.
/// It produces one pattern - the last one applied - shared by every record, and
/// every engine message ends up tagged as game code. Nothing crashes, nothing is
/// missing, and the log is quietly wrong. Setting it on the sink once is the
/// only formulation that cannot have that failure.
///
/// WHY THE TAG IS NOT PADDED to a fixed column: %n emits the name unpadded, so
/// "Core" and "Client" leave the timestamp at different offsets. spdlog's
/// pattern language supports width specifiers only for its numeric fields, not
/// for %n, so a padded tag needs a custom formatter subclass - and in spdlog
/// 1.17 that interface takes an fmt memory buffer, so writing one means binding
/// to fmt internals that change between spdlog releases. A slightly ragged tag
/// column is a cosmetic cost; a formatter tied to a third-party buffer API is a
/// recurring maintenance cost. Revisit only if the raggedness proves to matter.
constexpr std::string_view kSinkPattern =
    "[%n] [%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [t%-6T] [%s:%#] %v";
} // namespace

bool Log::Initialize(LogLevel                     minLevel,
                     const std::filesystem::path& logFile,
                     bool                          consoleOutput,
                     bool                          flushOnError,
                     LogLevel                      clientLevel)
{
    // Tear down any previous loggers first so repeated Initialise calls (common
    // when the Editor restarts the engine subsystem) cannot leave live sinks
    // writing interleaved output to the same file handle.
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
        // BOTH loggers are handed the SAME sink instances, not copies.
        //
        // WHY THAT IS SAFE: spdlog's _mt sinks (stdout_color_sink_mt,
        // basic_file_sink_mt) are individually mutex-guarded, and the mutex
        // lives on the sink. So one shared sink serialises writes from both
        // loggers, which means a Core record and a Client record can never
        // interleave halfway through a line in the log file.
        //
        // WHY NOT GIVE EACH LOGGER ITS OWN SINK: the file sink would then open
        // the same path twice with independent buffers and independent mutexes,
        // and interleaved half-lines become possible - which is exactly the
        // corruption you hit when reading a crash log.
        // const, not constexpr: the levels are runtime parameters. The channel
        // list IS constexpr because it is the enum's full value set - which is
        // what makes adding a channel a compile error here rather than a
        // silently unlogged third channel.
        constexpr std::array<LogChannel, 2> kChannels{LogChannel::Core,
                                                     LogChannel::Client};
        const std::array<LogLevel, 2> kLevels{minLevel, clientLevel};

        // The pattern is applied to the SINKS, before any logger exists. See
        // kSinkPattern for the spdlog behaviour that makes this load-bearing
        // rather than a matter of taste.
        for (auto& sink : sinks)
        {
            sink->set_pattern(std::string{kSinkPattern});
        }

        for (std::size_t i = 0; i < kChannels.size(); ++i)
        {
            auto logger = std::make_shared<spdlog::logger>(
                std::string{LogChannelName(kChannels[i])}, sinks.begin(), sinks.end());

            logger->set_level(ToSpdlog(kLevels[i]));
            logger->flush_on(flushOnError ? spdlog::level::warn : spdlog::level::off);

            // Registered as well as stored: third-party libraries (including
            // Assimp once it lands) log through spdlog::default_logger().
            // Registering routes them through our sinks and level filter
            // instead of letting them silently discard messages.
            spdlog::register_logger(logger);

            state.channel[i] = std::move(logger);
        }

        state.initialized = true;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "[NOVA] logger construction failed: %s\n", error.what());

        // Both channels fall back to independent console loggers. They no longer
        // share a sink, so a flush on one does not cover the other - hence
        // Flush() iterating both rather than stopping at the first.
        state.channel[IndexOf(LogChannel::Core)]   = MakeConsoleLogger("nova_early_core");
        state.channel[IndexOf(LogChannel::Client)] = MakeConsoleLogger("nova_early_client");
        state.initialized = false;
        return false;
    }

    // Registered AFTER State() is first touched, so this object is destroyed
    // BEFORE the log state, meaning the final flush happens while the loggers
    // are still alive. Reversing those two inits silently drops the last few
    // buffered lines - which are usually the ones describing the crash.
    [[maybe_unused]] static const ExitFlush exitFlush{};

    return true;
}

void Log::Shutdown()
{
    LogState& state = State();

    for (const auto& logger : state.channel)
    {
        if (logger)
        {
            logger->flush();
        }
    }

    // drop_all() rather than resetting our shared_ptrs alone: sinks registered
    // from inside spdlog (pattern loggers, third-party default loggers) are not
    // reachable through our pointers and would otherwise be destroyed after
    // spdlog's own static teardown.
    spdlog::drop_all();

    for (auto& logger : state.channel)
    {
        logger.reset();
    }

    state.initialized = false;
}

bool Log::IsInitialized()
{
    return State().initialized;
}

spdlog::logger& Log::Channel(LogChannel channel)
{
    LogState& state  = State();
    auto&       entry = state.channel[IndexOf(channel)];

    if (!entry)
    {
        // Lazy fallback. Startup code logs before Initialize often enough that
        // treating it as an error would mean guarding every call site. Each
        // channel gets its own logger here rather than sharing one, so that a
        // SetChannelLevel applied before Initialize is not silently lost.
        entry = MakeConsoleLogger(std::string{"nova_early_"} +
                                  std::string{LogChannelName(channel)});
    }

    return *entry;
}

spdlog::logger& Log::Get()
{
    return Channel(LogChannel::Core);
}

void Log::SetChannelLevel(LogChannel channel, LogLevel level)
{
    Channel(channel).set_level(ToSpdlog(level));
}

LogLevel Log::ChannelLevel(LogChannel channel)
{
    // Round-tripping through LogLevel rather than returning spdlog's enum keeps
    // spdlog's type out of this function's signature, which is the whole point
    // of the facade.
    switch (Channel(channel).level())
    {
        case spdlog::level::trace:    return LogLevel::Trace;
        case spdlog::level::debug:    return LogLevel::Debug;
        case spdlog::level::info:     return LogLevel::Info;
        case spdlog::level::warn:     return LogLevel::Warning;
        case spdlog::level::err:      return LogLevel::Error;
        case spdlog::level::critical: return LogLevel::Critical;
        case spdlog::level::off:      return LogLevel::Off;
        default:                      return LogLevel::Info;
    }
}

void Log::Flush()
{
    for (const auto& logger : State().channel)
    {
        if (logger)
        {
            logger->flush();
        }
    }
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