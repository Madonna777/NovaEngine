# ===========================================================================
#  Editor - long-lived developer tool
# ===========================================================================
#  Currently a placeholder. ImGui arrives with the Renderer milestone (see the
#  "editor" feature in the root vcpkg.json), because the editor's docking and
#  viewport are meaningless without a swap chain to render into.
#
#  WHY THIS IS A SEPARATE APP FROM SANDBOX: Sandbox is disposable and gets
#  rewritten; Editor accumulates. Keeping the tool you maintain isolated from
#  the scratchpad means a bad afternoon in Sandbox can never destabilise the
#  thing you actually use to work.
# ===========================================================================

add_executable(Editor src/main.cpp)

target_link_libraries(Editor PRIVATE Nova::Core)