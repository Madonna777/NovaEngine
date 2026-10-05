// ===========================================================================
//  Mat4.h
// ---------------------------------------------------------------------------
//  4x4 float matrix, COLUMN-MAJOR, for D3D constant buffers and M*v vector
//  math.
//
//  MEMORY LAYOUT, AND WHY IT IS THIS WAY
//  -------------------------------------
//  Element (row, col) lives at m[col * 4 + row]. A matrix times a column
//  vector consumes four consecutive groups of four floats, which matches both:
//      - HLSL's default cbuffer packing (column-major). float[16] -> float4x4
//        in a cbuffer is a straight memcpy, no swizzle, no row/column flip.
//      - CPU-side XMVECTOR handling once DirectXMath arrives, because it uses
//        the same convention and a future wrapper is then a type alias.
//  Row-major storage would give identical algebra on the CPU but a silent
//  transpose whenever the data is handed to a shader, which is precisely the
//  kind of bug that produces a black window and never a warning.
// ===========================================================================
#pragma once

#include <Math/Vec3.h>
#include <Math/Vec4.h>

#include <cmath>
#include <cstddef>

namespace Nova::Math
{
struct Mat4 final
{
    /// Raw 16 floats, column-major. Public so GPU-side buffers can memcpy it
    /// directly. User code reaches it through a method name where possible.
    float m[16];

    constexpr Mat4() : m{ 0.0F } {}

    explicit constexpr Mat4(const float values[16])
    {
        for (std::size_t i = 0; i < 16; ++i)
        {
            m[i] = values[i];
        }
    }

    /// Stride-through access by (col, row). Column index first, matching the
    /// storage order - so m(0,0) and m(3,3) hit the diagonal at m[0]...m[15].
    float& operator()(std::size_t col, std::size_t row) { return m[col * 4 + row]; }
    const float& operator()(std::size_t col, std::size_t row) const { return m[col * 4 + row]; }

    // ---- column / row accessors --------------------------------------------

    Vec4 GetColumn(std::size_t col) const
    {
        return Vec4{ m[col * 4 + 0], m[col * 4 + 1], m[col * 4 + 2], m[col * 4 + 3] };
    }

    Vec4 GetRow(std::size_t row) const
    {
        return Vec4{ m[0 * 4 + row], m[1 * 4 + row], m[2 * 4 + row], m[3 * 4 + row] };
    }

    // ---- arithmetic ---------------------------------------------------------

    /// Frobenius-style component-wise product (GLM's mul(mat, mat) is the full
    /// product; component-wise is operator*= on scalars only).
    Mat4 operator*(float scalar) const
    {
        Mat4 result;
        for (std::size_t i = 0; i < 16; ++i)
        {
            result.m[i] = m[i] * scalar;
        }
        return result;
    }

    friend Mat4 operator*(float scalar, const Mat4& mat) { return mat * scalar; }

    /// C = A * B with column-vector math: C v = A (B v). Transformations are
    /// applied right-to-left to the vector - Scale, then Rotate, then Translate
    /// if the matrix is T*R*S.
    Mat4 operator*(const Mat4& other) const
    {
        Mat4 result;
        for (std::size_t col = 0; col < 4; ++col)
        {
            for (std::size_t row = 0; row < 4; ++row)
            {
                float sum = 0.0F;
                for (std::size_t k = 0; k < 4; ++k)
                {
                    sum += m[k * 4 + row] * other.m[col * 4 + k];
                }
                result.m[col * 4 + row] = sum;
            }
        }
        return result;
    }

