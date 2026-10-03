// ===========================================================================
//  GlfwRuntime.h
// ---------------------------------------------------------------------------
//  Reference-counted glfwInit() / glfwTerminate().
//
//  WHY THIS IS A SEPARATE FILE
//  ---------------------------
//  Window.h documents why this cannot live in Window: glfwInit and
//  glfwTerminate are process-global, so the last Window to die owns the
//  teardown. Putting that in Window.cpp would make the ownership rule implicit
//  - visible only by reading the destructor. As its own unit with two
//  functions, it is greppable: one search for glfwTerminate in the engine
//  returns exactly this file.
//
//  THIS IS THE ONLY PLACE IN THE ENGINE THAT CALLS glfwInit,
//  glfwTerminate, OR glfwSetErrorCallback.
//
//  WHY REFERENCE COUNTING AND NOT A SINGLE "ALREADY INITIALISED" BOOL
//  -----------------------------------------------------------------
//  A bool would be enough for exactly one window, which is all we have today.
//  It breaks the moment a second exists - the editor opening a preview panel,
//  a multi-monitor tool - and it breaks silently: glfwTerminate() with a live
//  window is undefined behaviour that usually manifests as a crash inside
//  GLFW, minutes later, with a stack that points nowhere near the cause. A
//  counter costs one atomic increment and makes that state unreachable.
// ===========================================================================
#pragma once

namespace Nova::Detail
{
/// Initialises GLFW if it is not already initialised, and installs the
/// engine's error callback.
///
/// @return False if glfwInit() failed. The error callback has already logged
///         the reason; the caller decides whether it is fatal.
/// @note Idempotent and thread-safe with respect to other Acquire calls.
bool AcquireGlfw();

/// Releases one reference. The GLFW library is terminated when the count
/// reaches zero, which is the ONLY point at which glfwTerminate() is legal.
///
/// @note MUST NOT be called while any GLFWwindow is still alive. The Window
///       class holds a reference for exactly this reason, so a correctly
///       scoped Window can never violate that rule.
void ReleaseGlfw();
} // namespace Nova::Detail