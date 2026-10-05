// ===========================================================================
//  Transform.h
// ---------------------------------------------------------------------------
//  An object's local pose in world space: a position, an orientation, and a
//  size. Euler angles (radians, pitch/yaw/roll) are stored rather than a
//  quaternion for the same reason a concept car does not ship a mass matrix
//  per panel: the number is what you edit, query and expose in a UI, and the
//  math is derived on demand. When the renderer needs an orientation query or
//  a slerp, the same pose can be converted to a quaternion without changing
//  any call site.
// ===========================================================================
#pragma once

#include <Math/Mat4.h>
#include <Math/Vec3.h>

namespace Nova::Math
{
struct Transform final
{
    /// World-space position of the object's origin.
    Vec3 position{ 0.0F, 0.0F, 0.0F };

    /// Euler angles in RADIANS: x = pitch about local X, y = yaw about local Y,
    /// z = roll about local Z. GetModelMatrix applies them
    /// "pitch first, then yaw, then roll" on the column-vector side - so the
    /// vertex is rolled about its own forward axis last, which reads naturally
    /// left-to-right on paper but is invested into the matrix right-to-left.
    /// See GetModelMatrix for why that order is a choice, not a discovery.
    Vec3 rotation{ 0.0F, 0.0F, 0.0F };

    /// Per-axis size factor. m == 1 on every component is the unit cube.
    Vec3 scale{ 1.0F, 1.0F, 1.0F };

    /// Model matrix: local -> world. Build is T * Rz * Ry * Rx * S, which says
    /// to a vertex: keep your local coordinates, scale them, rotate them about
    /// X/Y/Z, and then add the world position. Column-major, so constant
    /// buffers consume it with no transposition.
    Mat4 GetModelMatrix() const
    {
        const Mat4 translation = Mat4::Translate(position);
        const Mat4 rotationX   = Mat4::Rotate(rotation.x, Vec3::Right);
        const Mat4 rotationY   = Mat4::Rotate(rotation.y, Vec3::Up);
        const Mat4 rotationZ   = Mat4::Rotate(rotation.z, Vec3::Forward);
        const Mat4 scaleM      = Mat4::Scale(scale);
        return translation * (rotationZ * rotationY * rotationX) * scaleM;
    }

    /// Convenience accessor that composes the pose into a direction matrix:
    /// the same rotation the model matrix uses, without its translation/scale.
    Mat4 GetRotationMatrix() const
    {
        const Mat4 rotationX = Mat4::Rotate(rotation.x, Vec3::Right);
        const Mat4 rotationY = Mat4::Rotate(rotation.y, Vec3::Up);
        const Mat4 rotationZ = Mat4::Rotate(rotation.z, Vec3::Forward);
        return rotationZ * rotationY * rotationX;
    }

    /// Translates in WORLD space: adding to Position, not rotating the offset.
    void Translate(const Vec3& delta) { position += delta; }

    /// Adds to the current Euler angles, preserving order semantics that
    /// composing rotations via two axes would make an order-dependent-but-hard
    /// call.
    void Rotate(const Vec3& angles) { rotation += angles; }

    /// Component-wise size multiplier. Use (1,1,1) to mean "unchanged":
    /// scale(0) collapses the object and makes GetModelMatrix non-invertible.
    void Scale(const Vec3& factors)
    {
        scale.x *= factors.x;
        scale.y *= factors.y;
        scale.z *= factors.z;
    }

    /// The object's local +X axis in world space.
    Vec3 Right() const
    {
        const Vec4 result = GetRotationMatrix() * Vec4{ 1.0F, 0.0F, 0.0F, 0.0F };
        return Vec3{ result.x, result.y, result.z };
    }

    /// The object's local +Y axis in world space.
    Vec3 Up() const
    {
        const Vec4 result = GetRotationMatrix() * Vec4{ 0.0F, 1.0F, 0.0F, 0.0F };
        return Vec3{ result.x, result.y, result.z };
    }

    /// The object's local +Z axis in world space. D3D convention: +Z is
    /// forward, not -Z as in some right-handed renderers.
    Vec3 Forward() const
    {
        const Vec4 result = GetRotationMatrix() * Vec4{ 0.0F, 0.0F, 1.0F, 0.0F };
        return Vec3{ result.x, result.y, result.z };
    }
};

} // namespace Nova::Math
