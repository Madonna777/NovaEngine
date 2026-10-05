# ===========================================================================
#  NovaEngine - Engine Library
# ===========================================================================
#  WHY A STATIC LIBRARY
#  ---------------------
#  An engine library must be STATIC rather than SHARED for three reasons:
#
#  1. INLINING. D3D12 performance work lives in code that must inline across
#     module boundaries - vertex transforms, descriptor building, and the
#     per-draw submission path. A DLL boundary is an optimization barrier:
#     the compiler cannot inline through it, and every call becomes an
#     indirect jump plus a marshalling thunk. For a data-oriented renderer
#     this is measurable in frame time, not academic.
#
#  2. NO EXPORT MACROS. A shared engine would require __declspec(dllexport)
#     on every public symbol and dllexport/dllimport decorations on every
#     internal cross-DLL call. That noise infects the public headers and is a
#     permanent tax on every class added.
#
#  3. NO CRT BOUNDARY RISK. Objects crossing a DLL boundary may allocate in one
#     module's heap and free in another's. With static linking there is exactly
#     one heap, one CRT, and one place for a mismatch to hide.
#
#  TRADE-OFF WE ARE ACCEPTING FOR NOW: we cannot hot-reload engine code.
#  That matters for the Editor later. The migration path is deliberate -
#  turning a target to SHARED later requires only that we add export macros at
#  that point. Changing link type now, before there is code to churn, is much
#  cheaper than changing it after.
# ===========================================================================

# Shared public include root for every engine module. See nova_add_module for
# why headers are addressed as <Core/Log.h> rather than <Log.h>.
#
# WHY A PLAIN VARIABLE AND NOT A CACHE ENTRY: add_subdirectory copies the parent
# scope into the child, so this is visible inside Engine/Core's scope. A cache
# entry would also work but would pollute the cache with a value that is an
# implementation detail and must never be overridden by hand.
set(NOVA_PUBLIC_INCLUDE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

add_subdirectory(Core)
add_subdirectory(Math)
add_subdirectory(Renderer)

# ---------------------------------------------------------------------------
# Remaining modules, in dependency order. Each is added as it lands:
#
#   Math/      - no dependencies. Pure CPU math. Testable without a GPU. DONE.
#   Renderer/  - depends on Core. The D3D12 backend. DONE.
#   Scene/     - depends on Math, Core, Renderer (needs GPU-visible buffers).
#
# NOTE on Math: implemented after the Renderer, even though the module list is
# ordered the other way. Renderer originally hard-coded clip-space positions in
# shader source; an MVP-driven draw needs the math types to agree, and the Math
# module ships first while the HUD harness for it is still being designed in
# the Sandbox.
#
# NOTE on Scene: it comes after Renderer deliberately. The scene graph needs to
# allocate from GPU-visible heaps, so letting Scene dictate its own memory model
# and then having Renderer satisfy it is the wrong direction of dependency. The
# renderer owns memory; the scene describes what to put in it.
# ---------------------------------------------------------------------------