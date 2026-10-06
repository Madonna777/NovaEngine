// ===========================================================================
//  Model.cpp
// ===========================================================================

#include <Renderer/Model.h>

#include <Core/Log.h>

#include <string>
#include <utility>

namespace Nova::Renderer
{
bool Model::LoadFromFile(ID3D12Device* device, const std::string& path, float scale, bool flipV)
{
    if (device == nullptr)
    {
        // A null device IS a programmer error, unlike a missing file. Callers
        // checking this get a clear message; callers who do not get an
        // exception rather than a null dereference inside a resource create.
        throw std::invalid_argument("Model::LoadFromFile requires a device");
    }

    // Replacing rather than appending: a second LoadFromFile on a model that is
    // already loaded is a "reload", and the meshes from the first load are
    // garbage from that point on. Clearing first makes that a single rule
    // instead of a question about what the old contents meant.
    meshes_.clear();
    sourcePath_ = path;

    MeshFile file = MeshLoader::LoadFromFile(path, scale, flipV);

    if (!file.ok || file.meshes.empty())
    {
        NOVA_WARN("Model: '{}' produced no geometry; model left empty", path);
        return false;
    }

    meshes_.reserve(file.meshes.size());

    for (MeshPart& part : file.meshes)
    {
        auto mesh = std::make_unique<Mesh>();

        // A mesh that fails here is skipped rather than fatal. Mesh::Create
        // throws on out-of-range indices, which is a real possibility from a
        // malformed file, and losing one part of a character is recoverable
        // where losing the whole model is not.
        try
        {
            mesh->Create(device, part.data.vertices, part.data.indices);
        }
        catch (const std::exception& error)
        {
            NOVA_ERROR("Model: mesh '{}' of '{}' rejected: {}", part.name, path, error.what());
            continue;
        }

        meshes_.push_back(std::move(mesh));
    }

    if (meshes_.empty())
    {
        NOVA_ERROR("Model: every mesh in '{}' was rejected", path);
        return false;
    }

    NOVA_INFO("Model loaded: '{}' with {} GPU mesh(es)", path, meshes_.size());
    return true;
}

bool Model::CreateFromData(ID3D12Device* device, MeshData&& data)
{
    if (device == nullptr)
    {
        throw std::invalid_argument("Model::CreateFromData requires a device");
    }

    meshes_.clear();
    sourcePath_.clear();

    if (data.IsEmpty())
    {
        NOVA_WARN("Model::CreateFromData given empty geometry");
        return false;
    }

    auto mesh = std::make_unique<Mesh>();

    try
    {
        mesh->Create(device, data.vertices, data.indices);
    }
    catch (const std::exception& error)
    {
        // Logged rather than propagated: this is the path the procedural
        // generators use, and a caller asking for a sphere is not in a position
        // to handle an exception from a bad subdivision count any better than a
        // bool would serve.
        NOVA_ERROR("Model::CreateFromData rejected the mesh: {}", error.what());
        return false;
    }

    meshes_.push_back(std::move(mesh));
    NOVA_INFO("Model created from data: {} vertices, {} triangles", data.vertices.size(),
              data.GetTriangleCount());

    return true;
}

void Model::Draw(ID3D12GraphicsCommandList* commandList) const noexcept
{
    DrawInstanced(commandList, 1);
}

void Model::DrawInstanced(ID3D12GraphicsCommandList* commandList, UINT instanceCount) const noexcept
{
    if (commandList == nullptr)
    {
        return;
    }

    // Bind and draw per mesh rather than binding all the vertex buffers and then
    // issuing all the draws. Binding every buffer first needs one slot per mesh
    // in the input assembler, and D3D12 has a hard limit of one vertex buffer
    // slot - so a five-mesh model would have to interleave anyway. Per mesh
    // costs a redundant IASetVertexBuffers when consecutive meshes share a
    // buffer, which is a two-call saving against a hard architectural limit.
    for (const std::unique_ptr<Mesh>& mesh : meshes_)
    {
        if (mesh == nullptr || !*mesh)
        {
            continue;
        }

        mesh->Bind(commandList);
        mesh->DrawIndexedInstanced(commandList, instanceCount);
    }
}

} // namespace Nova::Renderer