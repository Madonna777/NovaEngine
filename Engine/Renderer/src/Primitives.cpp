// ===========================================================================
//  Primitives.cpp
// ---------------------------------------------------------------------------
//  Procedural mesh construction.
//
//  THE ONE INVARIANT EVERY GENERATOR KEEPS: the winding order and the normal
//  are derived from the same pair of edge vectors. Each face is emitted from a
//  corner plus two edges, the normal is cross(u, v) normalised, and the
//  triangles follow that same corner sequence. So a normal can never disagree
//  with the winding by accident - there is no second place to get it wrong.
//  This is what makes the "cube renders inside-out" bug structurally
//  impossible here rather than something to check for by eye.
// ===========================================================================

#include <Renderer/Primitives.h>

#include <Math/MathUtils.h>
#include <Math/Vec3.h>

#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace Nova::Renderer
{
namespace
{
/// Appends one quad as two triangles.
///
/// @param out      Mesh being built.
/// @param corner   Corner of the face.
/// @param edgeU    First edge, from @p corner.
/// @param edgeV    Second edge, from @p corner.
/// @param uvOrigin UV of @p corner, for placing a texture per face.
///
/// @note The four corners are corner, corner+u, corner+u+v, corner+v. Traversed
///       in that order they appear counter-clockwise to a viewer standing on the
///       side cross(u, v) points at, which is the definition of "the front
///       face" for every winding convention in common use.
void AppendQuad(MeshData& out, const Math::Vec3& corner, const Math::Vec3& edgeU,
                const Math::Vec3& edgeV, const Math::Vec2& uvOrigin)
{
    const std::size_t first = out.vertices.size();

    // Member form, not a static Cross(a, b): the type's own cross product is an
    // instance method, and a free-function wrapper over it would be a second
    // spelling of the same operation for no gain.
    const Math::Vec3 normal = edgeU.Cross(edgeV).Normalized();
    const Math::Vec2 uStep(1.0F, 0.0F);
    const Math::Vec2 vStep(0.0F, 1.0F);

    out.vertices.push_back(Vertex::Create(corner, normal, uvOrigin));
    out.vertices.push_back(Vertex::Create(corner + edgeU, normal, uvOrigin + uStep));
    out.vertices.push_back(Vertex::Create(corner + edgeU + edgeV, normal,
                                          uvOrigin + uStep + vStep));
    out.vertices.push_back(Vertex::Create(corner + edgeV, normal, uvOrigin + vStep));

    // 0,1,2 and 0,2,3 - the shared diagonal is 0-2 rather than 1-3 because the
    // 0-2 diagonal follows the same edge directions as the face's own winding.
    // Either diagonal is geometrically valid; picking the consistent one means
    // a face split across a different diagonal order does not flip.
    const uint32_t base = static_cast<uint32_t>(first);
    out.indices.insert(out.indices.end(),
                       { base, base + 1U, base + 2U, base, base + 2U, base + 3U });
}
} // namespace

MeshData GenerateCube(float size)
{
    if (!(size > 0.0F))
    {
        throw std::invalid_argument("GenerateCube requires a positive size");
    }

    const float half = size * 0.5F;

    MeshData mesh;
    mesh.vertices.reserve(24U);
    mesh.indices.reserve(36U);

    // Corner plus two edges per face, written out rather than looped over axis
    // signs: each face's edge pair differs, and a sign table would need the pair
    // as data - this list with an extra indirection and a lookup to get wrong.
    //
    // The rule that keeps them right: each edge is a FULL `size` along ONE axis,
    // and the two are perpendicular. cross(u, v) then comes out along the third
    // axis with a sign that names the face, so the normal is axis-aligned by
    // construction rather than by being typed in. An earlier version passed a
    // half-extent vector as one edge, which produced normals like (s/2, -s/2, 0)
    // - diagonally outward, and a cube whose lighting is subtly wrong on every
    // face at once.
    AppendQuad(mesh, Math::Vec3(half, -half, half), Math::Vec3(0, 0, -size),
               Math::Vec3(0, size, 0), Math::Vec2(0.0F, 0.0F));   // +X
    AppendQuad(mesh, Math::Vec3(-half, -half, -half), Math::Vec3(0, 0, size),
               Math::Vec3(0, size, 0), Math::Vec2(0.0F, 0.0F));   // -X
    AppendQuad(mesh, Math::Vec3(-half, half, half), Math::Vec3(size, 0, 0),
               Math::Vec3(0, 0, -size), Math::Vec2(0.0F, 0.0F));  // +Y
    AppendQuad(mesh, Math::Vec3(-half, -half, -half), Math::Vec3(size, 0, 0),
               Math::Vec3(0, 0, size), Math::Vec2(0.0F, 0.0F));   // -Y
    AppendQuad(mesh, Math::Vec3(-half, -half, half), Math::Vec3(size, 0, 0),
               Math::Vec3(0, size, 0), Math::Vec2(0.0F, 0.0F));   // +Z
    AppendQuad(mesh, Math::Vec3(half, -half, -half), Math::Vec3(-size, 0, 0),
               Math::Vec3(0, size, 0), Math::Vec2(0.0F, 0.0F));  // -Z

    return mesh;
}

MeshData GeneratePlane(float width, float depth, int subdivisions)
{
    if (!(width > 0.0F) || !(depth > 0.0F) || subdivisions < 1)
    {
        throw std::invalid_argument(
            "GeneratePlane requires a positive size and at least one subdivision");
    }

    const int   cells = subdivisions;
    const float halfW = width * 0.5F;
    const float halfD = depth * 0.5F;

    MeshData mesh;
    mesh.vertices.reserve(static_cast<std::size_t>(cells + 1) * static_cast<std::size_t>(cells + 1));
    mesh.indices.reserve(static_cast<std::size_t>(cells) * static_cast<std::size_t>(cells) * 6U);

    const Math::Vec3 normal = Math::Vec3::Up;

    // Row-major over Z then X, because the index maths below is one base
    // offset plus a delta. Generating in a different order than the indices
    // assume is the classic off-by-one-row bug, and the row count is the
    // number of SEGMENTS, not vertices: cells + 1 vertices per row.
    for (int row = 0; row <= cells; ++row)
    {
        const float v     = static_cast<float>(row) / static_cast<float>(cells);
        const float localZ = -halfD + v * depth;

        for (int column = 0; column <= cells; ++column)
        {
            const float u       = static_cast<float>(column) / static_cast<float>(cells);
            const float localX  = -halfW + u * width;

            mesh.vertices.push_back(
                Vertex::Create(Math::Vec3(localX, 0.0F, localZ), normal, Math::Vec2(u, v)));
        }
    }

    const auto rowStride = static_cast<uint32_t>(cells + 1);

    for (int row = 0; row < cells; ++row)
    {
        for (int column = 0; column < cells; ++column)
        {
            const uint32_t topLeft = static_cast<uint32_t>(row) * rowStride +
                                    static_cast<uint32_t>(column);
            const uint32_t topRight = topLeft + 1U;
            const uint32_t bottomLeft = topLeft + rowStride;
            const uint32_t bottomRight = bottomLeft + 1U;

            // Counter-clockwise seen from +Y. The alternative winding here is
            // the one that makes a ground plane vanish the moment back-face
            // culling is switched on, because the camera is above it.
            mesh.indices.insert(mesh.indices.end(),
                                { topLeft, bottomLeft, bottomRight,
                                  topLeft, bottomRight, topRight });
        }
    }

    return mesh;
}

} // namespace Nova::Renderer