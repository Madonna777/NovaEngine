// ===========================================================================
//  Primitives.h
// ---------------------------------------------------------------------------
//  Procedural meshes, so the engine has something to draw before it has an
//  asset pipeline and a regression test that needs no data file.
// ===========================================================================
#pragma once

#include <Renderer/Mesh.h>

namespace Nova::Renderer
{
/// A unit-ish cube centred on the origin, with per-face normals and UVs.
///
/// @param size Edge length of the cube. The result spans -size/2 to +size/2 on
///             each axis.
///
/// @return 24 vertices and 36 indices. 24 rather than 8 is the point: the four
///         corners of a face cannot share a normal with the corners of the
///         adjacent face, so a cube with one vertex per corner has to average
///         them - and an averaged normal is wrong on every face at once, which
///         is what makes a "8-vertex cube" look flat and lifeless.
MeshData GenerateCube(float size = 1.0F);

/// A horizontal quad in the XZ plane, facing +Y, centred on the origin.
///
/// @param width         Extent along X.
/// @param depth         Extent along Z.
/// @param subdivisions  Edges per axis. Must be at least 1. More than 1 exists
///                      for vertex-lit work and for later displacement; a flat
///                      unlit quad needs 1.
///
/// @return (subdivisions + 1)^2 vertices and subdivisions^2 * 6 indices.
///
/// @note Winding is counter-clockwise seen from +Y, which is the face-forward
///       convention the rest of the engine's primitives follow. The rasteriser
///       currently culls nothing, so this is documentation rather than a
///       requirement - and it becomes a requirement the moment back-face
///       culling is enabled, which is why it is settled now rather than then.
MeshData GeneratePlane(float width = 1.0F, float depth = 1.0F, int subdivisions = 1);

/// A UV sphere centred on the origin.
///
/// @param radius   Sphere radius.
/// @param segments Longitude divisions. Must be at least 3.
/// @param rings    Latitude divisions. Must be at least 2, because a sphere
///                 needs at least one ring band to have any surface at all.
///
/// @return Normals point radially outward and are exactly unit length, which is
///         what makes the Lambert term meaningful across the whole surface.
MeshData GenerateSphere(float radius = 1.0F, int segments = 32, int rings = 16);

} // namespace Nova::Renderer