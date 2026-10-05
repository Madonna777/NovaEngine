// ===========================================================================
//  Camera.cpp
// ===========================================================================

#include <Scene/Camera.h>

namespace Nova::Scene
{
Math::Mat4 Camera::GetProjectionMatrix() const
{
    return Math::Mat4::Perspective(fov, aspectRatio, nearPlane, farPlane);
}

Math::Mat4 Camera::GetViewMatrix() const
{
    // LookAt needs a real up that is never parallel to the forward vector.
    // Pitch is controller-clamped to +-89 degrees, so +Y is always a legal
    // choice here; the +-90 case is irrelevant because the clamp is wholesale.
    return Math::Mat4::LookAt(position, position + GetForwardVector(), Math::Vec3::Up);
}

Math::Mat4 Camera::GetViewProjectionMatrix() const
{
    return GetProjectionMatrix() * GetViewMatrix();
}

Math::Vec3 Camera::GetForwardVector() const
{
    // Column-vector form of the yaw/pitch rotation matrix times (0,0,1).
    // Sign is chosen so yaw = +pi/2 turns the forward axis to +X, which is the
    // same convention FPSCameraController uses when integrating mouse deltas.
    //
    // NOTE the cos(pitch) scaling on x and z: without it the horizontal
    // components would not shrink as pitch climbs to +-90, producing a
    // length-sqrt(2) forward that callers had to silently re-normalize.
    const float c = std::cos(pitch);
    Math::Vec3 forward{ c * std::sin(yaw), std::sin(pitch), c * std::cos(yaw) };
    forward.Normalize();
    return forward;
}

Math::Vec3 Camera::GetRightVector() const
{
    // right = up_world x forward, using the left-handed look-at convention:
    //    for forward (0,0,1) with +Y up, cross((0,1,0),(0,0,1)) == (1,0,0).
    // Re-normalizing guards against a pitch that has almost made the two axes
    // parallel, even though the clamp in the controller keeps that from
    // actually happening.
    Math::Vec3 right = Math::Vec3::Up.Cross(GetForwardVector());
    if (right.LengthSquared() <= 0.0F)
    {
        return Math::Vec3::Right;
    }
    right.Normalize();
    return right;
}

Math::Vec3 Camera::GetUpVector() const
{
    // orthogonal, so it is not the world-up vector: lean the view and the math
    // derives a correctly tilted axes for the shader. Using world-up here too
    // would make the projection's vertical collapse when looking horizontally.
    Math::Vec3 up = GetForwardVector().Cross(GetRightVector());
    if (up.LengthSquared() <= 0.0F)
    {
        return Math::Vec3::Up;
    }
    up.Normalize();
    return up;
}

void Camera::UpdateAspectRatio(float width, float height)
{
    // Division by zero guard: a zero-height window is a real transient (during
    // a maximize animation on Win32), and inf/NaN would poison every projection
    // matrix built on this field until something rewrote it. Falling back to a
    // square aspect is visibly wrong rather than a silent cascade.
    if (height > 0.0F)
    {
        aspectRatio = width / height;
    }
}

} // namespace Nova::Scene
