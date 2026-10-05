// ===========================================================================
//  Vec2.h
// ---------------------------------------------------------------------------
//  2-component float vector: texture coordinates, screen-space positions,
//  small offsets. Storage and layout are the Vec3 shape minus one float.
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
struct Vec2 final
{
    union
    {
        struct { float x, y; };
        struct { float r, g; };
        float data[2];
    };

    constexpr Vec2() = default;
    constexpr explicit Vec2(float scalar) : data{ scalar, scalar } {}
    constexpr Vec2(float x, float y) : data{ x, y } {}

    float& operator[](std::size_t i) { return data[i]; }
    const float& operator[](std::size_t i) const { return data[i]; }

    Vec2 operator+(const Vec2& other) const { return Vec2{ x + other.x, y + other.y }; }
    Vec2 operator-(const Vec2& other) const { return Vec2{ x - other.x, y - other.y }; }
    Vec2 operator-() const { return Vec2{ -x, -y }; }
    Vec2 operator*(const Vec2& other) const { return Vec2{ x * other.x, y * other.y }; }
    Vec2 operator/(const Vec2& other) const { return Vec2{ x / other.x, y / other.y }; }
    Vec2 operator*(float scalar) const { return Vec2{ x * scalar, y * scalar }; }
    Vec2 operator/(float scalar) const { return Vec2{ x / scalar, y / scalar }; }

    Vec2& operator+=(const Vec2& other) { x += other.x; y += other.y; return *this; }
    Vec2& operator-=(const Vec2& other) { x -= other.x; y -= other.y; return *this; }
    Vec2& operator*=(float scalar) { x *= scalar; y *= scalar; return *this; }
    Vec2& operator/=(float scalar) { x /= scalar; y /= scalar; return *this; }

    friend Vec2 operator*(float scalar, const Vec2& v) { return v * scalar; }

    bool operator==(const Vec2& other) const { return x == other.x && y == other.y; }
    bool operator!=(const Vec2& other) const { return !(*this == other); }

    float Dot(const Vec2& other) const { return x * other.x + y * other.y; }

    float Length() const { return std::sqrt(LengthSquared()); }
    float LengthSquared() const { return x * x + y * y; }

    void Normalize()
    {
        const float len = Length();
        if (len > 0.0F)
        {
            (*this) /= len;
        }
    }

    Vec2 Normalized() const
    {
        const float len = Length();
        return len > 0.0F ? (*this / len) : Vec2{ 0.0F, 0.0F };
    }

    Vec2 Lerp(const Vec2& other, float t) const
    {
        return Vec2{ x + (other.x - x) * t, y + (other.y - y) * t };
    }

    float Distance(const Vec2& other) const { return (*this - other).Length(); }
    float DistanceSquared(const Vec2& other) const { return (*this - other).LengthSquared(); }

    static const Vec2 Zero;
    static const Vec2 One;
};

inline const Vec2 Vec2::Zero{ 0.0F, 0.0F };
inline const Vec2 Vec2::One{ 1.0F, 1.0F };

} // namespace Nova::Math

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
