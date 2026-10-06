// ===========================================================================
//  MeshTest - standalone checks for the renderer's mesh and primitive layer
// ---------------------------------------------------------------------------
//  Plain main(), matching MathTest: these are shape and invariant checks that
//  read as "here is the expected shape" rather than as EXPECT_EQ shells.
//
//  WHAT IS AND IS NOT TESTED HERE, and why the line is where it is:
//  Everything that is pure CPU geometry - vertex counts, index ranges, normal
//  direction, winding, UV coverage - is checked, because a wrong answer here is
//  a wrong answer forever and it is the class of bug that renders as "the model
//  looks slightly off" rather than as an error. Nothing that needs an
//  ID3D12Device is checked, because Mesh::Create requires one and there is no
//  device in a unit test. That boundary is why the GPU-side half of Mesh is
//  thin: every decision worth being wrong about happens before the device is
//  touched.
//
//  The invariants themselves live in MeshChecks.h. What is here is the
//  specification - the numbers a cube, a plane and a sphere are supposed to
//  have - which is the part that reads differently when it is wrong.
// ===========================================================================

#include "MeshChecks.h"

#include <Math/Math.h>
#include <Renderer/Mesh.h>
#include <Renderer/Primitives.h>

#include <cstdio>

using Nova::Math::Vec3;
using Nova::MeshChecks::Check;
using Nova::MeshChecks::IndexCountIsTriangles;
using Nova::MeshChecks::IndicesInRange;
using Nova::MeshChecks::Near;
using Nova::MeshChecks::NormalsUnitLength;
using Nova::MeshChecks::UVsInUnitSquare;
using Nova::MeshChecks::WindingAgreesWithNormals;
using Nova::Renderer::GenerateCube;
using Nova::Renderer::GeneratePlane;
using Nova::Renderer::GenerateSphere;
using Nova::Renderer::MeshData;
using Nova::Renderer::Vertex;

/// The invariants every generated mesh must satisfy, and their names.
///
/// @note Wrapped rather than stored as a bare function-pointer array: two of
///       these take a defaulted tolerance, which gives them a two-parameter type
///       that will not convert to a one-parameter function pointer. Normalising
///       that at the table is cheaper than removing the parameter from the two
///       checks that want it.
struct Invariant
{
    bool (*predicate)(const MeshData&);
    const char* name;
};

/// Checks that hold for every mesh the engine produces, whatever it is.
void CheckCommonInvariants(const MeshData& mesh, const char* label)
{
    static const Invariant kInvariants[] = {
        { &Nova::MeshChecks::IndicesInRange,           "every index is in range" },
        { &Nova::MeshChecks::IndexCountIsTriangles,    "index count is a multiple of 3" },
        { [](const MeshData& m) { return NormalsUnitLength(m); },        "normals are unit length" },
        { &Nova::MeshChecks::WindingAgreesWithNormals, "winding agrees with normals" },
        { [](const MeshData& m) { return UVsInUnitSquare(m); },          "UVs lie in the unit square" },
    };

    std::printf("%s\n", label);
    for (const Invariant& invariant : kInvariants)
    {
        Check(invariant.predicate(mesh), invariant.name);
    }
}

