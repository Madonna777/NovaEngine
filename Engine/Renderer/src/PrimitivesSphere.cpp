// ===========================================================================
//  PrimitivesSphere.cpp
// ---------------------------------------------------------------------------
//  The sphere generator, split from Primitives.cpp because it is the one
//  generator whose vertex positions and normals are computed by different
//  expressions and therefore have to be argued about separately.
// ===========================================================================

#include <Math/MathUtils.h>
#include <Math/Vec2.h>
#include <Math/Vec3.h>
#include <Renderer/Primitives.h>

#include <cstdint>
#include <stdexcept>

namespace Nova::Renderer
{
MeshData GenerateSphere(float radius, int segments, int rings)
{
    // The bounds are not arbitrary politeness. Three segments is the fewest
    // that enclose anything at all, and two rings is the fewest that produce a
    // band of surface - a one-ring sphere is a cone with the tip cut off, which
    // is a shape nobody asked for and which renders as a defect.
    if (!(radius > 0.0F) || segments < 3 || rings < 2)
    {
        throw std::invalid_argument(
            "GenerateSphere requires a positive radius, at least 3 segments and at least 2 rings");
    }

    MeshData mesh;
    mesh.vertices.reserve(static_cast<std::size_t>(segments + 1) *
                          static_cast<std::size_t>(rings + 1));
    mesh.indices.reserve(static_cast<std::size_t>(segments) * static_cast<std::size_t>(rings) * 6U);

    // Rings run from the north pole (phi = 0) to the south pole (phi = pi).
    //
    // WHY THE POLES ARE DUPLICATED PER SEGMENT rather than being single shared
    // vertices: a single pole vertex would need a normal, and its normal is
    // perpendicular to every direction the surrounding triangles can face - the
    // average is zero and Normalized() on that is undefined. Duplicating the
    // pole per segment costs rings*2 extra vertices and gives each one an exact
    // normal, which is why smooth spheres are always built this way.
    for (int ring = 0; ring <= rings; ++ring)
    {
        const float phi   = static_cast<float>(ring) / static_cast<float>(rings) * Math::Pi;
        const float sinPhi = std::sin(phi);
        const float cosPhi = std::cos(phi);

        for (int segment = 0; segment <= segments; ++segment)
        {
            const float theta =
                static_cast<float>(segment) / static_cast<float>(segments) * Math::TwoPi;
            const float sinTheta = std::sin(theta);
            const float cosTheta = std::cos(theta);

            const Math::Vec3 normal(sinPhi * cosTheta, cosPhi, sinPhi * sinTheta);

            // The position is the normal scaled by the radius. Deriving one
            // from the other rather than recomputing the trig is what
            // guarantees the normal is exactly unit length and exactly
            // perpendicular to the surface: there is no second expression that
            // could drift from the first by a rounding error.
            const Math::Vec3 position = normal * radius;

            mesh.vertices.push_back(
                Vertex::Create(position, normal, Math::Vec2(static_cast<float>(segment) /
                                                                  static_cast<float>(segments),
                                                            static_cast<float>(ring) /
                                                                  static_cast<float>(rings))));
        }
    }

    const auto rowStride = static_cast<uint32_t>(segments + 1);

    for (int ring = 0; ring < rings; ++ring)
    {
        for (int segment = 0; segment < segments; ++segment)
        {
            const uint32_t topLeft = static_cast<uint32_t>(ring) * rowStride +
                                     static_cast<uint32_t>(segment);
            const uint32_t bottomLeft = topLeft + rowStride;

            // Two triangles per cell, ordered so the face normal comes out pointing
            // AWAY from the centre.
            //
            // WHY THIS SPECIFIC ORDER, since it is the part that is easy to get
            // wrong in a way nothing reports: rings run north to south and
            // segments run west to east, so the parameter grid is traversed in
            // the opposite rotational sense to a left-handed view down the
            // +Y axis. The pair (TL, BL, TL+1) therefore produces a face normal
            // pointing INWARD, which renders as a sphere that is back-face culled
            // the moment culling is enabled - and which looks entirely correct
            // while nothing is culled, because a lit sphere with inverted
            // normals is lit from the wrong side and subtly dimmer.
            //
            // (TL, TL+1, BL) and (TL+1, BL+1, BL) are the outward-facing order.
            // The MeshTest check "winding agrees with normals" is what holds this
            // in place, because it is not verifiable by reading the indices.
            //
            // Pole rows emit ONE triangle each, and WHICH row degenerates is the whole
            // subtlety - the two are mirror images and need opposite index
            // orders.
            //
            // At ring 0 the TOP row is the north pole repeated once per segment,
            // so topLeft and topLeft+1 are the same point. The triangle must
            // therefore span one pole vertex and BOTH vertices of the row below:
            // { TL, BL+1, BL }. Emitting { TL, BL, BL+1 } instead faces inward.
            //
            // At the last ring it is the BOTTOM row that is degenerate, so the
            // triangle spans one pole vertex and both vertices of the row above:
            // { TL, TL+1, BL }. Reusing ring 0's order here produces a triangle
            // whose two south-pole vertices coincide - a zero-length cross
            // product, no rasterised area, and still a full pixel setup on the
            // hardware.
            if (ring == 0)
            {
                mesh.indices.insert(mesh.indices.end(), { topLeft, bottomLeft + 1U, bottomLeft });
                continue;
            }

            if (ring == rings - 1)
            {
                mesh.indices.insert(mesh.indices.end(), { topLeft, topLeft + 1U, bottomLeft });
                continue;
            }

            mesh.indices.insert(mesh.indices.end(),
                                { topLeft, topLeft + 1U, bottomLeft,
                                  topLeft + 1U, bottomLeft + 1U, bottomLeft });
        }
    }

    return mesh;
}

} // namespace Nova::Renderer