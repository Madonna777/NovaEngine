// ===========================================================================
//  Pipeline.cpp
// ===========================================================================

#include <Renderer/Pipeline.h>

#include <Renderer/D3D12Context.h>

#include <Core/Log.h>

#include <d3dcompiler.h>

#include <cstdint>
#include <stdexcept>
#include <string>

namespace Nova::Renderer
{
namespace
{
/// Compiles one HLSL entry point with the runtime compiler.
///
/// @param path   Source file.
/// @param entry  Entry point name in the file.
/// @param target Shader Model profile, e.g. "vs_5_0" or "ps_5_0".
///
/// WHY D3DCompileFromFile AND NOT DXC: the Shaders README targets SM 6.x via DXC
/// for the shipped renderer, but that is a load-time plan, not this milestone.
/// D3DCompileFromFile runs D3DCompiler_47.dll, which ships in every supported
/// Windows version, compiles Shader Model 5.x, and needs only d3dcompiler.lib -
/// the simplest correct thing that can produce bytecode for a PSO today.
ComPtr<ID3DBlob> CompileShader(const std::wstring& path,
                               const char*         entry,
                               const char*         target)
{
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    // Skips optimisation so the Info Queue can resolve instructions back to
    // source lines. A release build would rather be small and fast than
    // debuggable at the shader level.
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ComPtr<ID3DBlob> codeBlob;
    ComPtr<ID3DBlob> errorBlob;

    const HRESULT result =
        D3DCompileFromFile(path.c_str(), nullptr, nullptr, entry, target, flags, 0,
                           codeBlob.GetAddressOf(), errorBlob.GetAddressOf());

    if (FAILED(result))
    {
        // The error blob carries the DXC diagnostic text - shader lines with
        // the same care as a compiler. Throwing it into the exception message
        // means a broken shader and a missing file read identically at the
        // call site, without the caller re-parsing anything.
        std::string message = "Shader compilation failed: ";
        message += errorBlob ? static_cast<const char*>(errorBlob->GetBufferPointer())
                             : Platform::GetLastErrorMessage(static_cast<std::uint32_t>(result)).c_str();
        throw std::runtime_error(message);
    }

    return codeBlob;
}

/// Creates the root signature for the pipeline.
///
/// A ROOT SIGNATURE is the contract between the command list and the shaders:
/// it describes exactly which root parameters the shaders may read, which
/// registers they live in, and which space. It is the D3D12 equivalent of the
/// binding layout in DirectX 11's constant buffer slots, generalised: descriptor
/// tables, root descriptors (raw GPU pointers to CBVs/SRVs/UAVs), and immediate
/// constants are all first-class citizens here.
///
/// OURS IS DELIBERATELY ONE CBV TABLE, AND THE TRIANGLE SHADER DOES NOT READ
/// IT. That is legal: the root signature must be a superset of what shaders
/// use, so an unused parameter is harmless. The moment a real constant buffer
/// (MVP, material colour) arrives, the geometry is exactly this slot, register
/// b0, updated once per draw.
ComPtr<ID3D12RootSignature> CreateRootSignature(ID3D12Device* device)
{
    ComPtr<ID3D12RootSignature> rootSignature;

    D3D12_DESCRIPTOR_RANGE ranges[1]{};
    ranges[0].RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    ranges[0].NumDescriptors     = 1;
    ranges[0].BaseShaderRegister = 0;
    ranges[0].RegisterSpace      = 0;
    ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[1]{};
    rootParameters[0].ParameterType                      = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[0].DescriptorTable.pDescriptorRanges   = ranges;
    rootParameters[0].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.NumParameters     = 1;
    desc.pParameters       = rootParameters;
    desc.NumStaticSamplers = 0;
    desc.pStaticSamplers   = nullptr;
    // ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT gives the IA stage access to the vertex
    // buffer it needs to feed the vertex shader. Without it the layout declared
    // on the PSO is dead weight, and D3D12 reports the input layout as unused
    // instead of a validation failure people actually read.
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signatureBlob;
    ComPtr<ID3DBlob> errorBlob;
    NOVA_THROW_IF_FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                                     signatureBlob.GetAddressOf(),
                                                     errorBlob.GetAddressOf()));

    NOVA_THROW_IF_FAILED(device->CreateRootSignature(0, signatureBlob->GetBufferPointer(),
                                                     signatureBlob->GetBufferSize(),
                                                     IID_PPV_ARGS(&rootSignature)));
    return rootSignature;
}