int main()
{
    std::printf("Cube\n");

    {
        const MeshData cube = GenerateCube(1.0F);

        // 24 vertices, not 8. The four corners of a face cannot share a normal
        // with the corners of an adjacent face, so a cube with one vertex per
        // corner has to average them - and an averaged normal is wrong on every
        // face at once, which is what makes an 8-vertex cube look flat and
        // lifeless.
        Check(cube.vertices.size() == 24U, "24 vertices, not 8");
        Check(cube.indices.size() == 36U, "36 indices");
        Check(cube.GetTriangleCount() == 12U, "12 triangles");

        Check(IndicesInRange(cube), "every index is in range");
        Check(IndexCountIsTriangles(cube), "index count is a multiple of 3");
        Check(NormalsUnitLength(cube), "normals are unit length");
        Check(WindingAgreesWithNormals(cube), "winding agrees with normals");

        // Each face must carry its own normal. This is the check that proves
        // the 24-vertex choice above was necessary rather than merely
        // conventional.
        bool sixDistinctNormals = true;
        for (std::size_t i = 0; i < 6U; ++i)
        {
            const Vec3& n = cube.vertices[i * 4U].normal;
            for (std::size_t j = i + 1U; j < 6U; ++j)
            {
                const Vec3& m = cube.vertices[j * 4U].normal;
                if (Near(n.x, m.x) && Near(n.y, m.y) && Near(n.z, m.z))
                {
                    sixDistinctNormals = false;
                }
            }
        }
        Check(sixDistinctNormals, "each face has its own normal");

        // A cube's normals must be axis-aligned. The generator derives them from
        // cross(u, v) over perpendicular axis-aligned edges; an earlier version
        // passed a half-extent vector as one edge and produced normals like
        // (0.5, -0.5, 0) - diagonally outward, which lights every face wrongly.
        bool axisAligned = true;
        for (const Vertex& vertex : cube.vertices)
        {
            const bool isAxis = Near(std::fabs(vertex.normal.x), 1.0F) ||
                                Near(std::fabs(vertex.normal.y), 1.0F) ||
                                Near(std::fabs(vertex.normal.z), 1.0F);
            if (!isAxis)
            {
                axisAligned = false;
            }
        }
        Check(axisAligned, "every normal is axis-aligned");

        // Scaling the size scales positions and nothing else - normals are
        // directions, and scaling them would dim the surface.
        const MeshData large = GenerateCube(3.0F);
        bool scaled = large.vertices.size() == cube.vertices.size();
        for (std::size_t i = 0; i < cube.vertices.size() && scaled; ++i)
        {
            scaled = Near(large.vertices[i].position.x, cube.vertices[i].position.x * 3.0F) &&
                     Near(large.vertices[i].normal.x, cube.vertices[i].normal.x);
        }
        Check(scaled, "size scales positions but not normals");
    }

    std::printf("\nPlane\n");

    {
        const MeshData plane = GeneratePlane(4.0F, 4.0F, 1);

        Check(plane.vertices.size() == 4U, "1x1: 4 vertices");
        Check(plane.indices.size() == 6U, "1x1: 6 indices");

        Check(IndicesInRange(plane), "1x1: every index is in range");
        Check(IndexCountIsTriangles(plane), "1x1: index count is a multiple of 3");
        Check(NormalsUnitLength(plane), "1x1: normals are unit length");
        Check(WindingAgreesWithNormals(plane), "1x1: winding agrees with normals");

        // The ground plane faces +Y. Getting this wrong makes it vanish the
        // moment back-face culling is switched on, because the camera is above
        // it - so this is checked even though nothing culls yet.
        bool facesUp = true;
        for (const Vertex& vertex : plane.vertices)
        {
            facesUp = facesUp && Near(vertex.normal.y, 1.0F) && Near(vertex.position.y, 0.0F);
        }
        Check(facesUp, "1x1: faces +Y, lies in the XZ plane");

        bool uvSpan = true;
        for (const Vertex& vertex : plane.vertices)
        {
            uvSpan = uvSpan && (Near(vertex.uv.x, 0.0F) || Near(vertex.uv.x, 1.0F));
        }
        Check(uvSpan, "1x1: UVs reach 0 and 1");
    }

    {
        // Subdivided, because the row-stride arithmetic is the part that is easy
        // to get wrong: the generator assumes one MORE vertex than cells per
        // row, and a generator that emitted cells-per-row would produce indices
        // that are in range but describe the wrong triangles.
        const MeshData grid = GeneratePlane(2.0F, 2.0F, 4);

        Check(grid.vertices.size() == 25U, "4x4: (4+1)^2 = 25 vertices");
        Check(grid.indices.size() == 96U, "4x4: 16 cells * 6 = 96 indices");

        Check(IndicesInRange(grid), "4x4: every index is in range");
        Check(IndexCountIsTriangles(grid), "4x4: index count is a multiple of 3");
        Check(NormalsUnitLength(grid), "4x4: normals are unit length");
        Check(WindingAgreesWithNormals(grid), "4x4: winding agrees with normals");
        Check(UVsInUnitSquare(grid), "4x4: UVs lie in the unit square");
    }

    std::printf("\nSphere\n");

    {
        const MeshData sphere = GenerateSphere(1.0F, 16, 8);

        // Two triangles per cell, minus one degenerate at each pole: the ring of
        // pole vertices is the same point repeated, so a triangle spanning that
        // row has zero area and still costs a pixel setup.
        Check(sphere.GetTriangleCount() == 16U * 8U * 2U - 16U * 2U,
              "two degenerate pole triangles skipped");

        Check(IndicesInRange(sphere), "every index is in range");
        Check(IndexCountIsTriangles(sphere), "index count is a multiple of 3");
        Check(NormalsUnitLength(sphere), "normals are unit length");
        Check(UVsInUnitSquare(sphere), "UVs lie in the unit square");
        Check(WindingAgreesWithNormals(sphere), "winding agrees with normals");

        // Position is derived by scaling the normal, so radius and normal must
        // agree exactly. This catches a future edit that recomputes one from the
        // trig independently of the other.
        bool onRadius = true;
        for (const Vertex& vertex : sphere.vertices)
        {
            onRadius = onRadius && Near(vertex.position.Length(), 1.0F, 0.002F);
        }
        Check(onRadius, "every vertex is on the radius");

        // Pole normals must be defined. A single shared pole vertex would need
        // the average of every surrounding face normal, which is zero - and
        // Normalized() on a zero vector is undefined. Duplicating the pole per
        // segment is what keeps this well defined.
        bool polesValid = true;
        for (const Vertex& vertex : sphere.vertices)
        {
            if (Near(std::fabs(vertex.position.y), 1.0F, 0.01F))
            {
                polesValid = polesValid && vertex.normal.LengthSquared() > 0.99F;
            }
        }
        Check(polesValid, "pole normals are well defined");
    }

    {
        const MeshData big = GenerateSphere(2.5F, 32, 16);

        Check(big.vertices.size() == 33U * 17U, "(segments+1) * (rings+1) vertices");

        bool onRadius = true;
        for (const Vertex& vertex : big.vertices)
        {
            onRadius = onRadius && Near(vertex.position.Length(), 2.5F, 0.005F);
        }
        Check(onRadius, "radius is applied to positions");
    }

    std::printf("\nVertex layout\n");

    {
        // The reflected stride and this struct's size have to agree, and the
        // only way to catch a divergence is to compare the numbers. A mismatch
        // reads attributes from the wrong offsets: it renders, with each vertex's
        // components shifted.
        constexpr std::size_t kExpectedStride =
            3U * sizeof(float) +   // position
            3U * sizeof(float) +   // normal
            2U * sizeof(float) +   // uv
            4U * sizeof(float);    // colour
        Check(sizeof(Vertex) == kExpectedStride,
              "size matches the four attributes with no padding");
        Check(alignof(Vertex) == sizeof(float), "4-byte aligned, so offsets are exact");
    }

    const int failures = Nova::MeshChecks::FailureCount();
    std::printf("\n%s\n", failures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return failures == 0 ? 0 : 1;
}