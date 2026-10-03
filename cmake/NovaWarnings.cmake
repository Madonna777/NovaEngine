# ===========================================================================
#  NovaWarnings.cmake
# ===========================================================================
#  Central warning policy.
#
#  Rationale: an engine is a codebase you will still be reading in two years.
#  Warning text is the cheapest documentation review you will ever get, so we
#  enable a strict set once, centrally, and fix what it finds.
# ===========================================================================

include_guard(GLOBAL)

option(NOVA_WARNINGS_AS_ERRORS
       "Treat compiler warnings as errors" OFF)

function(nova_apply_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            # --- Strictness ------------------------------------------------
            /W4                 # Project default is /W3; /W4 surfaces
                                # unsigned/size mismatches and bad casts.
            /permissive-        # Standard-conforming. MSVC's default still
                                # permits non-conforming extensions; this
                                # prevents code from accidentally relying
                                # on them.

            # --- Correctness (these three are not style choices) ---------
            /Zc:preprocessor    # Conformant preprocessor. Required for
                                # __VA_OPT__ and for vcpkg header macros;
                                # without it MSVC's legacy preprocessor
                                # silently mis-expands them.
            /Zc:__cplusplus     # Report the true __cplusplus value (199711L
                                # by default, which lies to every library
                                # feature test, including <filesystem>).
            /utf-8              # Mark source AND execution charset UTF-8.
                                # MANDATORY on this machine: the user profile
                                # path contains non-ASCII characters
                                # (C:\Users\<cyrillic>\...), so any
                                # MSVC-relative path may be non-ASCII. With
                                # the default codepage, string literals and
                                # narrow paths get mojibake, and log output
                                # containing filenames becomes unreadable.

            # --- Diagnostics quality ------------------------------------
            /EHsc               # Standard C++ exception semantics. Required
                                # because Core throws std::logic_error, and
                                # because any C++ exception crossing into
                                # D3D12 COM code must be compiled with
                                # synchronous unwinding.
            /FC                 # Absolute __FILE__ in diagnostics. Also makes
                                # __FILE__ usable as a log source location.
            /bigobj             # D3D12 + DirectXMath headers expand to
                                # object files beyond the legacy 65k-section
                                # limit. Fails the build otherwise.

            # --- Build speed (no semantic effect) ------------------------
            /MP                 # Parallel compilation. MSBuild-only; ignored
                                # under Ninja, where parallelism comes from
                                # -j instead.
            /Gy                 # Function-level linking: lets the linker
                                # drop unreferenced functions and fold
                                # identical COMDATs.
            /Gw                 # Optimize global data for faster link.

            # --- Suppressions (each one justified) -----------------------
            /wd4100             # "unreferenced formal parameter". A stub or
                                # callback must keep a parameter to satisfy an
                                # interface; the parameter's absence is not a
                                # defect.
            /wd4127             # "conditional expression is constant". Fired
                                # by __has_include and other compile-time
                                # feature macros we cannot change.
            /wd4324             # "structure was padded due to alignment
                                # specifier". This one is *expected*: simd /
                                # DirectXMath types and D3D12 constant-buffer
                                # structs are explicitly over-aligned, and the
                                # padding is a hard requirement of the
                                # hardware layout, not a bug.
            /wd4702             # "unreachable code". Our switches are
                                # exhaustively handled then followed by a
                                # default that trips the compiler.
            /wd4996             # "deprecated declaration". Several Win32 APIs
                                # we need (e.g. GetVersionEx) are deprecated
                                # but have no modern equivalent for our use.
            /wd5105             # "macro expansion producing 'defined'" -
                                # originates in third-party headers we do not
                                # control.

            # Diagnostic output goes to stderr so build logs stay parseable
            # by CI error collectors.
            /diagnostics:caret
        )

        if(NOVA_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()

    elseif(CXX_COMPILER_ID MATCHES "GNU|Clang")
        # Kept modest: the engine targets MSVC, so these exist to catch
        # genuinely portable bugs (uninitialised members, shadowing) during
        # CI on a cheaper runner, not to police style.
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wnon-virtual-dtor
            $<$<CXX_COMPILER_ID:GNU>:-Wduplicated-cond>
            $<$<CXX_COMPILER_ID:GNU>:-Wduplicated-branches>
            $<$<CXX_COMPILER_ID:GNU>:-Wlogical-op>
            -fno-omit-frame-pointer)   # keep frame pointers for Tracy/sampling
        if(NOVA_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()