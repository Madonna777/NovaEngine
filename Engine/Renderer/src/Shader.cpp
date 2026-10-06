// ===========================================================================
//  Shader.cpp
// ---------------------------------------------------------------------------
//  Runtime HLSL compilation and the reflection that feeds the pipeline state.
//
//  WHY D3DCompileFromFile AND NOT A BUILD-TIME BLOB CACHE
//  ------------------------------------------------------
//  Two exist and this milestone uses the runtime one:
//
//    - fxc at build time produces .cso files that load instantly and never
//      touch a compiler DLL at run time. That is where a shipped game ends up.
//    - D3DCompileFromFile at run time uses D3DCompiler_47.dll, which ships with
//      every supported Windows version. Compile time is a few dozen
//      milliseconds, once, at startup.
//
//  The runtime path is chosen because the alternative requires a custom CMake
//  target per shader, a staging rule, and a cache-invalidation story for when
//  a .hlsl changes - all of which is real work whose failure mode is a stale
//  blob that renders the previous version of the shader with no error anywhere.
//  The migration is a self-contained change inside this file.
//
//  THE INCLUDE HANDLER IS NOT OPTIONAL
//  -----------------------------------
//  lit_vs.hlsl and lit_ps.hlsl both do `#include "common.hlsli"`. With a NULL
//  ID3DInclude the compiler resolves relative includes against the PROCESS
//  WORKING DIRECTORY - the executable's folder under F5, the project folder
//  under a CI runner. The same shader then compiles in one place and fails in
//  the other with "could not open include file".
//
//  The SDK already has the right value: D3D_COMPILE_STANDARD_FILE_INCLUDE is
//  the magic pointer that makes fxc resolve #include relative to the file
//  containing the directive, which is exactly the required behaviour. A custom
//  ID3DInclude implementation - the obvious first move - is 60 lines of file
//  handling to reimplement something the library already does, and it would be
//  a second place to fix when the shader layout grows an include/ subdirectory.
// ===========================================================================

#include <Renderer/Shader.h>

#include <Core/Log.h>

#include <d3dcompiler.h>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace Nova::Renderer
{
namespace
{
/// Shader Model 5.0 profiles. Shader Model 6 requires DXC and DXIL, which is
/// the documented end state in Shaders/README.md; SM 5.0 is what fxc emits and
/// what a DXGI swap chain with a plain D3D12 device consumes without an export
/// shim.
constexpr const char* kVertexProfile = "vs_5_0";
constexpr const char* kPixelProfile  = "ps_5_0";

/// Entry point name. Both HLSL files expose `main`; a shader that wants a
/// different one passes it here rather than through a per-file convention.
constexpr const char* kEntryPoint = "main";

/// Compiles one stage, throwing with the compiler's own diagnostic text.
///
/// @param flags0 Extra D3DCOMPILE flags; Debug builds add DEBUG and
///               SKIP_OPTIMIZATION so the debug layer can resolve instruction
///               addresses back to HLSL lines.
ComPtr<ID3DBlob> CompileStage(const std::wstring& path, const char* profile, UINT flags0)
{
    ComPtr<ID3DBlob> bytecode;
    ComPtr<ID3DBlob> errors;

    // ENABLE_STRICTNESS turns "implicit truncation" warnings into errors. An
    // implicit float3 -> float4 conversion is how a shader ends up writing
    // garbage into the w component, and the diagnostic is the only chance to
    // catch it.
    const UINT flags = flags0 | D3DCOMPILE_ENABLE_STRICTNESS;

    // NOTE THE SECOND ARGUMENT: this SDK's D3DCompileFromFile takes a macro
    // table BEFORE the include handler, unlike the more commonly seen signature.
    // A nullptr macro table means no preprocessor defines, which is what a
    // shipping build wants - a shader that needs a #define should compile it
    // into a variant rather than branch at run time. See Shaders/README.md on
    // permutations for where the defines come from.
    const HRESULT result =
        D3DCompileFromFile(path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, kEntryPoint,
                           profile, flags, 0, bytecode.GetAddressOf(), errors.GetAddressOf());

    if (FAILED(result))
    {
        std::string message = "Shader compilation failed: " + ToUtf8(path.c_str());
        if (errors && errors->GetBufferSize() > 0)
        {
            message += "\n";
            message.append(static_cast<const char*>(errors->GetBufferPointer()),
                           errors->GetBufferSize());
        }
        else
        {
            message += "\n(no diagnostic text; fxc returned " + std::to_string(result) + ")";
        }
        throw std::runtime_error(message);
    }

    return bytecode;
}
} // namespace

// ===========================================================================
//  Compilation
// ===========================================================================

Shader Shader::CompileFromFile(const std::wstring& vertexShaderPath,
                               const std::wstring& pixelShaderPath)
{
    UINT flags = 0;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    Shader shader;
    shader.vertexBytecode_ = CompileStage(vertexShaderPath, kVertexProfile, flags);
    shader.pixelBytecode_  = CompileStage(pixelShaderPath, kPixelProfile, flags);
    shader.Reflect();

    NOVA_INFO("Shader compiled: {} + {} ({} input elements, {} constant buffers)",
              ToUtf8(vertexShaderPath.c_str()), ToUtf8(pixelShaderPath.c_str()),
              shader.inputLayout_.size(), shader.constantBuffers_.size());

    return shader;
}

D3D12_SHADER_BYTECODE Shader::GetVertexShaderBytecode() const noexcept
{
    return D3D12_SHADER_BYTECODE{ vertexBytecode_->GetBufferPointer(),
                                  vertexBytecode_->GetBufferSize() };
}

D3D12_SHADER_BYTECODE Shader::GetPixelShaderBytecode() const noexcept
{
    return D3D12_SHADER_BYTECODE{ pixelBytecode_->GetBufferPointer(),
                                  pixelBytecode_->GetBufferSize() };
}

} // namespace Nova::Renderer