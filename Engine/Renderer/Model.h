// ===========================================================================
//  Model.h
// ---------------------------------------------------------------------------
//  A model file on the GPU: its meshes plus the transform that places it.
// ===========================================================================
#pragma once

#include <Math/Mat4.h>
#include <Math/Transform.h>
#include <Renderer/Mesh.h>
#include <Renderer/MeshLoader.h>

#include <memory>
#include <string>
#include <vector>

namespace Nova::Renderer
{
/// A loaded model: every mesh in one file, sharing one transform.
///
/// WHY A MODEL IS NOT A MESH AND NOT A LIST OF MESHES: the transform is the
/// difference. One character file is dozens of meshes that must move as a rigid
/// body, so the transform belongs to the file rather than to each mesh. A
/// design that stored the transform per mesh would either duplicate it
/// everywhere or put it in the first mesh and read it from there, which is a
/// bug waiting for a scene graph.
///
/// WHY THE TRANSFORM IS A Transform AND NOT A Mat4: Transform holds position,
/// rotation and scale separately, which is what a caller actually has - a
/// spinning animation is a yaw angle, not sixteen floats someone multiplied.
/// GetModelMatrix composes the matrix on demand, so no caller has to remember
/// to invalidate one.
class Model final
{
public:
    Model()  = default;
    ~Model() = default;

    Model(const Model&)            = delete;
    Model& operator=(const Model&) = delete;
    Model(Model&&) noexcept        = default;
    Model& operator=(Model&&) noexcept = default;

    /// Loads a model file and uploads every mesh it contains.
    ///
    /// @param device Device to create the buffers from.
    /// @param path   UTF-8 path to the model file.
    /// @param scale  Uniform scale applied to every vertex.
    /// @param flipV  Invert the V texture coordinate.
    ///
    /// @return True on success. A failed load logs the reason and returns false
    ///         with no meshes, so the caller's next question is a null check
    ///         rather than a partial draw of half a character.
    ///
    /// @throws Nothing for a bad path: a missing asset is a runtime condition,
    ///         not an exception. Allocation failure still throws from below.
    [[nodiscard]] bool LoadFromFile(ID3D12Device* device, const std::string& path,
                                    float scale = 1.0F, bool flipV = false);

    /// Builds a model from CPU data, bypassing the loader.
    ///
    /// This is the path the procedural generators use, and it exists so that a
    /// cube is not required to be written to a file and read back to be
    /// visible. Without it, "no asset pipeline yet" would mean "no geometry at
    /// all", which is how renderers end up untestable for months.
    [[nodiscard]] bool CreateFromData(ID3D12Device* device, MeshData&& data);

    /// Binds and draws every mesh, in order.
    void Draw(ID3D12GraphicsCommandList* commandList) const noexcept;

    /// Binds and draws every mesh @p instanceCount times.
    void DrawInstanced(ID3D12GraphicsCommandList* commandList,
                       UINT instanceCount = 1) const noexcept;

    /// Local-to-world transform.
    ///
    /// @note Two accessors rather than one. A const-only accessor would force
    ///       every animation to build a whole Transform and assign it, which is
    ///       the right shape for a scene graph that owns and recomposes
    ///       transforms wholesale, and the wrong shape for a caller setting one
    ///       component. Both are legitimate, so both are available - and the
    ///       mutable one is named for what it does rather than hidden behind a
    ///       setter that would suggest the transform is a value to be replaced
    ///       rather than a thing with state.
    [[nodiscard]] const Math::Transform& GetTransform() const noexcept { return transform_; }
    [[nodiscard]] Math::Transform& GetTransform() noexcept { return transform_; }
    void SetTransform(const Math::Transform& value) noexcept { transform_ = value; }

    /// @return The composed model matrix. Recomputed per call rather than
    ///         cached, because a cache needs an invalidation rule and the
    ///         matrix product is sixteen multiplies.
    [[nodiscard]] Math::Mat4 GetModelMatrix() const noexcept
    {
        return transform_.GetModelMatrix();
    }

    [[nodiscard]] std::size_t GetMeshCount() const noexcept { return meshes_.size(); }
    [[nodiscard]] bool IsEmpty() const noexcept { return meshes_.empty(); }

    /// @return Path the model was loaded from, or empty for CreateFromData.
    [[nodiscard]] const std::string& GetSourcePath() const noexcept { return sourcePath_; }

    [[nodiscard]] explicit operator bool() const noexcept { return !meshes_.empty(); }

private:
    /// Meshes held by pointer because Mesh owns D3D12 resources and must not be
    /// copied, while a std::vector of them would reallocate and copy.
    /// unique_ptr also keeps the move semantics correct for free.
    std::vector<std::unique_ptr<Mesh>> meshes_;

    Math::Transform transform_{};

    std::string sourcePath_;
};

} // namespace Nova::Renderer