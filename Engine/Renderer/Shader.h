// ===========================================================================
//  Shader.h
// ---------------------------------------------------------------------------
//  A compiled vertex+pixel shader pair, plus the reflection data the renderer
//  needs to bind it: the vertex input layout and the constant buffer registry.
// ===========================================================================
#pragma once

#include <Renderer/D3D12Helpers.h>

// d3d12shader.h declares ID3D12ShaderReflection, the interface D3DReflect
// returns. It is a separate header from d3d12.h because it is only needed by
// code that reflects bytecode - shaders on the hot path of a shipping game
// never do, and the header is not small.
#include <d3d12shader.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Nova::Renderer
{
/// One constant buffer a shader reads, as reported by D3D reflection.
///
/// @note `registerIndex` is the shader's own register (b0, b1, ...) and NOT the
///       root parameter slot. They coincide only when the root signature is
///       built to mirror the shader, which is the engine's convention.
struct ConstantBufferDescription
{
    std::string name;
    UINT        registerIndex  = 0;
    UINT        registerSpace  = 0;
    UINT        sizeInBytes    = 0;
};

/// A vertex attribute the shader expects, with the DXGI format resolved from the
/// semantic name.
///
/// WHY THE FORMAT IS GUESSED FROM THE SEMANTIC: D3D reflection reports only the
/// semantic name and index, never a type. There is no API that returns "this
/// POSITION is float3". The mapping is therefore a convention this engine owns,
/// and it must match both the HLSL struct in common.hlsli and the CPU vertex
/// struct in VertexBuffer.h.
struct InputLayoutElement
{
    std::string semanticName;
    UINT        semanticIndex  = 0;

    /// Number of float lanes the shader actually reads, read out of the
    /// component mask during reflection. This is why a float2 TEXCOORD and a
    /// float3 POSITION need no per-shader special case.
    UINT        componentCount  = 0;

    DXGI_FORMAT format          = DXGI_FORMAT_UNKNOWN;
    UINT        byteOffset      = 0;
};

/// A compiled shader pair with its reflection data.
class Shader final
{
public:
    Shader()  = default;
    ~Shader() = default;

    Shader(const Shader&)            = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&&) noexcept        = default;
    Shader& operator=(Shader&&) noexcept = default;

    /// Compiles both stages from HLSL source on disk and reflects them.
    ///
    /// @param vertexShaderPath Path to the .hlsl with the vertex entry point.
    /// @param pixelShaderPath  Path to the .hlsl with the pixel entry point.
    ///
    /// @throws std::runtime_error carrying the DXC/fxc diagnostic text on any
    ///         compile failure, and std::invalid_argument for a file that does
    ///         not exist. Throwing rather than returning an error code keeps the
    ///         "shader is broken" case impossible to ignore at a call site.
    [[nodiscard]] static Shader CompileFromFile(const std::wstring& vertexShaderPath,
                                                const std::wstring& pixelShaderPath);

    /// @return Bytecode handle for CreateGraphicsPipelineState. Valid only while
    ///         this Shader is alive: the blob is a ComPtr member, not a copy.
    [[nodiscard]] D3D12_SHADER_BYTECODE GetVertexShaderBytecode() const noexcept;

    [[nodiscard]] D3D12_SHADER_BYTECODE GetPixelShaderBytecode() const noexcept;

    /// @return Input layout elements discovered by reflection, in the canonical
    ///         semantic order (see BuildInputLayout).
    [[nodiscard]] const std::vector<InputLayoutElement>& GetInputLayoutElements() const noexcept
    {
        return inputLayout_;
    }

    /// @return Constant buffers the shaders read, sorted by register.
    [[nodiscard]] const std::vector<ConstantBufferDescription>& GetConstantBuffers() const noexcept
    {
        return constantBuffers_;
    }

    /// Builds the D3D12_INPUT_ELEMENT_DESC array for the pipeline state.
    ///
    /// The offsets come from reflection's semantic list accumulated in the
    /// canonical order, NOT from declaration order in the HLSL: D3D reflection
    /// returns input parameters sorted by SEMANTIC NAME, not by their order in
    /// the struct. Deriving offsets from that order would produce a layout that
    /// disagrees with the CPU's vertex struct the moment the names are not
    /// alphabetical. So the canonical order is pinned here, and the CPU-side
    /// struct in VertexBuffer.h must use the same one.
    [[nodiscard]] std::vector<D3D12_INPUT_ELEMENT_DESC> BuildInputLayout() const;

    /// @return True when reflection ran and found no input parameters, which
    ///         means the shader declares no vertex input at all.
    [[nodiscard]] bool HasVertexInput() const noexcept { return !inputLayout_.empty(); }

    /// @return Byte stride between consecutive vertices, accumulated from the
    ///         reflected layout. Must equal sizeof(the CPU vertex struct): a
    ///         mismatch here reads attributes from the wrong offsets and is
    ///         silent.
    [[nodiscard]] UINT GetVertexStride() const noexcept { return vertexStride_; }

private:
    /// Runs D3D reflection over both stages and fills the two description lists.
    void Reflect();

    ComPtr<ID3DBlob> vertexBytecode_;
    ComPtr<ID3DBlob> pixelBytecode_;

    std::vector<InputLayoutElement>         inputLayout_;
    std::vector<ConstantBufferDescription> constantBuffers_;

    /// Byte stride between vertices, derived from the layout above.
    UINT vertexStride_ = 0;
};

} // namespace Nova::Renderer