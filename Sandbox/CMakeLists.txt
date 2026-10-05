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

# WHY TWO LINKS AND NOT ONE: Sandbox is the executable, so it is where the
# decision to render through D3D12 is actually made. Nova::Renderer already links
# Nova::Core transitively, but naming Core here too keeps the direct dependency
# visible - if the Renderer ever stopped depending on Core, Sandbox would still
# be honest about the fact that it logs through it.
target_link_libraries(Sandbox PRIVATE Nova::Core Nova::Renderer Nova::Math Nova::Scene)

# ---------------------------------------------------------------------------
#  SHADERS
# ---------------------------------------------------------------------------
#  WHY THE COPY: a shader is loaded at RUNTIME by path, so it has to sit next to
#  the executable. A post-build copy is the standard mechanism for that.
#
#  WHY copy_directory AND NOT A PER-FILE copy COMMAND: this stays correct when
#  shaders gain subdirectories (Shaders/Post/Blur.hlsl). An explicit list of
#  files has to be updated by hand at that point, and the failure mode is
#  silent - the build succeeds and the renderer fails to find the shader at
#  load time. Copying a directory of a few kilobytes is not a measurable cost.
#
#  WHY Shaders/ IS NOT A SOURCE DIRECTORY: shaders are runtime assets, not
#  compiled sources. Marking them as sources would put them in the IDE's file
#  list, which is misleading, and tempts someone into adding them to a target.
set(NOVA_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Shaders")

if(EXISTS "${NOVA_SHADER_DIR}")
    add_custom_command(TARGET Sandbox POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${NOVA_SHADER_DIR}"
                "$<TARGET_FILE_DIR:Sandbox>/Shaders"
        COMMENT "Staging shaders into $<TARGET_FILE_DIR:Sandbox>/Shaders"
        VERBATIM)
else()
    message(FATAL_ERROR
        "Shaders directory not found at ${NOVA_SHADER_DIR}. The renderer loads "
        "shaders by path at runtime, so this must exist.")
endif()