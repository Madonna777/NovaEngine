// ===========================================================================
//  Mesh.h
// ---------------------------------------------------------------------------
//  A vertex type, a CPU-side mesh, and the GPU resources that draw one.
// ===========================================================================
#pragma once

#include <Math/Vec2.h>
#include <Math/Vec3.h>
#include <Math/Vec4.h>
#include <Renderer/D3D12Helpers.h>

#include <cstdint>
#include <vector>

namespace Nova::Renderer
{
/// One vertex of a renderable mesh.
///
/// WHY THE FIELD ORDER IS LOAD-BEARING AND NOT A MATTER OF TASTE: three
/// independent places have to agree on it, and none of them can see the others.
/// The canonical order lives in ShaderReflection.cpp, the vertex format is
/// derived from it by reflection, and this struct's layout is what the input
/// assembler's byte offsets read. Reorder a field here and reflection keeps
/// producing the offsets it always produced, so the shader reads the wrong
/// bytes - vertices arrive with each attribute shifted by one component, which
/// renders rather than errors.
///
/// WHY THE MATH TYPES AND NOT float[3]: a union of three floats is exactly as
/// GPU-friendly as Vec3 and it is also a place where position.x can be
/// confused with position.r at a call site. The cost is that Renderer now
/// depends on Nova::Math, which is worth it: Math is a leaf module with no OS
/// or GPU dependency, so the dependency cannot cycle.
struct Vertex
{
    Math::Vec3 position;
    Math::Vec3 normal;
    Math::Vec2 uv;
    Math::Vec4 color;

    /// White, so a mesh whose source format carries no colour still lights
    /// correctly instead of rendering black and looking like a shader bug.
    static Vertex Create(Math::Vec3 pos, Math::Vec3 nrm, Math::Vec2 texCoord);
};

/// A mesh in CPU memory, before any GPU resource exists.
///
/// This is what the loader and the primitive generators produce, and it is
/// deliberately separate from Mesh: a file can be parsed on a worker thread,
/// diffed, saved back out, or sent over the wire, and none of that needs a
/// device. Keeping the two apart also means the loader does not have to
/// tolerate a null device, which is what a "load now, create later" API needs
/// to be usable at all.
struct MeshData
{
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;

    /// One primitive triangle list. Multiple meshes in one file come back as
    /// separate MeshData rather than as one range array, so a caller can skip
    /// the parts it does not want without re-parsing.
    [[nodiscard]] std::size_t GetTriangleCount() const noexcept
    {
        return indices.size() / 3U;
    }

    [[nodiscard]] bool IsEmpty() const noexcept { return vertices.empty() || indices.empty(); }
};

/// GPU-side mesh: an upload-heap vertex buffer, an upload-heap index buffer, and
/// the two views the input assembler consumes.
///
/// WHY THE CPU COPY IS KEPT ALONGSIDE THE GPU ONE: the vertex data has to
/// exist on the CPU to be uploaded in the first place, and dropping it
/// afterwards would mean the only way to inspect a loaded mesh is a GPU
/// readback. Holding it costs a few megabytes for a character mesh and makes
/// the mesh inspectable, hashable and re-uploadable without a round trip. The
/// upload-heap lifetime below is the real constraint that will eventually force
/// the copy to go; it is documented on Create.
class Mesh final
{
public:
    Mesh()  = default;
    ~Mesh() = default;

    Mesh(const Mesh&)            = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&&) noexcept        = default;
    Mesh& operator=(Mesh&&) noexcept = default;

    /// Uploads a mesh into fresh upload-heap buffers.
    ///
    /// @param device   Owning device.
    /// @param vertices Vertex data, copied.
    /// @param indices  Triangle-list indices, copied. Must be empty or all
    ///                 below vertices.size().
    ///
    /// @throws std::invalid_argument for empty data or an out-of-range index,
    ///         and std::runtime_error if a resource cannot be created.
    ///
    /// @note Index count must be a multiple of three. D3D12 does not check
    ///       this: a count of 4 reads one index past the end of the index
    ///       buffer, which is a GPU-side out-of-bounds read rather than an
    ///       error. The check belongs here, where the mistake is still a
    ///       typo instead of a crash on someone else's machine.
    void Create(ID3D12Device* device, const std::vector<Vertex>& vertices,
                const std::vector<uint32_t>& indices);

    /// Binds the vertex buffer to slot 0 and the index buffer to slot 0.
    ///
    /// @note Slot 0 for both, and the draw call below hardcodes the same
    ///       indices. That is a shared constant, not two independent choices:
    ///       they must agree, and one place to change is one place to get
    ///       wrong.
    void Bind(ID3D12GraphicsCommandList* commandList) const noexcept;

    /// Draws the whole mesh as indexed triangles.
    void Draw(ID3D12GraphicsCommandList* commandList) const noexcept;

    /// Draws @p instanceCount copies, continuing the instance index from
    /// @p startInstanceLocation so a per-instance constant buffer can be
    /// selected by the shader rather than by rebinding between draws.
    void DrawIndexedInstanced(ID3D12GraphicsCommandList* commandList,
                              UINT instanceCount = 1,
                              UINT startInstanceLocation = 0) const noexcept;

    [[nodiscard]] const D3D12_VERTEX_BUFFER_VIEW& GetVertexBufferView() const noexcept
    {
        return vertexBufferView_;
    }

    [[nodiscard]] const D3D12_INDEX_BUFFER_VIEW& GetIndexBufferView() const noexcept
    {
        return indexBufferView_;
    }

    [[nodiscard]] std::size_t GetVertexCount() const noexcept { return vertexCount_; }
    [[nodiscard]] std::size_t GetIndexCount() const noexcept { return indexCount_; }

    /// @return True once Create has allocated both buffers.
    [[nodiscard]] explicit operator bool() const noexcept { return vertexCount_ != 0; }

private:
    ComPtr<ID3D12Resource> vertexBuffer_;
    ComPtr<ID3D12Resource> indexBuffer_;

    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    D3D12_INDEX_BUFFER_VIEW  indexBufferView_{};

    std::size_t vertexCount_ = 0;
    std::size_t indexCount_  = 0;
};

} // namespace Nova::Renderer