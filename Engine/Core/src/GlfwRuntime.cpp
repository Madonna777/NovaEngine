// ===========================================================================
//  GlfwRuntime.cpp
// ---------------------------------------------------------------------------
//  The only translation unit that may call glfwInit / glfwTerminate.
// ===========================================================================

#include <Core/GlfwRuntime.h>

#include <Core/Log.h>

#include <atomic>

#include <GLFW/glfw3.h>

namespace Nova::Detail
{
namespace
{
/// Live references to the GLFW library. Atomic because two windows may be
/// created on different threads during editor bring-up, and because a
/// destructor running on a worker thread is a realistic future.
std::atomic<int> g_referenceCount{0};

/// WHY THE ERROR CALLBACK IS A FREE FUNCTION AND NOT A LAMBDA OR std::function
/// ----------------------------------------------------------------------------
/// GLFW stores the pointer in a plain global C variable of type
/// void(*)(int, const char*) and calls it from GLFW's own code, including from
/// inside a DLL that may outlive ours. A std::function would have to live
/// somewhere with static storage duration and would be destroyed before GLFW
/// stopped calling it - so GLFW would call into freed memory. A plain function
/// has no such lifetime. This is the documented reason the GLFW API is shaped
/// the way it is.
void OnGlfwError(int code, const char* description)
{
    // Logged at Error, not Critical: a GLFW error does not always mean the
    // engine is finished, and callers such as AcquireGlfw decide that. Making
    // the callback itself decide would duplicate the policy.
    NOVA_ERROR("GLFW error {}: {}", code, description != nullptr ? description : "(null)");
}

/// The callback GLFW currently holds, or null. Saved so it can be restored on
/// teardown instead of being left dangling - see the note in OnGlfwError.
void (*g_previousErrorCallback)(int, const char*) = nullptr;
} // namespace

bool AcquireGlfw()
{
    // fetch_add returns the value BEFORE the increment, so this test is exactly
    // "am I the one who just took it from zero to one?". Using the returned
    // value rather than the counter is what makes this correct under
    // concurrency - reading the counter separately would race.
    if (g_referenceCount.fetch_add(1, std::memory_order_acq_rel) == 0)
    {
        // Saved and installed BEFORE glfwInit: glfwInit itself can report an
        // error (no display, no GPU driver), and if the callback is only set
        // afterwards that failure is reported to a null pointer and lost.
        g_previousErrorCallback = glfwSetErrorCallback(&OnGlfwError);

        if (glfwInit() != GLFW_TRUE)
        {
            // Roll the count back: we took a reference but do not hold the
            // library, so leaving the count at 1 would make a later Release
            // call glfwTerminate on a library that never started - and the
            // later Acquire would think init was done.
            g_referenceCount.fetch_sub(1, std::memory_order_acq_rel);

            glfwSetErrorCallback(g_previousErrorCallback);
            g_previousErrorCallback = nullptr;

            NOVA_ERROR(
                "glfwInit failed. On Windows this usually means no desktop "
                "session is attached (a service or over SSH), or the process "
                "has no window station.");
            return false;
        }

        NOVA_INFO("GLFW {} initialised", glfwGetVersionString());
    }

    return true;
}

void ReleaseGlfw()
{
    // fetch_sub returns the value BEFORE the decrement, so 1 means "I am the
    // last reference" - the same argument as above.
    if (g_referenceCount.fetch_sub(1, std::memory_order_acq_rel) != 1)
    {
        // Someone over-released, or over-released and the count has wrapped.
        // glfwTerminate must NOT run here: it would tear down a library other
        // windows are still using. Clamping at zero keeps the invariant
        // recoverable and the next Acquire works correctly.
        if (g_referenceCount.load(std::memory_order_relaxed) < 0)
        {
            g_referenceCount.store(0, std::memory_order_relaxed);
            NOVA_ERROR("GlfwRuntime reference count underflow - clamped to 0. "
                       "A Window was destroyed more than once.");
        }
        return;
    }

    // Last reference: tear the library down, then restore the callback so GLFW
    // is not left holding a pointer into code that may be unloaded next.
    glfwTerminate();
    glfwSetErrorCallback(g_previousErrorCallback);
    g_previousErrorCallback = nullptr;

    NOVA_INFO("GLFW terminated");
}

} // namespace Nova::Detail