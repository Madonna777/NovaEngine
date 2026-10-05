// ===========================================================================
//  FPSCameraController.cpp
// ===========================================================================

#include <Scene/FPSCameraController.h>

#include <Core/Input.h>
#include <Core/KeyCodes.h>
#include <Core/Log.h>

#include <GLFW/glfw3.h>
#include <cmath>

namespace Nova::Scene
{
namespace
{
/// Pitch must stay away from +-pi/2: at straight up the forward and world-up
/// axes are parallel, LookAt's cross products degenerate, and a frame of
/// NaNs is the documented symptom of a port missing this clamp.
/// +-89 degrees in radians.
const float kPitchLimit = Math::Radians(89.0F);
} // namespace

FPSCameraController::FPSCameraController(Camera& camera, Window& window)
    : camera_(camera), window_(window)
{
    // The consumer that lives for the lifetime of this object is the
    // window-level EventDispatcher. It exists as part of the Window, and
    // the Subscription token ensures destructor-time unsubscribe.
    subscription_ = window_.GetEventDispatcher().Subscribe(EventType::MouseMoved,
        [this](const Event& event) { OnMouseMoved(event); });
}

FPSCameraController::~FPSCameraController()
{
    // The controller object is the `this` captured above. Window outlives us by
    // construction in Sandbox, but not always: if the Window dies we are inside
    // a torn-down dispatcher that is already clearing handlers, so a guard on
    // the token value covers both lifetime orders.
    if (subscription_ != 0)
    {
        window_.GetEventDispatcher().Unsubscribe(EventType::MouseMoved, subscription_);
        subscription_ = 0;
    }
}

bool FPSCameraController::SetActive(bool active)
{
    // Plain declaration, NOT `if (GLFWwindow* h = ...; cond)`: the init-statement
    // variable of an if-with-initializer goes out of scope at the end of the
    // whole if/else chain, and the cursor-mode calls below are after it.
    GLFWwindow* handle = window_.GetHandle();
    if (handle == nullptr)
    {
        return false;
    }

    // Idempotent: toggling to the state already held must not re-issue a GLFW
    // call, and a caller that sets the same value every frame should not pay
    // for it.
    if (active == active_)
    {
        return active;
    }

    if (active)
    {
        glfwSetInputMode(handle, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

        // Raw motion lets us not have to compensate for sensor acceleration or
        // Settings->mouse enhancement: an FPS controller is the one place in a
        // game engine where honest deltas are the whole job description.
        if (glfwRawMouseMotionSupported())
        {
            glfwSetInputMode(handle, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        }
        active_ = true;
    }
    else
    {
        glfwSetInputMode(handle, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        active_ = false;
    }

    // After any cursor mode switch the first Moved event would otherwise
    // carry the delta between the old visible position and the new (virtual)
    // one, producing a single-frame rotation lurch. Left zeroing is the
    // honest contract: one fantasy frame with zero delta before the real ones.
    pendingDeltaX_ = 0.0F;
    pendingDeltaY_ = 0.0F;

    NOVA_INFO("FPS camera controller {}: cursor {} (raw motion {}available)",
              active ? "activated" : "deactivated",
              active ? "captured" : "released",
              glfwRawMouseMotionSupported() ? "" : "NOT ");

    return true;
}

void FPSCameraController::Update(float deltaTime)
{
    if (!active_)
    {
        return;
    }

    // ---- mouse look ------------------------------------------------------
    //
    // Yaw/pitch integration is subject to sign choices. With our column-vector
    // convention, dragging the mouse right should make the camera face +X from
    // +Z (yaw = +pi/2 rotates forward to +X). The forward formula therefore
    // computes x = sin(pitch) is up. A negative dy means the cursor moved up,
    // so the sign inversion below brings the camera direction up as well.
    camera_.yaw += pendingDeltaX_ * mouseSensitivity;
    camera_.pitch -= pendingDeltaY_ * mouseSensitivity;
    pendingDeltaX_ = 0.0F;
    pendingDeltaY_ = 0.0F;

    // +89 degrees, not +90: see the file-scope constant for why.
    if (camera_.pitch > kPitchLimit)
    {
        camera_.pitch = kPitchLimit;
    }
    else if (camera_.pitch < -kPitchLimit)
    {
        camera_.pitch = -kPitchLimit;
    }

    // ---- polled WASD snap -------------------------------------------------
    //
    // Held-state read via polling instead of events: there is no KeyDown event
    // in EventType because polling handles held state cheaply, while events
    // carry edges. The subtle part is projecting forward onto the horizontal
    // plane so a camera looking at the sky still moves horizontally rather
    // than drifting up out of the level.
    const Math::Vec3 forward = Math::Vec3{  camera_.GetForwardVector().x, 0.0F,
                                camera_.GetForwardVector().z }.Normalized();
    const Math::Vec3 right   = Math::Vec3{  camera_.GetRightVector().x, 0.0F,
                                camera_.GetRightVector().z }.Normalized();

    Math::Vec3 displacement = Math::Vec3{ 0.0F, 0.0F, 0.0F };
    if (Input::IsKeyDown(Key::Code::W))
    {
        displacement += forward;
    }
    if (Input::IsKeyDown(Key::Code::S))
    {
        displacement -= forward;
    }
    if (Input::IsKeyDown(Key::Code::D))
    {
        displacement += right;
    }
    if (Input::IsKeyDown(Key::Code::A))
    {
        displacement -= right;
    }

    const bool sprinting = Input::IsAnyKeyDown({ Key::Code::LeftShift, Key::Code::RightShift });
    const float speed = moveSpeed * (sprinting ? sprintMultiplier : 1.0F);

    // deltaTime is not multiplied into the rotations above because sensitivity
    // is expressed in radians per pixel: tying it to time would make the same
    // physical motion slower or faster with framerate. Motion, on the other
    // hand, moves a certain distance per second, so time is what it is in
    // units.
    if (displacement.LengthSquared() > 0.0F)
    {
        displacement.Normalize();
        camera_.position += displacement * speed * deltaTime;
    }

    if (Input::IsKeyDown(Key::Code::Space))
    {
        camera_.position.y += speed * deltaTime;
    }
    if (Input::IsKeyDown(Key::Code::C))
    {
        camera_.position.y -= speed * deltaTime;
    }
}

void FPSCameraController::OnMouseMoved(const Event& event)
{
    // MouseMoveEvent carries raw deltas already accumulated by the dispatcher
    // (see Window::OnGlfwCursorPosition), so this handler does not need to
    // see cursor coordinates or store a last position: deltas in, deltas out.
    const MouseMoveEvent& move = event.As<MouseMoveEvent>();
    pendingDeltaX_ += move.deltaX;
    pendingDeltaY_ += move.deltaY;
}

} // namespace Nova::Scene
