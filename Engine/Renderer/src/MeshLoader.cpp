// ===========================================================================
//  MeshLoader.cpp
// ---------------------------------------------------------------------------
//  Assimp-backed model loading.
//
//  WHY ASSIMP IS THE RIGHT DEPENDENCY AND ALSO WHY IT IS A BIG ONE: it reads
//  forty-odd formats behind one interface, so the engine writes one importer
//  instead of forty. The cost is a library that pulls in zlib, minizip,
//  poly2tri, pugixml and OpenSSL - a lot of surface area in a shipping engine,
//  which is why this is the only place in the codebase that is allowed to know
//  Assimp exists. Everything above this file deals in MeshData.
// ===========================================================================

#include <Renderer/MeshLoader.h>

#include <Core/Log.h>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>

namespace Nova::Renderer
{
namespace
{
/// The thread-local Importer that GetLastError reads from.
///
/// @note Assimp's Importer owns parsing state - the scene, the error string,
///       internal caches - so it must outlive the aiScene pointers derived from
///       it and must not be shared across threads. One Importer per thread gives
///       both properties without a lock, at the cost of one small allocation
///       per thread rather than per load.
///
/// WHY A FUNCTION-LOCAL STATIC AND NOT A NAMESPACE ONE: a namespace-scope
/// Importer would be constructed during static initialisation, before the log
/// system exists and before any error could be reported. The first call is
/// always on a loading thread, which is exactly where construction belongs.
Assimp::Importer& GetImporter()
{
    thread_local Assimp::Importer importer;
    return importer;
}

/// Post-processing flags, named rather than written inline at the call.
///
/// @note Triangulate and GenSmoothNormals rather than GenNormals: the latter
///       discards the file's own normals and replaces them with per-face ones,
///       which turns every exported smooth surface visibly faceted. Generating
///       them only when the file has none is the difference between an
///       imported model matching its source and importing successfully.
///
/// @note JoinIdenticalVertices before the others: it is what makes a file that
///       was exported with split vertices produce correct smooth normals, and it
///       also collapses the duplicate pole vertices a sphere generator
///       deliberately creates.
///
/// @note CalcTangentSpace needs UVs, so it runs after Triangulate and is a
///       no-op for geometry without them rather than an error.
///
/// @note FlipUVs is a post-process flag here even though LoadFromFile also
///       takes a flipV parameter. Assimp's version flips what the FILE says;
///       the parameter flips what the ENGINE wants, and a file can be authored
///       either way. Two knobs on two different concerns, and only one of them
///       belongs to the caller.
unsigned int GetPostProcessFlags()
{
    return aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
           aiProcess_SortByPType | aiProcess_GenSmoothNormals |
           aiProcess_CalcTangentSpace | aiProcess_OptimizeMeshes;
}

/// Converts one aiMesh into CPU-side geometry.
///
/// @param aiMesh       Source mesh.
/// @param mesh         Assimp's node name, for the part's identifier.
/// @param scale        Uniform scale applied to positions.
/// @param flipV        Invert the V texture coordinate.
/// @param out          Part to fill.
///
/// @return False when the mesh has no faces, which happens for point clouds
///         and line sets and is not an error - just nothing to draw.
bool ConvertMesh(const aiMesh* aiMesh, const std::string& name, float scale, bool flipV,
                 MeshPart& out)
{
    if (aiMesh == nullptr || aiMesh->mNumFaces == 0 || aiMesh->mNumVertices == 0)
    {
        return false;
    }

    out.name       = name;
    out.meshIndex  = aiMesh->mNumVertices > 0U ? static_cast<unsigned>(out.meshIndex) : 0U;
    out.hasTangents = aiMesh->HasTangentsAndBitangents();

    // Assimp indexes into meshes with its own array, and aiVector3D is three
    // floats in the same order as ours - but the conversion is written out
    // rather than memcpy'd, because "these two layouts are compatible" is
    // exactly the assumption that breaks silently when either changes.
    out.data.vertices.reserve(aiMesh->mNumVertices);

    for (unsigned int vertex = 0; vertex < aiMesh->mNumVertices; ++vertex)
    {
        const aiVector3D& sourcePosition = aiMesh->mVertices[vertex];
        const aiVector3D& sourceNormal   = aiMesh->mNormals[vertex];

        Math::Vec3 position(sourcePosition.x, sourcePosition.y, sourcePosition.z);
        position = position * scale;

        Math::Vec3 normal(0.0F, 0.0F, 1.0F);
        if (aiMesh->HasNormals())
        {
            normal = Math::Vec3(sourceNormal.x, sourceNormal.y, sourceNormal.z);
        }

        // Defaulting to (0,0) rather than asserting: a file without UVs is
        // common and perfectly renderable untextured. An exception here would
        // make "no texture coordinates" a load failure, which is not what it is.
        Math::Vec2 uv(0.0F, 0.0F);
        if (aiMesh->HasTextureCoords(0))
        {
            uv = Math::Vec2(aiMesh->mTextureCoords[0][vertex].x, aiMesh->mTextureCoords[0][vertex].y);
        }
        if (flipV)
        {
            uv.y = 1.0F - uv.y;
        }

        out.data.vertices.push_back(Vertex::Create(position, normal, uv));
    }

    out.data.indices.reserve(static_cast<std::size_t>(aiMesh->mNumFaces) * 3U);

    // Triangulate guarantees three indices per face, but the check is kept
    // because the flag is a request, not a guarantee: a post-processing step
    // that fails on one face leaves a polygon in the array, and reading three
    // indices from a four-index face would walk into the next face's data.
    for (unsigned int face = 0; face < aiMesh->mNumFaces; ++face)
    {
        const aiFace& current = aiMesh->mFaces[face];
        if (current.mNumIndices != 3U)
        {
            NOVA_WARN("Mesh '{}' face {} has {} indices, expected 3; skipped",
                      name, face, current.mNumIndices);
            continue;
        }

        for (unsigned int corner = 0; corner < 3U; ++corner)
        {
            out.data.indices.push_back(static_cast<uint32_t>(current.mIndices[corner]));
        }
    }

    return !out.data.indices.empty();
}
} // namespace

namespace MeshLoader
{
std::string GetLastError()
{
    return GetImporter().GetErrorString() != nullptr ? GetImporter().GetErrorString() : "";
}

MeshFile LoadFromFile(const std::string& path, float scale, bool flipV)
{
    MeshFile result;
    result.sourcePath = path;

    if (!(scale > 0.0F))
    {
        NOVA_ERROR("MeshLoader: scale must be positive, got {}", scale);
        return result;
    }

    Assimp::Importer& importer = GetImporter();

    // ReadFile rather than ReadFileFromMemory: the file is the unit of failure
    // that users understand, and Assimp's error string names the file.
    const aiScene* scene = importer.ReadFile(path, GetPostProcessFlags());

    if (scene == nullptr || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0 ||
        scene->mRootNode == nullptr)
    {
        // Logged as a warning, not an error: a content pipeline probing for an
        // asset that may legitimately be absent is doing the right thing, and an
        // error-level line for it trains people to ignore the log.
        NOVA_WARN("MeshLoader: failed to load '{}': {}", path, GetLastError());
        return result;
    }

    unsigned int meshIndex = 0;

    // Walk the node tree rather than reading mMeshes directly. A file that uses
    // node instancing - which is how most DCC exporters write a character - has
    // far fewer meshes than it has nodes, and several nodes point at the same
    // mesh. Reading mMeshes alone would silently drop every instance except the
    // first, and the model would arrive with missing limbs and no error.
    std::vector<const aiNode*> pending;
    pending.push_back(scene->mRootNode);

    while (!pending.empty())
    {
        const aiNode* node = pending.back();
        pending.pop_back();

        for (unsigned int index = 0; index < node->mNumMeshes; ++index)
        {
            const unsigned int current = node->mMeshes[index];

            MeshPart part;
            part.meshIndex = current;

            if (ConvertMesh(scene->mMeshes[current], node->mName.C_Str(), scale, flipV, part))
            {
                NOVA_DEBUG("MeshLoader: '{}' mesh '{}' -> {} vertices, {} indices",
                           path, part.name, part.data.vertices.size(), part.data.indices.size());
                result.meshes.push_back(std::move(part));
            }

            ++meshIndex;
        }

        for (unsigned int child = 0; child < node->mNumChildren; ++child)
        {
            pending.push_back(node->mChildren[child]);
        }
    }

    static_cast<void>(meshIndex);

    if (result.meshes.empty())
    {
        NOVA_WARN("MeshLoader: '{}' loaded but contained no renderable geometry", path);
        return result;
    }

    result.ok = true;

    std::size_t totalVertices = 0;
    std::size_t totalIndices  = 0;
    for (const MeshPart& part : result.meshes)
    {
        totalVertices += part.data.vertices.size();
        totalIndices  += part.data.indices.size();
    }

    NOVA_INFO("MeshLoader: '{}' -> {} mesh(es), {} vertices, {} triangles", path,
              result.meshes.size(), totalVertices, totalIndices / 3U);

    return result;
}

} // namespace MeshLoader

} // namespace Nova::Renderer