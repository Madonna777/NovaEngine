// ===========================================================================
//  FPSCameraController.h
// ---------------------------------------------------------------------------
//  A driver-layer wrapper around a Camera: it watches the same Window for user
//  input and rewrites the camera's position and yaw/pitch when the user asks it
//  to. Deliberately NOT a script-like controller:
//      - Input is not abstracted: the math of event delivery spends one enum
//        check and no window ref bookkeeping.
//      - Cursor capture is a two-call caller-facing switch (SetActive), not a
//        hidden service call at first mouse movement.
// ===========================================================================
#pragma once

#include <Core/Window.h>
#include <Math/Math.h>
#include <Scene/Camera.h>

namespace Nova::Scene
{
/// Uploads mouselook and WASD motion to a Camera on Update-per-frame.
///
/// @note Couples the camera to exactly one input device mapping and nothing
///       else: no rotation about roll, no FF mode, no aircraft. An aircraft
///       controller would want a quaternion and a different speed model, not
///       every branch of this struct growing an "aerial" arm.
class FPSCameraController final
{
public:
    FPSCameraController(Camera& camera, Window& window);

    FPSCameraController(const FPSCameraController&)            = delete;
    FPSCameraController& operator=(const FPSCameraController&) = delete;
    FPSCameraController(FPSCameraController&&)                 = delete;
    FPSCameraController& operator=(FPSCameraController&&)      = delete;
    ~FPSCameraController();

    /// Captures or releases the window cursor. Call this when entering or
    /// leaving gameplay. @return True if raw mouse motion was requested.
    bool SetActive(bool active);

    [[nodiscard]] bool IsActive() const noexcept { return active_; }

    /// Integrates camera motion for a frame. Assumes the event pump has
    /// already run this frame, so mouse delta accumulation from MouseMoved
    /// events is current.
    void Update(float deltaTime);

    /// Tunables. Public on purpose: every customisable input feel will be set
    /// through these pointers rather than through a wrapped properties struct.
    float moveSpeed        = 4.5F;  ///< meters/second of translation
    float sprintMultiplier = 2.0F;  ///< shift-allayed transform speed
    float mouseSensitivity = 0.0025F; ///< radians per mouse delta pixel

private:
    void OnMouseMoved(const Event& event);

    Camera&     camera_;
    Window&     window_;
    bool        active_ = false;
    float       pendingDeltaX_ = 0.0F;
    float       pendingDeltaY_ = 0.0F;

    /// Subscription id for the mouse-move handler, used to unsubscribe in the
    /// destructor so the handler list never holds a dead `this`.
    EventDispatcher::Token subscription_ = 0;
};

} // namespace Nova::Scene
