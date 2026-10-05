// ===========================================================================
//  Camera.h
// ---------------------------------------------------------------------------
//  Pinhole camera over the Nova::Math column-major matrices. Perspective is the
//  normal case; the view maps world to camera-local space (LH, +Z forward) and
//  the projection maps camera-local to clip space with a [0,1] NDC depth
//  range. All fields are public because a camera is a value type in disguise:
//  there is deliberately no encapsulation tax for reading "where is it" from
//  a lightweight 3D scene tool.
// ===========================================================================
#pragma once

#include <Math/Math.h>

namespace Nova::Scene
{
/// A world-space camera with an euler-driven orientation.
///
/// @note Yaw and pitch are stored, but twist (roll) is not: a first-person
///       camera cannot roll without the horizon visibly tilting, which every
///       player experiences as a bug rather than a feature. Those looking for
///       rolls in cutscenes get a separate cubic spline driver later.
struct Camera
{
    /// World-space position of the lens.
    Math::Vec3 position{ 0.0F, 0.0F, 0.0F };

    /// Yaw in radians, counter-clockwise about +Y when viewed from above.
    /// 0 faces +Z; pi/2 faces +X, matching the Sin/Cos in GetForwardVector.
    float yaw   = 0.0F;

    /// Pitch in radians. Positive looks UP toward +Y. Clamped to (+-89 deg) by
    /// the controller; this struct does not clamp in the setter, because a game
    /// that needs a free-fly mode simply does not attach the controller.
    float pitch = 0.0F;

    /// Vertical field of view in radians. Default 60 degrees because that is
    /// what humans are used to from cinema, and it is inside the range where a
    /// rectilinear lens does not gape.
    float fov     = Math::Radians(60.0F);

    /// Clip distances. Near must stay below far by enough that the z-range
    /// stays well conditioned: Zf/(Zf-Zn) gets singular as near -> far.
    float nearPlane = 0.1F;
    float farPlane  = 1000.0F;

    /// Width / height, not height / width. Updated by UpdateAspectRatio.
    float aspectRatio = 16.0F / 9.0F;

    /// Camera-to-clip, LH with NDC z in [0, 1].
    Math::Mat4 GetProjectionMatrix() const;

    /// World-to-camera, computed from position + orientation. LookAt is used
    /// with the camera's own forward, so the result is an inverse rotation and
    /// translation without a separate quat-to-matrix step.
    Math::Mat4 GetViewMatrix() const;

    /// The combination every shader now needs. Separate accessors exist for the
    /// case where the view and projection matrices travel to different
    /// constant-buffer stages (forward rendering vs. shadow pass).
    Math::Mat4 GetViewProjectionMatrix() const;

    /// Unit vector the camera looks along (local +Z rotated into world space).
    Math::Vec3 GetForwardVector() const;

    /// Unit vector pointing screen-right (local +X rotated into world space).
    Math::Vec3 GetRightVector() const;

    /// Orthogonalized camera-up. Computed, not stored, because storing a third
    /// axis alongside yaw/pitch invites re-sync bugs between two sources of
    /// the same orientation.
    Math::Vec3 GetUpVector() const;

    /// Keeps the projection matrix's aspect matching the window it draws into.
    /// Expected to be called on window resize; safe any other time.
    void UpdateAspectRatio(float width, float height);
};

} // namespace Nova::Scene
