// ===========================================================================
//  MeshChecks.h
// ---------------------------------------------------------------------------
//  Shared checks and helpers for MeshTest.
//
//  WHY A HEADER AND NOT ONE MAIN: the invariants below are the ones a mesh can
//  violate silently. Rendering does not report them - a reversed winding draws,
//  a non-unit normal shades wrongly, an out-of-range index reads adjacent
//  memory. Isolating them here means the assertions themselves can be read, and
//  reviewed, as one list rather than scattered through a test body.
// ===========================================================================
#pragma once

#include <Math/Math.h>
#include <Renderer/Mesh.h>

#include <cmath>
#include <cstdint>

namespace Nova::MeshChecks
{
using Nova::Math::Vec3;
using Nova::Renderer::MeshData;
using Nova::Renderer::Vertex;

inline int& FailureCount()
{
    // A function-local static rather than a namespace-scope variable: the
    // counter's initialisation order relative to anything else in this test is
    // not something to depend on, and a static at namespace scope would be
    // constructed during static initialisation where its order is unspecified.
    static int failures = 0;
    return failures;
}

inline void Check(bool condition, const char* name)
{
    std::printf("%-52s %s\n", name, condition ? "OK" : "FAIL");
    if (!condition)
    {
        ++FailureCount();
    }
}

inline bool Near(float a, float b, float epsilon = 0.001F)
{
    return std::fabs(a - b) <= epsilon;
}

/// Every index must address a real vertex.
///
/// This is the single check that catches the most common malformed-mesh bug,
/// and the one Mesh::Create repeats before it will upload anything. D3D12 does
/// not bounds-check an index read, so an out-of-range value is a GPU-side
/// out-of-bounds read whose symptom depends entirely on the driver.
inline bool IndicesInRange(const MeshData& mesh)
{
    for (std::uint32_t index : mesh.indices)
    {
        if (index >= mesh.vertices.size())
        {
            return false;
        }
    }
    return true;
}

inline bool IndexCountIsTriangles(const MeshData& mesh)
{
    return mesh.indices.size() % 3U == 0U;
}

/// Every normal must be unit length.
///
/// A non-unit normal scales the Lambert term, so the shader looks wrong for a
/// reason that is nowhere near the shader.
inline bool NormalsUnitLength(const MeshData& mesh, float epsilon = 0.001F)
{
    for (const Vertex& vertex : mesh.vertices)
    {
        if (!Near(vertex.normal.LengthSquared(), 1.0F, epsilon))
        {
            return false;
        }
    }
    return true;
}

/// The winding of each triangle must agree with its stored normals.
///
/// WHY THIS IS THE CHECK THAT MATTERS MOST, AND NOT THE VERTEX COUNT: a mesh
/// with the right counts and reversed winding renders inside-out. With no
/// back-face culling it still draws - it is just lit from the wrong side and
/// subtly dimmer - and the moment culling is enabled the object disappears.
/// Comparing the geometric face normal against the average of the three stored
/// vertex normals is the only way to catch that without a GPU, and it is what
/// caught the reversed sphere winding in this file's first run.
inline bool WindingAgreesWithNormals(const MeshData& mesh)
{
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
    {
        const Vertex& a = mesh.vertices[mesh.indices[i + 0]];
        const Vertex& b = mesh.vertices[mesh.indices[i + 1]];
        const Vertex& c = mesh.vertices[mesh.indices[i + 2]];

        const Vec3 face     = (b.position - a.position).Cross(c.position - a.position);
        const Vec3 stored   = (a.normal + b.normal + c.normal).Normalized();

        // Same direction, not merely non-parallel: a face normal pointing the
        // other way is the inside-out case this exists to catch. A zero-length
        // face normal means two coincident vertices, which is the degenerate
        // triangle a sphere's pole rows produce if their index order is wrong.
        if (face.LengthSquared() < 1e-12F || face.Dot(stored) <= 0.0F)
        {
            return false;
        }
    }
    return true;
}

/// UVs must lie in the unit square.
///
/// A UV outside it is legal - tiling textures rely on it - so this is checked
/// only for the generators, whose parameterisation is deliberately normalised.
inline bool UVsInUnitSquare(const MeshData& mesh, float epsilon = 0.001F)
{
    for (const Vertex& vertex : mesh.vertices)
    {
        if (vertex.uv.x < -epsilon || vertex.uv.x > 1.0F + epsilon)
        {
            return false;
        }
        if (vertex.uv.y < -epsilon || vertex.uv.y > 1.0F + epsilon)
        {
            return false;
        }
    }
    return true;
}

} // namespace Nova::MeshChecks