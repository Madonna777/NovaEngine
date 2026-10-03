// ===========================================================================
//  LogMacros.h
// ---------------------------------------------------------------------------
//  Call-site sugar over Log::Write.
//
//  WHY MACROS AND NOT FUNCTIONS: they capture __FILE__/__LINE__/__func__ at the
//  CALL SITE. A function cannot - a wrapper would record the location of the
//  wrapper's own log call, so every record in the log file would point at
//  Log.h instead of at the line that emitted it. That is the single reason
//  every logger in the world with source attribution uses macros.
//
//  Included at the BOTTOM of <Core/Log.h>, deliberately. Macros are only
//  checked when they are expanded, so Log.h can define Log completely and then
//  pull these in; the expansion references ::Nova::Log::Write, which by then
//  exists. Doing it in this order avoids a circular include, which would
//  otherwise need a second header just to hold one enum.
//
//  DO NOT INCLUDE THIS FILE DIRECTLY. It is an implementation detail of
//  <Core/Log.h>, which is what pulls it in along with everything it needs.
// ===========================================================================
#pragma once

// ---------------------------------------------------------------------------
//  Channel routing. NOVA_LOG is the general form; the macros below are
//  shorthands for the two channels that cover essentially all call sites.
//
//  WHY NOVA_* IS THE CORE CHANNEL: engine code is the overwhelming majority of
//  call sites, and defaulting it there means writing NOVA_INFO(...) rather than
//  NOVA_LOG(LogChannel::Core, LogLevel::Info, ...). The cost is that the
//  shorthand exists at all - and the benefit is that when a client-code file
//  accidentally logs to the engine channel it is visible in review, because it
//  had to omit a prefix rather than pick the wrong one.
// ---------------------------------------------------------------------------

#define NOVA_LOG(channel, level, ...)                                        \
    ::Nova::Log::Write((channel), (level),                                    \
                       ::spdlog::source_loc{__FILE__, __LINE__, __func__},    \
                       __VA_ARGS__)

// Engine channel - the default for everything under Engine/.
#define NOVA_TRACE(...)    NOVA_LOG(::Nova::LogChannel::Core, ::Nova::LogLevel::Trace, __VA_ARGS__)
#define NOVA_DEBUG(...)    NOVA_LOG(::Nova::LogChannel::Core, ::Nova::LogLevel::Debug, __VA_ARGS__)
#define NOVA_INFO(...)     NOVA_LOG(::Nova::LogChannel::Core, ::Nova::LogLevel::Info, __VA_ARGS__)
#define NOVA_WARN(...)     NOVA_LOG(::Nova::LogChannel::Core, ::Nova::LogLevel::Warning, __VA_ARGS__)
#define NOVA_ERROR(...)    NOVA_LOG(::Nova::LogChannel::Core, ::Nova::LogLevel::Error, __VA_ARGS__)

// Critical and its client twin are wrapped in do/while so each expansion is a
// single statement: it cannot break an unbraced if/else, and it cannot become
// the dangling else of a caller's. Critical in particular does not return, so
// callers write NOVA_CRITICAL(...); and expect the engine to be on its way
// down - which must stay true even inside an if with no braces.
#define NOVA_CRITICAL(...)                                                    \
    do                                                                        \
    {                                                                         \
        NOVA_LOG(::Nova::LogChannel::Core, ::Nova::LogLevel::Critical, __VA_ARGS__); \
    } while (false)

// Client channel - game, tool and sandbox code.
#define NOVA_CLIENT_TRACE(...)    NOVA_LOG(::Nova::LogChannel::Client, ::Nova::LogLevel::Trace, __VA_ARGS__)
#define NOVA_CLIENT_DEBUG(...)    NOVA_LOG(::Nova::LogChannel::Client, ::Nova::LogLevel::Debug, __VA_ARGS__)
#define NOVA_CLIENT_INFO(...)     NOVA_LOG(::Nova::LogChannel::Client, ::Nova::LogLevel::Info, __VA_ARGS__)
#define NOVA_CLIENT_WARN(...)     NOVA_LOG(::Nova::LogChannel::Client, ::Nova::LogLevel::Warning, __VA_ARGS__)
#define NOVA_CLIENT_ERROR(...)    NOVA_LOG(::Nova::LogChannel::Client, ::Nova::LogLevel::Error, __VA_ARGS__)
#define NOVA_CLIENT_CRITICAL(...)                                               \
    do                                                                        \
    {                                                                         \
        NOVA_LOG(::Nova::LogChannel::Client, ::Nova::LogLevel::Critical, __VA_ARGS__); \
    } while (false)