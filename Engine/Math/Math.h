// ===========================================================================
//  Math.h
// ---------------------------------------------------------------------------
//  Umbrella header for the whole Nova::Math module: every public math type is
//  one #include away.
//
//  Why a hand-rolled math library for a DirectX 12 engine: DirectXMath ships
//  with the SDK, but its API uses opaque XMVECTOR/XMMATRIX types and CamelCase-
//  violating names that would leak ABI constraints into every game-object
//  struct that stores a vector. Owning Vec3/Mat4 keeps serialisation, hashing
//  (for reflections later), and POD layout under our control, while keeping
//  this header's integration path one simple matrix copy away from DirectXMath.
// ===========================================================================
#pragma once

#include <Math/Vec2.h>
#include <Math/Vec3.h>
#include <Math/Vec4.h>
#include <Math/Mat4.h>
#include <Math/Transform.h>
#include <Math/MathUtils.h>
