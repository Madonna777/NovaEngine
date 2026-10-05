// ===========================================================================
//  Vec3.h
// ---------------------------------------------------------------------------
//  3-component float vector: positions, directions, Euler angles, colours.
//
//  DESIGN NOTES
//  ------------
//  * All storage is plain float[3]. No templates, no SIMD types, nothing that
//    cannot cross a DLL boundary later without a translation unit argument.
//  * Arithmetic follows COLUMN-VECTOR math: a transformation is M * v, and a
//    direction is a Vec4 with w=0 while a position is a Vec4 with w=1.
//  * Named component access through a union is the classic D3DX/DirectXMath /
//    GLM idiom. It gives one object `.x/.y/.z`, `.r/.g/.b`, and `.data[]`
//    views over the same three floats. sizeof(Vec3) is always 12: there is no
//    padding and the layout matches HLSL `float3`.
//
//  ABOUT ANONYMOUS STRUCTS IN THE UNION
//  -------------------------------------
//  { x,y,z } and { r,g,b } are MSVC extensions and warn C4201 at /W4. The
//  warning is suppressed per-header in the umbrella scope because the layout is
//  intentional and incompatible with "plain by-the-book C++" any other way.
// ===========================================================================
#pragma once

#include <cmath>
#include <cstddef>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4201) // nameless struct/union: deliberate idiom here
#endif

namespace Nova::Math
{
struct Vec3 final
{
    union
    {
        struct { float x, y, z; };
        struct { float r, g, b; };
        float data[3];
    };

    // ---- construction -----------------------------------------------------

    constexpr Vec3() = default;

    /// All three components set to the same value.
    constexpr explicit Vec3(float scalar) : data{ scalar, scalar, scalar } {}

    constexpr Vec3(float x, float y, float z) : data{ x, y, z } {}

    // ---- per-component access ---------------------------------------------

    /// Returns the i-th component. Index 0->x, 1->y, 2->z.
    ///
    /// NOT constexpr because reference-returning indexing in constexpr context
    /// keeps the object a constexpr-usable lvalue only when MSVC inlines its own
    /// constexpr std::array gymnastics; keep it simple.
    float& operator[](std::size_t i) { return data[i]; }
    const float& operator[](std::size_t i) const { return data[i]; }

    // ---- arithmetic -------------------------------------------------------

    Vec3 operator+(const Vec3& other) const { return Vec3{ x + other.x, y + other.y, z + other.z }; }
    Vec3 operator-(const Vec3& other) const { return Vec3{ x - other.x, y - other.y, z - other.z }; }
    Vec3 operator-() const { return Vec3{ -x, -y, -z }; }

    /// Component-wise product. For scaling a vector use operator*(float).
    Vec3 operator*(const Vec3& other) const { return Vec3{ x * other.x, y * other.y, z * other.z }; }
    Vec3 operator/(const Vec3& other) const { return Vec3{ x / other.x, y / other.y, z / other.z }; }

    Vec3 operator*(float scalar) const { return Vec3{ x * scalar, y * scalar, z * scalar }; }
    Vec3 operator/(float scalar) const { return Vec3{ x / scalar, y / scalar, z / scalar }; }

    Vec3& operator+=(const Vec3& other) { x += other.x; y += other.y; z += other.z; return *this; }
    Vec3& operator-=(const Vec3& other) { x -= other.x; y -= other.y; z -= other.z; return *this; }
    Vec3& operator*=(float scalar) { x *= scalar; y *= scalar; z *= scalar; return *this; }
    Vec3& operator/=(float scalar) { x /= scalar; y /= scalar; z /= scalar; return *this; }

    friend Vec3 operator*(float scalar, const Vec3& v) { return v * scalar; }

    bool operator==(const Vec3& other) const { return x == other.x && y == other.y && z == other.z; }
    bool operator!=(const Vec3& other) const { return !(*this == other); }

    // ---- geometric queries -------------------------------------------------

    /// Dot product: for unit vectors, the cosine of the angle between them.
    float Dot(const Vec3& other) const { return x * other.x + y * other.y + z * other.z; }

    /// Cross product: returns the vector perpendicular to both, following the
    /// right-hand rule. (1,0,0) x (0,1,0) = (0,0,1).
    Vec3 Cross(const Vec3& other) const
    {
        return Vec3{ y * other.z - z * other.y,
                     z * other.x - x * other.z,
                     x * other.y - y * other.x };
    }

    /// True Euclidean length. Prefer LengthSquared when only comparisons of
    /// magnitude are needed: a square root is one of the more expensive common
    /// FP ops and it does not change the ordering of the result.
    float Length() const { return std::sqrt(LengthSquared()); }

    float LengthSquared() const { return x * x + y * y + z * z; }

    /// In-place normalisation. Zero vectors are left at zero rather than
    /// producing NaN, because dividing by an unmeasured normal is not a
    /// recoverable operation and every NaN downstream is anonymous to whoever
    /// looks at the render.
    void Normalize()
    {
        const float len = Length();
        if (len > 0.0F)
        {
            (*this) /= len;
        }
    }

    /// Length-1 copy of this vector. Same zero guard as Normalize.
    Vec3 Normalized() const
    {
        const float len = Length();
        return len > 0.0F ? (*this / len) : Vec3{ 0.0F, 0.0F, 0.0F };
    }

    /// Component-wise linear interpolation. t wraps as [0..1]; outside the
    /// range it extrapolates, because silently clamping inside the math layer is
    /// the kind of magic that produces slow-motion animations that nobody can
    /// explain.
    Vec3 Lerp(const Vec3& other, float t) const
    {
        return Vec3{ x + (other.x - x) * t, y + (other.y - y) * t, z + (other.z - z) * t };
    }

    float Distance(const Vec3& other) const { return (*this - other).Length(); }
    float DistanceSquared(const Vec3& other) const { return (*this - other).LengthSquared(); }

    // ---- shared constants --------------------------------------------------

    /// Zero / unit / canonical basis vectors.
    ///
    /// WHY THE `static const` + OUT-OF-LINE DEFINITIONS, NOT PLAIN
    /// `static constexpr Vec3 Zero{...};`: the class is still incomplete while
    /// its member list is parsed, so initializing a static member OF THE SAME
    /// TYPE inline is C2027 on MSVC. The declaration stays constant-initialized
    /// and usable everywhere a constant is expected; the definition simply
    /// lives after the closing brace of the class.
    static const Vec3 Zero;
    static const Vec3 One;

    // Engine convention: D3D left-handed, so Forward is +Z. Right is +X, Up is +Y.
    static const Vec3 Right;
    static const Vec3 Up;
    static const Vec3 Forward;
};

inline const Vec3 Vec3::Zero{ 0.0F, 0.0F, 0.0F };
inline const Vec3 Vec3::One{ 1.0F, 1.0F, 1.0F };
inline const Vec3 Vec3::Right{ 1.0F, 0.0F, 0.0F };
inline const Vec3 Vec3::Up{ 0.0F, 1.0F, 0.0F };
inline const Vec3 Vec3::Forward{ 0.0F, 0.0F, 1.0F };

} // namespace Nova::Math

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
