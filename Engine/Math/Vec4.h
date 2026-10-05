// ===========================================================================
//  Vec4.h
// ---------------------------------------------------------------------------
//  4-component float vector: homogeneous points, colours, rows and columns of
//  Mat4. The base case for everything Vec3-with-w.
// ===========================================================================
#pragma once

#include <cmath>
#include <cstddef>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4201)
#endif

namespace Nova::Math
{
struct Vec4 final
{
    union
    {
        struct { float x, y, z, w; };
        struct { float r, g, b, a; };
        float data[4];
    };

    constexpr Vec4() = default;
    constexpr explicit Vec4(float scalar) : data{ scalar, scalar, scalar, scalar } {}
    constexpr Vec4(float x, float y, float z, float w) : data{ x, y, z, w } {}

    float& operator[](std::size_t i) { return data[i]; }
    const float& operator[](std::size_t i) const { return data[i]; }

    Vec4 operator+(const Vec4& other) const
    {
        return Vec4{ x + other.x, y + other.y, z + other.z, w + other.w };
    }
    Vec4 operator-(const Vec4& other) const
    {
        return Vec4{ x - other.x, y - other.y, z - other.z, w - other.w };
    }
    Vec4 operator-() const { return Vec4{ -x, -y, -z, -w }; }
    Vec4 operator*(const Vec4& other) const
    {
        return Vec4{ x * other.x, y * other.y, z * other.z, w * other.w };
    }
    Vec4 operator/(const Vec4& other) const
    {
        return Vec4{ x / other.x, y / other.y, z / other.z, w / other.w };
    }
    Vec4 operator*(float scalar) const
    {
        return Vec4{ x * scalar, y * scalar, z * scalar, w * scalar };
    }
    Vec4 operator/(float scalar) const
    {
        return Vec4{ x / scalar, y / scalar, z / scalar, w / scalar };
    }

    Vec4& operator+=(const Vec4& other)
    {
        x += other.x; y += other.y; z += other.z; w += other.w; return *this;
    }
    Vec4& operator-=(const Vec4& other)
    {
        x -= other.x; y -= other.y; z -= other.z; w -= other.w; return *this;
    }
    Vec4& operator*=(float scalar)
    {
        x *= scalar; y *= scalar; z *= scalar; w *= scalar; return *this;
    }
    Vec4& operator/=(float scalar)
    {
        x /= scalar; y /= scalar; z /= scalar; w /= scalar; return *this;
    }

    friend Vec4 operator*(float scalar, const Vec4& v) { return v * scalar; }

    bool operator==(const Vec4& other) const
    {
        return x == other.x && y == other.y && z == other.z && w == other.w;
    }
    bool operator!=(const Vec4& other) const { return !(*this == other); }

    float Dot(const Vec4& other) const
    {
        return x * other.x + y * other.y + z * other.z + w * other.w;
    }

    float Length() const { return std::sqrt(LengthSquared()); }

    float LengthSquared() const { return x * x + y * y + z * z + w * w; }

    void Normalize()
    {
        const float len = Length();
        if (len > 0.0F)
        {
            (*this) /= len;
        }
    }

    Vec4 Normalized() const
    {
        const float len = Length();
        return len > 0.0F ? (*this / len) : Vec4{ 0.0F, 0.0F, 0.0F, 0.0F };
    }

    Vec4 Lerp(const Vec4& other, float t) const
    {
        return Vec4{ x + (other.x - x) * t, y + (other.y - y) * t,
                     z + (other.z - z) * t, w + (other.w - w) * t };
    }

    float Distance(const Vec4& other) const { return (*this - other).Length(); }
    float DistanceSquared(const Vec4& other) const { return (*this - other).LengthSquared(); }

    static const Vec4 Zero;
    static const Vec4 One;
};

inline const Vec4 Vec4::Zero{ 0.0F, 0.0F, 0.0F, 0.0F };
inline const Vec4 Vec4::One{ 1.0F, 1.0F, 1.0F, 1.0F };

} // namespace Nova::Math

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