/// Builds the PSO from a reflected shader and the context's root signature.
///
/// @note The input layout comes from the shader's own reflection rather than a
///       caller-supplied array. A hand-written layout and a reflected one can
///       disagree - a semantic order or an offset - and the disagreement is
///       silent: the pipeline is created, the draw executes, and the vertices
///       arrive shuffled per attribute. One source of truth removes the class
///       of bug rather than the instance of it.
ComPtr<ID3D12PipelineState> CreatePipelineState(ID3D12Device*              device,
                                                ID3D12RootSignature*         rootSignature,
                                                D3D12_SHADER_BYTECODE        vertexShader,
                                                D3D12_SHADER_BYTECODE        pixelShader,
                                                const D3D12_INPUT_ELEMENT_DESC* inputLayout,
                                                std::uint32_t                inputElementCount)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};

    // The bytecode is a (pointer, size) pair, not an ID3DBlob: the PSO copies
    // what it needs at creation, so the Shader is free to release its blobs the
    // moment Create returns.
    desc.pRootSignature = rootSignature;
    desc.VS             = vertexShader;
    desc.PS             = pixelShader;

    // Fixed function state that the triangle does not need but the PSO must
    // still name. Blending disabled means the pixel shader wins every pixel.
    // Rasteriser culls nothing: a triangle is a triangle regardless of winding,
    // and a culling pipeline would silently hide the opposite-handed setup.
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.RasterizerState.CullMode                        = D3D12_CULL_MODE_NONE;
    desc.RasterizerState.FillMode                        = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.DepthClipEnable                 = TRUE;
    // Depth/stencil disabled because there is no depth attachment on the swap
    // chain. The first geometry that needs depth lands a DSV buffer next to the
    // RTV heap, and this flag flips at the same time.
    desc.DepthStencilState.DepthEnable     = FALSE;
    desc.DepthStencilState.StencilEnable   = FALSE;
    desc.DSVFormat                         = DXGI_FORMAT_UNKNOWN;

    desc.NumRenderTargets          = 1;
    desc.RTVFormats[0]             = D3D12Context::kBackBufferFormat;
    desc.SampleDesc.Count          = 1;
    desc.SampleDesc.Quality        = 0;
    desc.SampleMask                = UINT_MAX;
    desc.PrimitiveTopologyType     = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.InputLayout.NumElements   = inputElementCount;
    desc.InputLayout.pInputElementDescs = inputLayout;

    ComPtr<ID3D12PipelineState> pso;
    NOVA_THROW_IF_FAILED(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso)));
    return pso;
}
} // namespace

void D3D12Pipeline::Create(ID3D12Device* device, const Shader& shader,
                           ID3D12RootSignature* rootSignature)
{
    if (device == nullptr || rootSignature == nullptr)
    {
        // A null device or root signature here would surface as an obscure
        // E_INVALID_ARGUMENT from CreateGraphicsPipelineState, or - worse -
        // succeed and fail at draw time with the debug layer's "root signature
        // mismatch". The check is free and the message is actionable.
        throw std::invalid_argument("D3D12Pipeline::Create requires a device and a root signature");
    }

    const std::vector<D3D12_INPUT_ELEMENT_DESC> layout = shader.BuildInputLayout();

    rootSignature_ = rootSignature;
    pso_ = CreatePipelineState(device, rootSignature, shader.GetVertexShaderBytecode(),
                              shader.GetPixelShaderBytecode(), layout.data(),
                              static_cast<UINT>(layout.size()));

    NOVA_INFO("Pipeline state created for {} input elements", layout.size());
}

void D3D12Pipeline::Create(ID3D12Device*                  device,
                           const std::wstring&             vertexShaderPath,
                           const std::wstring&             pixelShaderPath,
                           const D3D12_INPUT_ELEMENT_DESC* inputElements,
                           std::uint32_t                   inputElementCount)
{
    NOVA_INFO("Compiling shaders: {} + {}", ToUtf8(vertexShaderPath.c_str()),
              ToUtf8(pixelShaderPath.c_str()));

    ComPtr<ID3DBlob> vertexBlob = CompileShader(vertexShaderPath, "main", "vs_5_0");
    ComPtr<ID3DBlob> pixelBlob  = CompileShader(pixelShaderPath,  "main", "ps_5_0");

    rootSignature_ = CreateRootSignature(device);
    pso_           = CreatePipelineState(device, rootSignature_.Get(),
                                          { vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize() },
                                          { pixelBlob->GetBufferPointer(),  pixelBlob->GetBufferSize()  },
                                          inputElements, inputElementCount);

    NOVA_INFO("Pipeline state created");
}

void D3D12Pipeline::Bind(ID3D12GraphicsCommandList* commandList) const
{
    commandList->SetPipelineState(pso_.Get());
    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    // Primitive topology is a command-list setting, not part of the PSO:
    // see Pipeline.h. Whatever follows in the recording is evaluated as
    // triangle list until this is changed again.
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

} // namespace Nova::Renderer