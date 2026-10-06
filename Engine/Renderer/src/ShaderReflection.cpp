// ===========================================================================
//  ShaderReflection.cpp
// ---------------------------------------------------------------------------
//  The reflection half of Shader, split from the compilation half so neither
//  file has to explain the other's half.
//
//  WHICH REFLECTION API THIS SDK ACTUALLY HAS
//  -----------------------------------------
//  There are two unrelated interfaces both called ID3D12ShaderReflection, and
//  picking the wrong one is a wall of "is not a member of" errors:
//
//    - The fxc-era one: GetInputSignatureParameterCount, GetConstantBufferCount,
//      D3D12_SHADER_INPUT_SIGNATURE_DESC. NOT DECLARED in the Windows 10 SDK
//      (10.0.26100). It existed in DirectXShaderCompiler's own headers.
//    - The DXC-era one in <d3d12shader.h>, which this file uses:
//      GetDesc() reports the counts, GetInputParameterDesc() reads the input
//      signature, GetResourceBindingDesc() reads resource bindings.
//
//  WHAT REFLECTION DOES AND DOES NOT TELL US
//  -----------------------------------------
//  Does tell us, and this is genuinely useful:
//    - how many input parameters and bound resources there are;
//    - the semantic name and index of every input;
//    - the COMPONENT MASK of every input, which is the number of floats the
//      attribute occupies. So "POSITION is float3" is not a convention here -
//      it is read out of the shader, and a float2 POSITION in a future mesh
//      needs no change in this file.
//
//  Does NOT tell us:
//    - the byte offset of the attribute inside the CPU's vertex struct. The
//      struct's order is the engine's, and this file accumulates offsets from
//      the canonical order below. Three places agree on that order:
//      ShaderReflection.cpp, VertexBuffer.h and common.hlsli's VSInput.
//
//  WHY ORDERING IS PINNED BY HAND
//  ------------------------------
//  The input signature arrives in the order the compiler emitted it, which is
//  not guaranteed to be declaration order. Deriving offsets from the arrival
//  order would therefore produce a layout that disagrees with the CPU's vertex
//  struct the moment the compiler reorders two attributes - and the symptom is
//  a mesh whose vertices are shuffled per attribute, which renders.
// ===========================================================================

#include <Renderer/Shader.h>

