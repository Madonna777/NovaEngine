// ===========================================================================
//  MathUtils.h
// ---------------------------------------------------------------------------
//  The small always-on helpers the math types above cannot carry as members:
//  angle units, clamping, interpolation, and a tiny reproducible random source.
// ===========================================================================
#pragma once

#include <cmath>
#include <random>

namespace Nova::Math
{
/// Identity-scale constants for the engine's coordinate conventions.
inline constexpr float Pi     = 3.14159265358979323846F;
inline constexpr float TwoPi  = 6.28318530717958647692F;
inline constexpr float HalfPi = 1.57079632679489661923F;

/// Degrees -> radians. D3D and every math header in this engine think in
/// radians, but humans edit the former.
inline float Radians(float degrees) { return degrees * (Pi / 180.0F); }

/// Radians -> degrees, for the reverse journey into UI.
inline float Degrees(float radians) { return radians * (180.0F / Pi); }

/// Returns value constrained to [minValue, maxValue].
inline float Clamp(float value, float minValue, float maxValue)
{
    if (value < minValue)
    {
        return minValue;
    }
    if (value > maxValue)
    {
        return maxValue;
    }
    return value;
}

/// Smaller of the two - named Min to avoid the Windows min macro when this
/// header reaches a stricter consumer.
inline float Min(float a, float b) { return a < b ? a : b; }

/// Larger of the two.
inline float Max(float a, float b) { return a > b ? a : b; }

/// Component-wise linear interpolation on floats. t outside [0, 1]
/// extrapolates on purpose, identically to Vec3::Lerp.
inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }

/// Classic Hermite smoothstep with an explicit plateau range. Easing functions
/// belong to the same header because they are the closest cousin of Lerp -
/// both interpolate, this one just does it with an S-curve.
inline float SmoothStep(float edge0, float edge1, float x)
{
    const float t = Clamp((x - edge0) / (edge1 - edge0), 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

/// A single reproducible, process-wide random float in [minValue, maxValue].
///
/// @note The mt19937 state is seeded once from std::random_device via
///       a function-local static - no global constructor ordering issue, and
///       thread-safe by the C++11 magic-static rule. Fine for colours and
///       camera jitter; not the source of shuffle decks or fairness-critical
///       outcomes.
inline float RandomFloat(float minValue = 0.0F, float maxValue = 1.0F)
{
    static std::mt19937 generator{ std::random_device{}() };
    std::uniform_real_distribution<float> distribution(minValue, maxValue);
    return distribution(generator);
}

} // namespace Nova::Math
