// ===========================================================================
//  MeshLoader.h
// ---------------------------------------------------------------------------
//  Turning model files into CPU-side meshes, via Assimp.
// ===========================================================================
#pragma once

#include <Renderer/Mesh.h>

#include <string>
#include <vector>

namespace Nova::Renderer
{
/// One node's worth of loaded geometry, plus what Assimp said about it.
///
/// @note `meshIndex` is the index Assimp assigned to the aiMesh, and it is kept
///       because a file with many meshes otherwise loses the link back to the
///       node it came from - the name is what a content pipeline needs to rename
///       or recolour a specific part, and a bare integer is not enough.
struct MeshPart
{
    /// Identifier from the loader's LoadFromFile. Empty when the file has a
    /// single unnamed mesh and the caller does not care.
    std::string name;

    /// Assimp's mesh index, for correlating with the source file.
    unsigned meshIndex = 0;

    MeshData data;

    /// True when Assimp supplied tangents and a bitangent. The vertex type has
    /// no room for them yet, so this is reported rather than silently dropped:
    /// a caller about to apply a normal map needs to know the file had one.
    bool hasTangents = false;
};

/// The result of loading one file: every mesh in it, in Assimp's order.
///
/// @note A file with one mesh is the common case and this still returns a
///       vector. Normalising "one mesh" and "many meshes" to the same shape at
///       the call site is where the `if (meshes.size() == 1)` special case
///       would otherwise appear, and it would appear once per call site.
struct MeshFile
{
    /// Path the file was loaded from, as given to LoadFromFile.
    std::string sourcePath;

    /// Per-mesh geometry. Empty when the load failed; check `ok`.
    std::vector<MeshPart> meshes;

    /// False when the file could not be read or parsed, or contained no
    /// renderable geometry. The failure reason is in the log rather than here:
    /// an error string that exists in two places is one that will be read in
    /// the wrong place eventually.
    bool ok = false;

    [[nodiscard]] bool IsEmpty() const noexcept { return meshes.empty(); }
};

/// Stateless namespace, not a class with static members: the loader holds no
/// state, so making it a type would invite a future cache to be added as a
/// member, and a cache has different lifetime and threading rules than the
/// parse does. Keeping it a set of free functions means that decision has to be
/// made on purpose later.
namespace MeshLoader
{
/// Loads every mesh in a model file.
///
/// @param path UTF-8 path to an .obj, .fbx, .gltf, .ply, .dae or anything else
///            Assimp was built with a reader for.
/// @param scale Uniform scale applied to every position. D3D-style metres or
///             engine units are both defensible and a factor of 100 between
///             them is the difference between a model that fills the screen and
///             one that is a speck; taking it here means no caller has to
///             post-process the vertices.
/// @param flipV  Invert the V texture coordinate. Half the formats disagree
///             about which way V points and getting it wrong looks like a
///             texture rendered upside down rather than like a coordinate
///             convention.
///
/// @return The loaded meshes, or `ok == false` with the reason logged. This
///         function does not throw for a bad file: a missing or malformed
///         asset is an expected runtime condition that a content pipeline must
///         survive, not an exceptional one. A programmer error - a null device -
///         is a different category and still throws.
[[nodiscard]] MeshFile LoadFromFile(const std::string& path, float scale = 1.0F,
                                    bool flipV = false);

/// @return Assimp's own message for the most recent failed load, or an empty
///         string. Assimp keeps the detail - the offending token, the line, the
///         unsupported feature - and discarding it in favour of "load failed"
///         means the log cannot answer the question the reader will ask.
[[nodiscard]] std::string GetLastError();

} // namespace MeshLoader

} // namespace Nova::Renderer