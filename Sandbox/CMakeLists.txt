# ===========================================================================
#  Sandbox - the engine's test application
# ===========================================================================
#  WHAT THIS IS FOR
#  ----------------
#  Sandbox is a scratchpad, and its most important job is to stay minimal. Its
#  purpose is to exercise an engine feature in isolation so that when something
#  misbehaves, the cause is inside one subsystem with no gameplay logic in the
#  way.
#
#  Two apps exist on purpose: Sandbox is disposable and gets rewritten weekly,
#  while Editor is long-lived. Keeping them separate stops "just testing one
#  thing" from ending up as permanent code in the tool you actually maintain.
# ===========================================================================

# WHY NOT THE WIN32 SUBSYSTEM YET: this entry point is plain console main().
# MSVC's linker prefers WinMain under /SUBSYSTEM:WINDOWS and falls back to
# mainCRTStartup, which works but produces an app that has no console and no
# window - strictly worse than what we want here. The Win32 entry point arrives
# with the renderer's GLFW window, where there is a window to justify it.
add_executable(Sandbox src/main.cpp)

target_link_libraries(Sandbox PRIVATE Nova::Core)