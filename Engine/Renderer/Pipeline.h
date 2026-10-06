// ===========================================================================
//  Pipeline.h
// ---------------------------------------------------------------------------
//  The pipeline state object (PSO) and the root signature a draw commands
//  against. A PSO is D3D12's version of a DirectX 11 state block: an
//  immutable object bundling shaders, input layout, blend state, rasteriser
//  state, depth-stencil usage, and RTV formats. Creating one does not allocate
//  GPU memory - there is no implicit dispatch - but it costs milliseconds on
//  first use (the driver builds the GPU binary), which is why it is created at
//  startup and reused, never per-frame.
// ===========================================================================
#pragma once

#include <Renderer/D3D12Helpers.h>
#include <Renderer/Shader.h>

#include <cstdint>
#include <string>

namespace Nova::Renderer
{
/// Owns a root signature and a graphics PSO for one rendering setup.
///
/// @note Non-copyable and non-movable like everything COM-owning in this
///       module: the COM objects are reference counted and copying would just
///       add reference count traffic while duplicating lifetimes.
class D3D12Pipeline final
{
public:
    D3D12Pipeline()  = default;
    ~D3D12Pipeline() = default;

    D3D12Pipeline(const D3D12Pipeline&)            = delete;
    D3D12Pipeline& operator=(const D3D12Pipeline&) = delete;
    D3D12Pipeline(D3D12Pipeline&&)                 = delete;
    D3D12Pipeline& operator=(D3D12Pipeline&&)      = delete;

    /// Builds the PSO for an already-compiled shader pair.
    ///
    /// @param device           Device to create the pipeline state from.
    /// @param shader           Compiled shader pair, including its reflected
    ///                         input layout.
    /// @param rootSignature    Root signature the shader was compiled against.
    ///                         Must be the SAME OBJECT the command list will be
    ///                         recorded with: D3D12 validates the pairing at
    ///                         draw time, and the debug layer's report names
    ///                         neither of the two signatures that mismatched.
    ///
    /// @throws std::runtime_error if the pipeline state cannot be created.
    void Create(ID3D12Device* device, const Shader& shader, ID3D12RootSignature* rootSignature);

    /// Compiles the shaders, creates the root signature and builds the PSO.
    ///
    /// @param device           Device to create resources from.
    /// @param vertexShaderPath    HLSL path to compile for the vs_5_0 target.
    /// @param pixelShaderPath    HLSL path to compile for the ps_5_0 target.
    /// @param inputElements      Fixed-function input assembly layout. Describes
    ///                           the attribute names, formats and offsets the
    ///                           vertex shader declares - without it the input
    ///                           assembler has no idea how to read the vertex
    ///                           buffer.
    /// @param inputElementCount Number of entries in @p inputElements.
    ///
    /// @throws std::runtime_error if a shader fails to compile or any D3D12
    ///         creation call fails. Shader compilation errors carry the DXC
    ///         error message in the exception text.
    void Create(ID3D12Device*                  device,
                const std::wstring&             vertexShaderPath,
                const std::wstring&             pixelShaderPath,
                const D3D12_INPUT_ELEMENT_DESC* inputElements,
                std::uint32_t                   inputElementCount);

    /// Binds the PSO and root signature onto @p commandList, plus the primitive
    /// topology this pipeline expects (triangles).
    ///
    /// @note Topology is NOT part of a PSO. D3D12_GRAPHICS_PIPELINE_STATE_DESC
    ///       has PrimitiveTopologyType, but the actual topology - triangle list,
    ///       triangle strip, line - is a command-list setting. Set it wherever
    ///       you draw; here because one topology per pipeline is the 99% case,
    ///       and the call is idempotent per command list.
    void Bind(ID3D12GraphicsCommandList* commandList) const;

    /// @return The root signature, for future code that binds descriptor tables.
    [[nodiscard]] ID3D12RootSignature* GetRootSignature() const noexcept { return rootSignature_.Get(); }

private:
    ComPtr<ID3D12RootSignature> rootSignature_;
    ComPtr<ID3D12PipelineState>  pso_;
};

} // namespace Nova::Renderer