#include <Core/Log.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace Nova::Renderer
{
namespace
{
/// The vertex layout order. This list IS the contract with VertexBuffer.h's
/// struct and common.hlsli's VSInput: all three must agree, and none of them
/// can be reordered without updating the other two.
enum class Semantic : std::uint8_t
{
    Position,
    Normal,
    Tangent,
    Binormal,
    Uv,
    Color,
    Count
};

Semantic SemanticFromName(const char* name) noexcept
{
    if (name == nullptr)
    {
        return Semantic::Count;
    }
    if (std::strcmp(name, "POSITION") == 0) { return Semantic::Position; }
    if (std::strcmp(name, "NORMAL") == 0)   { return Semantic::Normal; }
    if (std::strcmp(name, "TANGENT") == 0)  { return Semantic::Tangent; }
    if (std::strcmp(name, "BINORMAL") == 0) { return Semantic::Binormal; }
    if (std::strcmp(name, "TEXCOORD") == 0) { return Semantic::Uv; }
    if (std::strcmp(name, "COLOR") == 0)    { return Semantic::Color; }
    return Semantic::Count;
}

/// DXGI format from a component count.
///
/// The four formats are the only ones a vertex attribute can usefully be: the
/// pipeline reads them as raw float lanes, and the CPU side stores floats in
/// the vertex struct. A shader declaring a uint POSITION would be binding a
/// float vertex buffer over it, so the format is chosen here rather than
/// derived from the shader's component type.
DXGI_FORMAT FormatForComponentCount(UINT count) noexcept
{
    switch (count)
    {
    case 1:  return DXGI_FORMAT_R32_FLOAT;
    case 2:  return DXGI_FORMAT_R32G32_FLOAT;
    case 3:  return DXGI_FORMAT_R32G32B32_FLOAT;
    default: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    }
}

/// Byte size from a component count.
UINT PayloadSizeForComponentCount(UINT count) noexcept
{
    return count * sizeof(float);
}

/// Number of float lanes a component mask selects.
///
/// Components always run X, Y, Z, W in order and never have holes, so counting
/// the set bits is the lane count. The SDK spells these D3D10_COMPONENT_MASK_X /
/// _Y / _Z / _W; they are written as literals here because this Windows SDK's
/// d3dcommon.h does not declare the names, and a mask bit pattern is stable
/// ABI - it is a contract with the compiled bytecode, not a header detail.
constexpr BYTE kComponentMaskX = 0x1;
constexpr BYTE kComponentMaskY = 0x2;
constexpr BYTE kComponentMaskZ = 0x4;
constexpr BYTE kComponentMaskW = 0x8;

UINT ComponentCountFromMask(BYTE mask) noexcept
{
    UINT count = 0;
    if ((mask & kComponentMaskX) != 0) { ++count; }
    if ((mask & kComponentMaskY) != 0) { ++count; }
    if ((mask & kComponentMaskZ) != 0) { ++count; }
    if ((mask & kComponentMaskW) != 0) { ++count; }
    return count;
}
} // namespace

void Shader::Reflect()
{
    // ---- input layout, from the VERTEX stage only ------------------------
    //
    // The pixel stage has no vertex inputs, and reflecting both would merge two
    // different signatures into one ambiguous list.
    ComPtr<ID3D12ShaderReflection> vertexReflection;
    if (SUCCEEDED(D3DReflect(vertexBytecode_->GetBufferPointer(),
                              vertexBytecode_->GetBufferSize(),
                              IID_PPV_ARGS(&vertexReflection))))
    {
        D3D12_SHADER_DESC shaderDescription{};
        if (SUCCEEDED(vertexReflection->GetDesc(&shaderDescription)))
        {
            for (UINT index = 0; index < shaderDescription.InputParameters; ++index)
            {
                D3D12_SIGNATURE_PARAMETER_DESC parameter{};
                if (FAILED(vertexReflection->GetInputParameterDesc(index, &parameter)))
                {
                    continue;
                }

                const Semantic semantic = SemanticFromName(parameter.SemanticName);
                if (semantic == Semantic::Count)
                {
                    // Not fatal: a shader may legitimately declare a semantic
                    // the engine has no vertex format for. Skipped with a log
                    // rather than bound to a guessed layout - a wrong guess
                    // renders garbage, a skip renders nothing, and the second is
                    // at least attributable.
                    NOVA_WARN("Shader input semantic '{}' has no engine layout; skipped",
                              parameter.SemanticName != nullptr ? parameter.SemanticName : "(null)");
                    continue;
                }

                InputLayoutElement element;
                element.semanticName  = parameter.SemanticName;
                element.semanticIndex = parameter.SemanticIndex;
                element.componentCount =
                    ComponentCountFromMask(static_cast<BYTE>(parameter.Mask));
                element.format = FormatForComponentCount(element.componentCount);

                inputLayout_.push_back(element);
            }
        }
    }

    // Sort into the canonical order rather than the compiler's emission order,
    // then accumulate byte offsets in THAT order so the layout is contiguous and
    // matches the CPU struct.
    std::sort(inputLayout_.begin(), inputLayout_.end(),
              [](const InputLayoutElement& a, const InputLayoutElement& b)
              {
                  return static_cast<int>(SemanticFromName(a.semanticName.c_str())) <
                         static_cast<int>(SemanticFromName(b.semanticName.c_str()));
              });

    UINT offset = 0;
    for (InputLayoutElement& element : inputLayout_)
    {
        element.byteOffset = offset;
        offset += PayloadSizeForComponentCount(element.componentCount);
    }
    vertexStride_ = offset;

    // ---- constant buffers, from BOTH stages ------------------------------
    //
    // Merged by (space, register): the two stages must agree about a buffer,
    // and a buffer only one stage reads still has to be declared in the root
    // signature. A duplicate at the same slot with a different size is a real
    // authoring error and is logged, because both sizes cannot be right and the
    // symptom otherwise is a shader reading past a buffer that was sized for
    // the other stage.
    const ComPtr<ID3DBlob> stages[] = { vertexBytecode_, pixelBytecode_ };
    for (const ComPtr<ID3DBlob>& stage : stages)
    {
        ComPtr<ID3D12ShaderReflection> reflection;
        if (FAILED(D3DReflect(stage->GetBufferPointer(), stage->GetBufferSize(),
                              IID_PPV_ARGS(&reflection))))
        {
            continue;
        }

        D3D12_SHADER_DESC stageDescription{};
        if (FAILED(reflection->GetDesc(&stageDescription)))
        {
            continue;
        }

        for (UINT index = 0; index < stageDescription.BoundResources; ++index)
        {
            D3D12_SHADER_INPUT_BIND_DESC binding{};
            if (FAILED(reflection->GetResourceBindingDesc(index, &binding)) ||
                binding.Type != D3D_SIT_CBUFFER)
            {
                continue;
            }

            // The SDK's D3D12_SHADER_INPUT_BIND_DESC has no size field - only a
            // name, a bind point and a space. The size comes from the constant
            // buffer descriptor of the SAME name, which is why the lookup is by
            // name rather than by index: the two lists are indexed differently
            // and pairing them positionally would read the wrong buffer's size.
            UINT sizeInBytes = 0;
            if (ID3D12ShaderReflectionConstantBuffer* buffer =
                    reflection->GetConstantBufferByName(binding.Name);
                buffer != nullptr)
            {
                D3D12_SHADER_BUFFER_DESC bufferDescription{};
                if (SUCCEEDED(buffer->GetDesc(&bufferDescription)))
                {
                    sizeInBytes = bufferDescription.Size;
                }
            }

            ConstantBufferDescription description;
            description.name          = binding.Name != nullptr ? binding.Name : "<unnamed>";
            description.registerIndex = binding.BindPoint;
            description.registerSpace = binding.Space;
            description.sizeInBytes   = sizeInBytes;

            const auto existing = std::find_if(
                constantBuffers_.begin(), constantBuffers_.end(),
                [&description](const ConstantBufferDescription& candidate)
                {
                    return candidate.registerIndex == description.registerIndex &&
                           candidate.registerSpace == description.registerSpace;
                });

            if (existing != constantBuffers_.end())
            {
                if (existing->sizeInBytes != description.sizeInBytes)
                {
                    NOVA_ERROR("Shader stages disagree on constant buffer b{}: {} bytes vs {}",
                               description.registerIndex, existing->sizeInBytes,
                               description.sizeInBytes);
                }
            }
            else
            {
                constantBuffers_.push_back(description);
            }
        }
    }

    std::sort(constantBuffers_.begin(), constantBuffers_.end(),
              [](const ConstantBufferDescription& a, const ConstantBufferDescription& b)
              { return a.registerIndex < b.registerIndex; });
}

std::vector<D3D12_INPUT_ELEMENT_DESC> Shader::BuildInputLayout() const
{
    std::vector<D3D12_INPUT_ELEMENT_DESC> layout;
    layout.reserve(inputLayout_.size());

    for (const InputLayoutElement& element : inputLayout_)
    {
        D3D12_INPUT_ELEMENT_DESC desc{};
        desc.SemanticName         = element.semanticName.c_str();
        desc.SemanticIndex        = element.semanticIndex;
        desc.Format               = element.format;
        desc.InputSlot            = 0;
        desc.AlignedByteOffset    = element.byteOffset;
        desc.InputSlotClass       = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
        desc.InstanceDataStepRate = 0;

        // SemanticName points into this element's std::string. inputLayout_ is a
        // member of the same Shader, so the string outlives the descriptor and
        // the pointer stays valid for as long as the PSO does.
        layout.push_back(desc);
    }

    return layout;
}
} // namespace Nova::Renderer