    /// M * v, the main use of the layout: one Vec4 comes out.
    Vec4 operator*(const Vec4& v) const
    {
        return Vec4{
            m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12] * v.w,
            m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13] * v.w,
            m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14] * v.w,
            m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15] * v.w,
        };
    }

    Mat4& operator*=(const Mat4& other) { *this = *this * other; return *this; }

    bool operator==(const Mat4& other) const
    {
        for (std::size_t i = 0; i < 16; ++i)
        {
            if (m[i] != other.m[i])
            {
                return false;
            }
        }
        return true;
    }
    bool operator!=(const Mat4& other) const { return !(*this == other); }

    // ---- inverse and transpose ---------------------------------------------

    /// Full general 4x4 inverse via the classical cofactor adjugate. A singular
    /// matrix returns the identity rather than NaN: NaN propagates through
    /// every subsequent matrix multiply, and the resulting garbage geometry has
    /// stock-'last camera blow-up' signatures. A singular matrix usually means
    /// a zero scale or a degenerate look-at; the identity is a visible,
    /// harmless stand-in until the user fixes their inputs.
    Mat4 Inverse() const
    {
        const float* src = m;
        float inv[16];

        inv[0] = src[5] * src[10] * src[15] - src[5] * src[11] * src[14] - src[9] * src[6] * src[15] + src[9] * src[7] * src[14] + src[13] * src[6] * src[11] - src[13] * src[7] * src[10];
        inv[4] = -src[4] * src[10] * src[15] + src[4] * src[11] * src[14] + src[8] * src[6] * src[15] - src[8] * src[7] * src[14] - src[12] * src[6] * src[11] + src[12] * src[7] * src[10];
        inv[8] = src[4] * src[9] * src[15] - src[4] * src[11] * src[13] - src[8] * src[5] * src[15] + src[8] * src[7] * src[13] + src[12] * src[5] * src[11] - src[12] * src[7] * src[9];
        inv[12] = -src[4] * src[9] * src[14] + src[4] * src[10] * src[13] + src[8] * src[5] * src[14] - src[8] * src[6] * src[13] - src[12] * src[5] * src[10] + src[12] * src[6] * src[9];

        inv[1] = -src[1] * src[10] * src[15] + src[1] * src[11] * src[14] + src[9] * src[2] * src[15] - src[9] * src[3] * src[14] - src[13] * src[2] * src[11] + src[13] * src[3] * src[10];
        inv[5] = src[0] * src[10] * src[15] - src[0] * src[11] * src[14] - src[8] * src[2] * src[15] + src[8] * src[3] * src[14] + src[12] * src[2] * src[11] - src[12] * src[3] * src[10];
        inv[9] = -src[0] * src[9] * src[15] + src[0] * src[11] * src[13] + src[8] * src[1] * src[15] - src[8] * src[3] * src[13] - src[12] * src[1] * src[11] + src[12] * src[3] * src[9];
        inv[13] = src[0] * src[9] * src[14] - src[0] * src[10] * src[13] - src[8] * src[1] * src[14] + src[8] * src[2] * src[13] + src[12] * src[1] * src[10] - src[12] * src[2] * src[9];

        inv[2] = src[1] * src[6] * src[15] - src[1] * src[7] * src[14] - src[5] * src[2] * src[15] + src[5] * src[3] * src[14] + src[13] * src[2] * src[7] - src[13] * src[3] * src[6];
        inv[6] = -src[0] * src[6] * src[15] + src[0] * src[7] * src[14] + src[4] * src[2] * src[15] - src[4] * src[3] * src[14] - src[12] * src[2] * src[7] + src[12] * src[3] * src[6];
        inv[10] = src[0] * src[5] * src[15] - src[0] * src[7] * src[13] - src[4] * src[1] * src[15] + src[4] * src[3] * src[13] + src[12] * src[1] * src[7] - src[12] * src[3] * src[5];
        inv[14] = -src[0] * src[5] * src[14] + src[0] * src[6] * src[13] + src[4] * src[1] * src[14] - src[4] * src[2] * src[13] - src[12] * src[1] * src[6] + src[12] * src[2] * src[5];

        inv[3] = -src[1] * src[6] * src[11] + src[1] * src[7] * src[10] + src[5] * src[2] * src[11] - src[5] * src[3] * src[10] - src[9] * src[2] * src[7] + src[9] * src[3] * src[6];
        inv[7] = src[0] * src[6] * src[11] - src[0] * src[7] * src[10] - src[4] * src[2] * src[11] + src[4] * src[3] * src[10] + src[8] * src[2] * src[7] - src[8] * src[3] * src[6];
        inv[11] = -src[0] * src[5] * src[11] + src[0] * src[7] * src[9] + src[4] * src[1] * src[11] - src[4] * src[3] * src[9] - src[8] * src[1] * src[7] + src[8] * src[3] * src[5];
        inv[15] = src[0] * src[5] * src[10] - src[0] * src[6] * src[9] - src[4] * src[1] * src[10] + src[4] * src[2] * src[9] + src[8] * src[1] * src[6] - src[8] * src[2] * src[5];

        float det = src[0] * inv[0] + src[1] * inv[4] + src[2] * inv[8] + src[3] * inv[12];
        if (det == 0.0F)
        {
            return Mat4::Identity();
        }

        det = 1.0F / det;
        for (std::size_t i = 0; i < 16; ++i)
        {
            inv[i] *= det;
        }

        Mat4 result;
        for (std::size_t i = 0; i < 16; ++i)
        {
            result.m[i] = inv[i];
        }
        return result;
    }

    /// Swaps rows and columns: the element at (row, col) moves to (col, row).
    Mat4 Transpose() const
    {
        Mat4 result;
        for (std::size_t col = 0; col < 4; ++col)
        {
            for (std::size_t row = 0; row < 4; ++row)
            {
                result.m[row * 4 + col] = m[col * 4 + row];
            }
        }
        return result;
    }

    // ---- factories -----------------------------------------------------------

    static Mat4 Identity()
    {
        Mat4 result{};
        result.m[0]  = 1.0F;
        result.m[5]  = 1.0F;
        result.m[10] = 1.0F;
        result.m[15] = 1.0F;
        return result;
    }

    /// Stores the offset in column 3 (the translation components at rows 0..2).
    static Mat4 Translate(const Vec3& offset)
    {
        Mat4 result = Mat4::Identity();
        result.m[12] = offset.x;
        result.m[13] = offset.y;
        result.m[14] = offset.z;
        return result;
    }

    /// Uniform scale .
    static Mat4 Scale(const Vec3& factors)
    {
        Mat4 result{};
        result.m[0]  = factors.x;
        result.m[5]  = factors.y;
        result.m[10] = factors.z;
        result.m[15] = 1.0F;
        return result;
    }

    /// Axis-angle rotation. @axis is normalised internally by this function:
    /// Rodrigues' formula, v' = v cos(theta) + (k x v) sin(theta) + k (k . v)(1 - cos(theta)).
    /// Positive angle rotates counter-clockwise when viewed along the axis TOWARDS the origin.
    static Mat4 Rotate(float angleRadians, const Vec3& axis)
    {
        const Vec3 k = axis.Normalized();
        const float  c = std::cos(angleRadians);
        const float  s = std::sin(angleRadians);
        const float  t = 1.0F - c;

        Mat4 result = Mat4::Identity();
        result.m[0]  = t * k.x * k.x + c;
        result.m[4]  = t * k.x * k.y - s * k.z;
        result.m[8]  = t * k.x * k.z + s * k.y;

        result.m[1]  = t * k.x * k.y + s * k.z;
        result.m[5]  = t * k.y * k.y + c;
        result.m[9]  = t * k.y * k.z - s * k.x;

        result.m[2]  = t * k.z * k.x - s * k.y;
        result.m[6]  = t * k.z * k.y + s * k.x;
        result.m[10] = t * k.z * k.z + c;
        return result;
    }

    /// Left-handed D3D projection: NDC z in [0, 1].
    static Mat4 Perspective(float fovYRadians, float aspect, float nearZ, float farZ)
    {
        const float f = 1.0F / std::tan(fovYRadians * 0.5F);
        const float range = farZ - nearZ;

        Mat4 result{};
        result.m[0]  = f / aspect;
        result.m[5]  = f;
        result.m[10] = farZ / range;
        result.m[11] = 1.0F;
        result.m[14] = -(nearZ * farZ) / range;
        return result;
    }

    /// Orthographic D3D projection with a far-near range of [0, 1] on z.
    static Mat4 Orthographic(float left, float right, float bottom, float top, float nearZ, float farZ)
    {
        Mat4 result = Mat4::Identity();
        result.m[0]  = 2.0F / (right - left);
        result.m[5]  = 2.0F / (top - bottom);
        result.m[10] = 1.0F / (farZ - nearZ);
        result.m[12] = -(right + left) / (right - left);
        result.m[13] = -(top + bottom) / (top - bottom);
        result.m[14] = -nearZ / (farZ - nearZ);
        return result;
    }

    /// View matrix from eye/center/up. The camera looks down its LOCAL +Z with
    /// row axis to the right and up being +Y before rotation - the convention
    /// Nova's D3D renderer uses. Result maps world space into camera-local
    /// space: an object sitting at `center` ends up at (0, 0, distance) in view.
    static Mat4 LookAt(const Vec3& eye, const Vec3& center, const Vec3& up)
    {
        const Vec3 zaxis = (center - eye).Normalized();
        const Vec3 xaxis = up.Cross(zaxis).Normalized();
        const Vec3 yaxis = zaxis.Cross(xaxis);

        Mat4 result = Mat4::Identity();
        result.m[0]  = xaxis.x;
        result.m[1]  = xaxis.y;
        result.m[2]  = xaxis.z;
        result.m[4]  = yaxis.x;
        result.m[5]  = yaxis.y;
        result.m[6]  = yaxis.z;
        result.m[8]  = zaxis.x;
        result.m[9]  = zaxis.y;
        result.m[10] = zaxis.z;

        result.m[12] = -(xaxis.Dot(eye));
        result.m[13] = -(yaxis.Dot(eye));
        result.m[14] = -(zaxis.Dot(eye));
        return result;
    }
};

} // namespace Nova::Math
